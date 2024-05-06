#include <asm-generic/errno-base.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

/*  meson setup build
    meson compile -C build 
    ./build/tests/dev-null   */
    
#define MAX_PROCESS_COUNT 512 

// store process info 
struct Process {
    long pid;        
    char *name; // copy of argv[0] 
    int status; // -1 if running, exit status or signal + 128 if terminated 
};

struct Process ssp_processes[MAX_PROCESS_COUNT]; 
struct Process unknown_processes[MAX_PROCESS_COUNT]; 
int ssp_process_count = 0; // current # of process 
int unknown_process_count = 0; 

/*  always be called once before a user makes any other call to your library. 
    You should initialize or setup anything you need here. */
void ssp_init() {
    for (int i = 0; i < MAX_PROCESS_COUNT; i++) {
        ssp_processes[i].pid = -1;   
        ssp_processes[i].name = NULL; 
        ssp_processes[i].status = 0; 
    }
    ssp_process_count = 0; 
    prctl(PR_SET_CHILD_SUBREAPER, 1);
}

/*  create a new process in this function, that new process should eventually call execvp(argv[0], argv) */
int ssp_create(char *const *argv, int fd0, int fd1, int fd2) {
    // find available slot 
    int ssp_id = -1;
    for (int i = 0; i < MAX_PROCESS_COUNT; i++) {
        if (ssp_processes[i].pid == -1) {
            ssp_id = i;
            break;
        }
    }

    if (ssp_id == -1) {
        perror("ERROR: no slot available for new process "); 
        return -1; 
    }

    // fork a new process
    long child_pid = fork();
    if (child_pid == -1) {
        perror("ERROR: forking failed "); 
        return -1;
    }

    // printf("ssp_create \n"); 

    if (child_pid == 0) { // child process
        // printf("in child \n"); 

        // printf("fd0 %d \n", fd0);         
        // printf("fd1 %d \n", fd1); 
        // printf("fd2 %d \n", fd2); 
        // set file descriptors 
        dup2(fd0, 0);
        dup2(fd1, 1);
        dup2(fd2, 2);
 
        
        DIR *new_dir; 
        new_dir = opendir("/proc/self/fd"); 

        if (new_dir) { 
            // fprintf(stderr, "ssp_create contains file"); 
            struct dirent *entry;
            while ((entry = readdir(new_dir))) {
                if (entry->d_type == DT_LNK) {
                    int fd = atoi(entry->d_name);
                    
                    if (fd != 0 && fd != 1 && fd != 2) {
                        close(fd);
                    }
                }
            }
            closedir(new_dir);
        }
        
        // execute the program 
        int status_code = execvp(argv[0], argv);

        if (status_code == -1) { // if execvp fails
            perror("ERROR: execvp failed "); 
            exit(errno);
        } 
    } else { // parent process
        ssp_processes[ssp_id].pid = child_pid;
        ssp_processes[ssp_id].name = strdup(argv[0]); // copy the argv[0] string 
        ssp_processes[ssp_id].status = -1; // process is running 

        ssp_process_count++;

        return ssp_id; 
    }

    perror("ERROR: ssp_create() failed ");  
    return -1; 
}

/*  return the current status of the process referred to by ssp_id without blocking. */
int ssp_get_status(int ssp_id) {
    // check if ssp_id process is still running
    if (ssp_processes[ssp_id].status == -1) { 
        int status; 
        long result = waitpid(ssp_processes[ssp_id].pid, &status, WNOHANG); 

        // printf("ssp_processes[ssp_id].pid = %ld", ssp_processes[ssp_id].pid); 
        // printf("ssp_get_status result = %d", (int)(result)); 

        if (result == ssp_processes[ssp_id].pid) { // process has exited 
            if (WIFEXITED(status)) { // process exited normally 
                ssp_processes[ssp_id].status = WEXITSTATUS(status); 
            } else if (WIFSIGNALED(status)) { // process exited with a signal 
                ssp_processes[ssp_id].status = WTERMSIG(status) + 128; 
            } else { 
                ssp_processes[ssp_id].status = -1; 
                perror("ERROR: process has other exit status, not normal "); 
            } 
        } 
    } else {
        // perror("ssp_processes[ssp_id].pid != -1 "); 
    }

    return ssp_processes[ssp_id].status;
}

/*  send a signal signum to the process referred to by ssp_id. 
    If the process is no longer running, you should not return an error and instead do nothing. */
void ssp_send_signal(int ssp_id, int signum) {     
    
    if (ssp_processes[ssp_id].status == -1) {
        // send the signal to the process 
        if (kill(ssp_processes[ssp_id].pid, signum) == -1) { 
            perror("ERROR: sending signal failed "); 
        }
    }
}

void ssp_unknown() {

    int status; 
    long pid; 

    while (true) {
        pid = waitpid(-1, &status, WNOHANG); 

        if (pid > 0) {

            // if pid not in process[], then it is unknown 
            int isUnknown = 1; 
            for (int i = 0; i < ssp_process_count; i++) {
                if (pid == ssp_processes[i].pid) {
                    isUnknown = 0; 
                    break; 
                }
            }

            // printf("isUnknown = %d \n", isUnknown); 

            if (isUnknown) {
                unknown_processes[unknown_process_count].pid = pid;
                unknown_processes[unknown_process_count].name = strdup("<unknown>");
                // printf("pid = %ld, name = %s \n", unknown_processes[unknown_process_count].pid, unknown_processes[unknown_process_count].name); 
                if (WIFEXITED(status)) {
                    unknown_processes[unknown_process_count].status = WEXITSTATUS(status); // process exited normally 
                } else if (WIFSIGNALED(status)) {
                    unknown_processes[unknown_process_count].status = WTERMSIG(status) + 128; // process exited with a signal 
                } else {
                    unknown_processes[unknown_process_count].status = -1;
                    perror("ERROR: process has other exit status, not normal "); 
                }
                unknown_process_count++;
            }

        } else if (pid < 0) {
            // perror("ERROR: ssp_unknown error out "); 
            
            if (errno == ECHILD) break; 
        } else {
            break; 
        }


    }
}

/*  block and only return when all processes created through ssp_create terminate. 
    As a sanity check, all processes should have a status between 0 and 255 after this call completes. */
void ssp_wait() { 
    int status; 
    long pid; 

    // update the status of exited process
    for (int i = 0; i < ssp_process_count; i++) {

        // wait for any child process to exit
        pid = waitpid(ssp_processes[i].pid, &status, 0); 
        // printf("ssp_processes[i].pid = %ld\n", ssp_processes[i].pid); 
        
        if (ssp_processes[i].pid == pid) {

            if (WIFEXITED(status)) {
                ssp_processes[i].status = WEXITSTATUS(status); // process exited normally 
                // printf("1\n"); 
                // printf("ssp_processes[i] status = %d\n", ssp_processes[i].status); 
            } else if (WIFSIGNALED(status)) {
                ssp_processes[i].status = WTERMSIG(status) + 128; // process exited with a signal 
                // printf("2\n"); 
                // printf("ssp_processes[i] status = %d\n", ssp_processes[i].status); 
            } else {
                ssp_processes[i].status = -1; 
                perror("ERROR: process has other exit status, not normal "); 
            }
        } 
    }


}

/*  non-blocking call that outputs the PID, name, and current status of every process created through ssp_create. */
void ssp_print() {
    
    ssp_unknown(); 

    int max_name_len; 
    // find width of longest process name 
    max_name_len = (int)strlen("CMD");

    if (unknown_process_count) {
        max_name_len = 9; 
    }

    for (int i = 0; i < MAX_PROCESS_COUNT; i++) {
        if (ssp_processes[i].pid != -1) {
            int name_len = strlen(ssp_processes[i].name);
            if (name_len > max_name_len) {
                max_name_len = name_len;
            }
        }
    }

    printf("%7s %-*s %s\n", "PID", max_name_len, "CMD", "STATUS"); // print header 

    for (int i = 0; i < MAX_PROCESS_COUNT; i++) {
        if (ssp_processes[i].pid != -1){  // print each process 
            printf("%7ld %-*s %d\n", ssp_processes[i].pid, max_name_len, ssp_processes[i].name, ssp_processes[i].status);
        }
    }

    // printf("unknown_process_count = %d\n", unknown_process_count); 
    
    for (int i = 0; i < unknown_process_count; i++) {
        printf("%7ld %-*s %d\n", unknown_processes[i].pid, max_name_len, unknown_processes[i].name, unknown_processes[i].status);
    }
}

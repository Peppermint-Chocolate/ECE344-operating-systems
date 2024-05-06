#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*  meson setup build  
    meson compile -C build
    build/tps 
    
    ps -eo pid:5,ucmd
    /proc 
    /proc/<pid>/status */

/*  PID CMD
    1 systemd
    2 kthreadd
    3 rcu_gp
    4 rcu_par_gp
    5 slub_flushwq
    6 netns
    7 kworker/0:0-cgroup_destroy
    8 kworker/0:0H-events_highpri */

void print_pid_name(long pid) {
    char pid_path[100]; 
    char line[100]; 
    char pid_name[100]; 
    FILE* pid_status; 

    // construct path to the status file for pid 
    snprintf(pid_path, sizeof(pid_path), "/proc/%ld/status", pid); 

    pid_status = fopen(pid_path, "r"); 

    if (! pid_status) {
        return; 
    }

    // read pid status line by line
    while (fgets(line, sizeof(line), pid_status)) {
        if (sscanf(line, "Name:\t%s", pid_name) != EOF) {
            printf("%5ld %s\n", pid, pid_name); 
            break; 
        }
    }

    fclose(pid_status); 
    return; 
}

int main() {
    printf("  PID CMD\n"); 

    DIR* proc = opendir("/proc");
    struct dirent* entry; 
    long pid; 

    // error out if opendir failed 
    if (proc == NULL) {
        perror("ERROR: opendir(/proc)"); 
        return 1; 
    }

    // read /proc dir and print pid & name 
    while ( (entry = readdir(proc)) != NULL) {
        
        if (atol(entry->d_name) != 0) {
            
            pid = atol(entry->d_name); 

            print_pid_name(pid); 

        }

    }

    closedir(proc); 


    return 0;
    
}

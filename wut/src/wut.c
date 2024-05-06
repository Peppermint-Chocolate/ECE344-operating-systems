#include "wut.h"

#include <assert.h> // assert
#include <errno.h> // errno
#include <stddef.h> // NULL
#include <stdio.h> // perror
#include <stdlib.h> // reallocarray
#include <sys/mman.h> // mmap, munmap
#include <sys/signal.h> // SIGSTKSZ
/* may also find sys/queue.h helpful, especially the TAILQ family of functions that implement a useful linked list */
#include <sys/queue.h> // TAILQ_* 
#include <ucontext.h> // getcontext, makecontext, setcontext, swapcontext
#include <valgrind/valgrind.h> // VALGRIND_STACK_REGISTER

/* meson compile -C build */
/* meson test --print-errorlogs -C build */
/* run each test case:  ./build/tests/main-thread-yields */

/* suggested to have one global variable that contains the id of the currently running thread, and a ready queue */

#define MAX_NUM_THREADS 128 

struct list_entry{
    int id; 
    TAILQ_ENTRY(list_entry) pointers; 
}; 
TAILQ_HEAD(list_head, list_entry); 
static struct list_head list_head; 

typedef struct {
    ucontext_t *context; 
    int id; 
    int status; 
    char* stack; 
} tcb_t; 

static int current_thread = 0; 
static int next_thread = 0; 
static int num_threads = 1; 
static int thread_to_wait; 
static int thread_blocked; 


struct list_entry* ready_queue = NULL;
// static tcb_t* threads[MAX_NUM_THREADS]; 
static tcb_t* threads = NULL; 
static tcb_t* exiting_thread; 

ucontext_t context;
ucontext_t* contextptr = &context;

void print_threads(void) {
    printf("Threads: "); 
    for (int i = 0; i <= num_threads; i++) {
        printf(" %d", threads[i].id); 
    }
    printf(" \n"); 
}

void print_list(void) {
    printf("List: "); 
    // struct list_entry* e; 
    TAILQ_FOREACH(ready_queue, &list_head, pointers) {
        printf(" %d", ready_queue->id); 
    }
    printf(" \n"); 
}

void print_list_last(void) {
    struct list_entry* e = TAILQ_LAST(&list_head, list_head); 

    printf("List last: %d \n", e->id); 
}

// // add a thread to the ready queue 
// static void enqueue_ready(int id) {
//     ready_queue[ready_queue_size] = id; 
//     ready_queue_size++; 
// }

// // remove a thread from the ready queue 
// static void dequeue_ready() {
//     if (ready_queue_size) {
//         for (int i = 0; i < ready_queue_size - 1; i++) {
//             ready_queue[i] = ready_queue[i + 1]; 
//         }
//         ready_queue_size--; 
//     }
// }

static void die(const char* message) {
    int err = errno;
    perror(message);
    exit(err);
}

static char* new_stack(void) {
    char* stack = mmap(
        NULL,
        SIGSTKSZ,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_ANONYMOUS | MAP_PRIVATE,
        -1,
        0
    );
    if (stack == MAP_FAILED) {
        die("mmap stack failed");
    }
    VALGRIND_STACK_REGISTER(stack, stack + SIGSTKSZ);
    return stack;
}

static void delete_stack(char* stack) {
    if (munmap(stack, SIGSTKSZ) == -1) {
        die("munmap stack failed");
    }
}

void (*run_function)(void); 

void new_run() {

    printf("new_run() called \n"); 

    // run_function(); 
    
    wut_exit(0); 
}

void manage_thread() {
    return; 
}

void exit_thread() {
    printf("----------------------------- exit_thread() called ----------------------------- \n"); 

    // wut_exit(0); 

    printf(" ----------------------------- wut_exit() completed ----------------------------- \n"); 

    while (! TAILQ_EMPTY(&list_head)) {
        wut_yield(); 
        wut_exit(0); 
    }
    wut_exit(0); 

    printf("list:  \n"); 
    print_list();  
    printf("thread:  \n");    
    print_threads(); 
    printf("current_thread: %d \n", current_thread);    
}

void wut_init() {

    printf("----------------------------- wut_init() called ----------------------------- \n"); 

    TAILQ_INIT(&list_head);
    threads = (tcb_t*)reallocarray(threads, 1, sizeof(tcb_t)); 
    // tcb_t* main_thread = (tcb_t*)malloc(sizeof(tcb_t)); 
    // getcontext(&main_thread->context); 

    threads[0].id = 0; 
    threads[0].status = -1; 
    threads[0].context = (ucontext_t*)malloc(sizeof(ucontext_t)); 
    getcontext(threads[0].context);
    threads[0].stack = threads[0].context->uc_stack.ss_sp;
    // main_thread->id = next_thread_id; 

    next_thread++; 
    // main_thread->status = 0; 
    // threads[num_threads] = main_thread; 
    // num_threads++; 
    // enqueue_ready(main_thread->id); 
    getcontext(contextptr);
    contextptr->uc_stack.ss_sp = new_stack();
    contextptr->uc_stack.ss_size = SIGSTKSZ;

    printf("threads after init: \n"); 
    print_threads(); 

    makecontext(contextptr, exit_thread, 0);

}

/* return id of current running thread */
int wut_id() {
    printf("wut_id() called \n"); 
    return current_thread; 
}


/* have all the ucontext stuff, have to make a context and tell it to execute the function */
int wut_create(void (*run)(void)) {
    printf("wut_create() called \n"); 
    if (num_threads >= MAX_NUM_THREADS) {
        return -1; 
    }
    // tcb_t* new_thread = (tcb_t*)malloc(sizeof(tcb_t)); 

    num_threads++; 

    threads = (tcb_t*)reallocarray(threads, num_threads, sizeof(tcb_t)); 
    // tcb_t* main_thread = (tcb_t*)malloc(sizeof(tcb_t)); 
    // getcontext(&main_thread->context); 

    int new_thread = num_threads - 1; 


    threads[new_thread].id = new_thread; 
    threads[new_thread].status = -1; 
    threads[new_thread].stack = new_stack();
    threads[new_thread].context = (ucontext_t*)malloc(sizeof(ucontext_t)); 
    getcontext(threads[0].context);
    // char* s = new_stack(); 
    threads[new_thread].context->uc_stack.ss_sp = threads[new_thread].stack;  
    threads[new_thread].context->uc_stack.ss_size = SIGSTKSZ;  
    threads[new_thread].context->uc_link = contextptr;  

    getcontext(threads[new_thread].context); 
    run_function = run; 
    makecontext(threads[new_thread].context, run, 0); 

    // new_thread->id = next_thread_id; 
    // next_thread_id++; 
    // new_thread->status = 0; 
    // threads[num_threads] = new_thread; 
    // num_threads++; 
    // enqueue_ready(new_thread->id); 

    ready_queue = (struct list_entry*)malloc(sizeof(struct list_entry)); 
    ready_queue->id = threads[new_thread].id; 
    TAILQ_INSERT_TAIL(&list_head, ready_queue, pointers);

    print_list(); 
    print_threads(); 

    return threads[new_thread].id; 
}

/* more like kill -9 but for thread, forcefully terminate a thread */
int wut_cancel(int id) {
    printf("wut_cancel() called \n"); 

    if (id == 0 || TAILQ_EMPTY(&list_head)) {
        return -1; 
    }
    tcb_t thread_to_cancel = threads[id]; 
    // if (thread_to_cancel == NULL) { 
    //     return -1; 
    // }

    print_list(); 

    TAILQ_FOREACH(ready_queue, &list_head, pointers){
        if (ready_queue->id == id) {
            TAILQ_REMOVE(&list_head, ready_queue, pointers);

            delete_stack(thread_to_cancel.stack);
            free(threads[ready_queue->id].context);

            thread_to_cancel.status = 128; // set status to cancelled 
            return 0;
        }
    }

    return -1; 
}

/* whenever a thread joins and it gets blocked, whenever the thread it is joining on  terminates, it get sent to the back */
/* this is the equivalence of creating a new thread and it would go to the back */
int wut_join(int id) {
    printf(" ----------------------------- wut_join() called ----------------------------- \n"); 

    if (id < 0 || id > num_threads) {
        printf("wut_join returns -1: id < 0 || id >= num_threads \n"); 
        return -1; 
    }

    thread_to_wait = id;
    // if (threads[thread_to_wait].status == -1) {
    //     printf("wut_join returns -1: threads[thread_to_wait_on].status == -1 \n"); 
    //     return -1; 
    // }

    int original_thread = current_thread; 

    if (thread_to_wait == current_thread) {
        printf("wut_join returns -1: thread_to_wait == current_thread \n"); 
        return -1; 
    }

    printf("list before:  \n"); 
    print_list();  
    printf("thread before:  \n");    
    print_threads(); 
    printf("current_thread: %d \n", current_thread);    

    tcb_t* current_tcb = &threads[current_thread];
    tcb_t* wait_tcb = &threads[thread_to_wait];

    thread_blocked = current_tcb->id; 
    current_tcb->status = -2;

    tcb_t* next_tcb = &threads[TAILQ_FIRST(&list_head)->id];
    int next_thread = next_tcb->id;

    TAILQ_REMOVE(&list_head,  TAILQ_FIRST(&list_head), pointers);

    current_thread = next_thread;

    swapcontext(threads[original_thread].context, threads[next_thread].context);

    int waited_thread_status = wait_tcb->status;
    // delete_stack(wait_tcb->stack);
    // wait_tcb->stack = NULL;

    // free(wait_tcb->context);
    // wait_tcb->context = NULL;
    // wait_tcb->status = -1;

    // int status = current_tcb->status; 
    // return status;

    printf("list after:  \n"); 
    print_list();  
    printf("thread after:  \n");    
    print_threads(); 

    return waited_thread_status;
}

/* stop the current thread from running, switch to the next one that is in the ready or waiting queue */
int wut_yield() {
    printf("wut_yield() called \n"); 

    printf("list before:  \n"); 
    print_list(); 

    if (TAILQ_EMPTY(&list_head)) {
        return -1; 
    } 
    
    int current_thread_archive = current_thread; 

    ready_queue = (struct list_entry*)malloc(sizeof(struct list_entry)); 
    ready_queue->id = current_thread; 

    TAILQ_INSERT_TAIL(&list_head, ready_queue, pointers); 
    next_thread = TAILQ_FIRST(&list_head)->id; 
    if (next_thread == current_thread) {
        return -1; 
    } 

    TAILQ_REMOVE(&list_head, TAILQ_FIRST(&list_head), pointers); 
    current_thread = next_thread; 

    swapcontext(threads[current_thread_archive].context, threads[current_thread].context); 

    printf("list after:  \n"); 
    print_list(); 
 
    return 0; 
}

void wut_exit(int status) {
    printf("----------------------- wut_exit() called ----------------------------- \n"); 

    // if (current_thread == 0) {
    //     exit(status);
    // }

    printf("list before:  \n"); 
    print_list();  
    printf("thread before:  \n");    
    print_threads(); 
    printf("current_thread: %d \n", current_thread);    

    status &= 0xFF;

    int current_thread_archive = current_thread;
    threads[current_thread].status = status;
    // exiting_thread->status = status; 

    if (threads[thread_to_wait].status >= 0 && thread_to_wait != thread_blocked){
        ready_queue = (struct list_entry*)malloc(sizeof(struct list_entry));
        ready_queue->id = threads[thread_blocked].id;

        TAILQ_INSERT_TAIL(&list_head, ready_queue, pointers);
        printf("list after TAILQ_INSERT_TAIL:  \n"); 
        print_list();  
        threads[thread_blocked].status=status;
        delete_stack(threads[thread_to_wait].stack);
        free(threads[thread_to_wait].context);
       
    }

    // delete_stack((char*)current_tcb->context.u5c_stack.ss_sp);
    // threads[current_thread] = NULL;
    printf("current_thread before swapcontext: %d \n", current_thread);    

    swapcontext(threads[current_thread_archive].context, threads[TAILQ_FIRST(&list_head)->id].context);

    
    // exit(0);
}

#ifndef GLOBALS
#define GLOBALS

#include "uthreads.h"
#include "threadID.h"
#include <map>
#include <list>
#include <signal.h>
#include <vector>
#include <sys/time.h>
#include <setjmp.h>
using namespace std;

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

//Thread states
typedef enum ThreadState {
    READY,
    RUNNING,
    BLOCKED
}ThreadState;
//Thread struct
typedef struct Thread{
    int tid;
    ThreadState state=READY;
    char* stack;
    thread_entry_point entry_point;
    sigjmp_buf env;
    int quantums=0;
    int sleep_until_quantums=0;
    bool is_blocked=false;
}Thread;


extern int QUANTUM_USECS;
extern int CURRENT_TID;
extern int TOTAL_QUANTUMS;
extern map<int, Thread*> all_threads;
extern list<int> blocked_queue;
extern list<int> ready_queue;
extern ThreadID threadIDGenerator;
extern Thread* zombie_thread;

void corruption_prevention_block();
void corruption_prevention_unblock();
void clear_zombie_thread();
int search_in_ready_queue(int tid);
int search_in_blocked_list(int tid);
void setTimer();
address_t translate_address(address_t addr);
void timer_handler(int sig);
void handleSignalTiming() ;
int switch_threads();
#endif
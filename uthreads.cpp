#include "uthreads.h"
#include "uthreadsHelper.h"
#include "threadID.h"
#include <iostream>
#include <list>
#include <map>
#include <signal.h>
#include <vector>
#include <sys/time.h>
using namespace std;
typedef void (*thread_entry_point)(void);

// Global variables: Shared state for timing and execution control
int QUANTUM_USECS = 0;
int CURRENT_TID = 0;
int TOTAL_QUANTUMS = 0;

// Thread management structures
map<int, Thread*> all_threads;
list<int> blocked_queue;
list<int> ready_queue;
ThreadID threadIDGenerator = ThreadID();

/**
 * Holds a self-terminated thread's pointer until the next context switch cleans it up.
 * Necessary because we cannot delete a thread's stack while currently executing on it.
 */
Thread* zombie_thread = nullptr;

/**
 * @brief Performs a context switch to the next thread in the READY queue.
 * * This function handles the core scheduling logic:
 * 1. Increments the global quantum counter.
 * 2. Selects the next thread to run (Round Robin).
 * 3. Manages sleeping threads and wakes them up if their time has passed.
 * 4. Triggers the context switch using siglongjmp.
 * * @return Does not return on success (jumps to the next thread's execution point).
 */
int switch_threads() {
    corruption_prevention_block();
    TOTAL_QUANTUMS++;
    Thread* next_thread;
    if (ready_queue.empty()) {//incase there are no other threads we"ll just go to the main thread
        CURRENT_TID = 0;
        next_thread = all_threads[0];
    }
    else{
        int next_thread_tid = ready_queue.front();
        ready_queue.pop_front();
        CURRENT_TID = next_thread_tid;
        next_thread=all_threads[next_thread_tid];
    }
    next_thread->state = RUNNING;
    next_thread->quantums++;
    for(auto it = blocked_queue.begin(); it != blocked_queue.end();){
        int tid= *it;
        bool is_erased=false;
        Thread* thread=all_threads[tid];
        int sleep_quantums = thread->sleep_until_quantums;
        
        if(sleep_quantums<=TOTAL_QUANTUMS && sleep_quantums>0){
            thread->sleep_until_quantums=0;
            if(!thread->is_blocked){//if not blocked , will be realesed here
                all_threads[tid]->state=READY;
                ready_queue.push_back(tid);
                it=blocked_queue.erase(it);
                is_erased=true;
            }
            //else , will be realesed by resume
        }
        if (!is_erased){it++;}
    }
    setTimer();
    siglongjmp(next_thread->env, 1);
}
/**
 * @brief initializes the thread library.
 *
 * Once this function returns, the main thread (tid == 0) will be
 *  set as RUNNING. There is no need to 
 * provide an entry_point or to create a stack for the main thread
 *  - it will be using the "regular" stack and PC.
 * You may assume that this function is called before any other
 *  thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantum in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 * 
 * @return On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs) {
    if (quantum_usecs <= 0) {
        cerr << "thread library error: " << "quantum_usecs must be positive" << std::endl;
        return -1;
    }
    Thread* main_thread;
    try {
        main_thread = new Thread();
    }
    catch (const std::bad_alloc& e) {
        // memory allocation is a system-level failure → "system error" + exit(1)
        // (not a library error, not return -1)
        cerr << "system error: " << "failed to allocate memory for main thread" << std::endl;
        exit(1);
    }
    main_thread->tid = 0;
    main_thread->state = RUNNING;
    main_thread->stack = nullptr; // main uses the process's original stack, no allocation needed.
                                  // nullptr is important so that delete[] in uthread_terminate(0)'s
                                  // cleanup loop is safe — delete[] nullptr is well-defined in C++.
    main_thread->entry_point = nullptr;
    main_thread->quantums = 1;  // main starts already running, counts as 1 quantum
    TOTAL_QUANTUMS = 1;         // same reason
    all_threads[0] = main_thread;
    QUANTUM_USECS = quantum_usecs;
    handleSignalTiming();
    return 0;
}


/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid() {
    return CURRENT_TID;
}


/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 * It is an error to call this function with a null entry_point.
 * @return On success, return the ID of the created thread. On failure, return -1.
*/
int uthread_spawn(thread_entry_point entry_point) {
    corruption_prevention_block();
    if (entry_point == nullptr) {
        cerr << "thread library error: " << "entry_point cannot be null" << std::endl;
        corruption_prevention_unblock();
        return -1;
    }
    int new_tid = threadIDGenerator.getID();
    if (new_tid == -1) {
        corruption_prevention_unblock();
        cerr << "thread library error: " << "reached maximum number of threads" << std::endl;
        return -1;
    }
    Thread* new_thread;
    try {
        new_thread = new Thread();
    }
    catch (const std::bad_alloc& e) {
        cerr << "system error: " << "failed to allocate memory for thread" << std::endl;
        exit(1);
    }
    new_thread->tid = new_tid;
    new_thread->entry_point = entry_point;
    try {
        new_thread->stack = new char[STACK_SIZE];
    }
    catch (const std::bad_alloc& e) {
        cerr << "system error: " << "failed to allocate memory for thread stack" << std::endl;
        delete new_thread; // clean up the Thread object we already allocated before exiting
        exit(1);
    }
    address_t sp = (address_t) new_thread->stack + STACK_SIZE - sizeof(address_t);
    address_t pc = (address_t) entry_point;

    // sigsetjmp here does NOT save a meaningful CPU state for the new thread.
    // We only call it to properly initialize the env buffer's internal structure.
    // We then immediately overwrite SP and PC with the new thread's stack and entry point.
    // When siglongjmp is later called on this env (from switch_threads),
    // the CPU will jump directly into entry_point() with the new stack — 
    // no if/else needed, siglongjmp IS the activation.
    sigsetjmp(new_thread->env, 1);
    (new_thread->env->__jmpbuf)[JB_SP] = translate_address(sp);
    (new_thread->env->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&new_thread->env->__saved_mask);

    all_threads[new_tid] = new_thread;
    ready_queue.push_back(new_tid);
    corruption_prevention_unblock();
    return new_tid;
}


/**
 * @brief Terminates the thread with ID tid and deletes
 *  it from all relevant control structures.
 *
 * All the resources allocated by the library for this
 *  thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread
 *  (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * 
 * @return The function returns 0 if the thread was
 *  successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
*/
int uthread_terminate(int tid) {
    corruption_prevention_block();
    clear_zombie_thread();
    if (!tid) {
        // terminating main = terminating the whole process.
        // iterate and free everything first to avoid memory leaks.
        // delete[] nullptr (main's stack) is safe in C++, no special case needed.
        for (auto& pair : all_threads) {
            if (pair.first == CURRENT_TID && pair.first != 0) continue;
            delete[] pair.second->stack;
            delete pair.second;
        }
        corruption_prevention_unblock();
        exit(0);
    }
    if (tid < 0 || all_threads.find(tid) == all_threads.end() ) {
        cerr << "thread library error: " << "thread with ID " << tid << " does not exist" << std::endl;
        corruption_prevention_unblock();
        return -1;
    }
    Thread* thread_to_terminate = all_threads[tid];
    all_threads.erase(tid);
    threadIDGenerator.releaseID(tid); // return ID to the pool so it can be reused
                                      // (spec: new thread gets smallest available ID)
    if(search_in_ready_queue(tid) ==1){
        ready_queue.remove(tid);
    }
    if (search_in_blocked_list(tid) == 1){//cant be on both ready and blocked queues at the same time
        blocked_queue.remove(tid);
    }
    if (tid == CURRENT_TID) {
        // self-termination: we can't delete the thread's memory yet because
        // we're still executing on its stack right now. Store it as a zombie
        // and let the next thread clean it up via clear_zombie_thread().
        zombie_thread = thread_to_terminate;
        switch_threads(); // switch away — this does not return
    }
    else {
        // external termination: safe to delete immediately since we're not on this thread's stack
        delete thread_to_terminate;
    }
    corruption_prevention_unblock();
    return 0;
}


/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error.
 *  In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself,
 *  a scheduling decision should be made. Blocking a thread in
 * BLOCKED state has no effect and is *not* considered an error.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_block(int tid) {
    corruption_prevention_block();
    if (all_threads.find(tid) == all_threads.end()) {
        cerr << "thread library error: " << "thread with ID " << tid << " does not exist" << std::endl;
        corruption_prevention_unblock();
        return -1;
    }
    if (tid == 0) {
        cerr << "thread library error: " << "cannot block the main thread" << std::endl;
        corruption_prevention_unblock();
        return -1;  
    }
    Thread* thread_to_block = all_threads[tid];
    thread_to_block->is_blocked=true;
    if(thread_to_block->state == BLOCKED){
        corruption_prevention_unblock();
        return 0;
    }
    thread_to_block->state = BLOCKED;
    bool is_self_blocking = (tid == CURRENT_TID);
    blocked_queue.push_back(tid);
    if (is_self_blocking){
        if (sigsetjmp(thread_to_block->env, 1)==0){
            switch_threads();
        }
        else{
            clear_zombie_thread();
        }
    }
    else{
        for (auto it = ready_queue.begin(); it != ready_queue.end(); ++it) {
            if (*it == tid) {
                ready_queue.erase(it);
                break;
            }
        }
    }
    corruption_prevention_unblock();
    return 0;
}


/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY state.
 *
 * Resuming a thread in a RUNNING or READY state has no effect and
 *  is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 * When a thread transition to the READY state it is placed at the
 *  end of the READY queue.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_resume(int tid) {
    corruption_prevention_block();
    if (all_threads.find(tid)==all_threads.end()){
        cerr << "thread library error: " << "thread with ID " << tid << " does not exist" << std::endl;
        corruption_prevention_unblock();
        return -1;
    }
    Thread* thread_to_resume = all_threads[tid];
    if (thread_to_resume->state==READY || thread_to_resume->state==RUNNING){
        corruption_prevention_unblock();
        return 0;
    }
    thread_to_resume->is_blocked=false;
    if (thread_to_resume->sleep_until_quantums>0){
        corruption_prevention_unblock();
        return 0;
    }
    thread_to_resume->state = READY;
    //else can return safely to ready queue
    ready_queue.push_back(tid);
    blocked_queue.remove(tid);
    corruption_prevention_unblock();
    return 0;
}


/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a
 *  scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY queue.
 * If the thread which was just RUNNING should also be added to the READY queue, or
 *  if multiple threads wake up 
 * at the same time, the order in which they're added to the end of the READY queue doesn't matter.
 * The number of quantums refers to the number of times a new quantum starts,
 *  regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to uthread_sleep isn’t counted.
 * A call with num_quantums == 0 will immediately stop the thread
 *  and move it to the back of the execution queue.
 * 
 * It is considered an error if the main thread (tid == 0) calls this
 *  function with num_quantums != 0.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_sleep(int num_quantums) {
    corruption_prevention_block();
    if (CURRENT_TID==0 && num_quantums!=0){
        cerr<<"thread library error: " << "main thread cannot sleep for more than 0 quantums" << std::endl;
        corruption_prevention_unblock();
        return -1;
    }
    if (num_quantums < 0) {  
        cerr << "thread library error: cannot sleep for negative number of quantums" << endl;
        corruption_prevention_unblock();
        return -1;
    }
    if(sigsetjmp(all_threads[CURRENT_TID]->env, 1)==0){
        if (num_quantums == 0) {
            all_threads[CURRENT_TID]->state = READY;
            ready_queue.push_back(CURRENT_TID);
        }
        else{
            all_threads[CURRENT_TID]->state = BLOCKED;
            blocked_queue.push_back(CURRENT_TID);
            all_threads[CURRENT_TID]->sleep_until_quantums = num_quantums+TOTAL_QUANTUMS+1;
        }
        switch_threads();
    }
    else{
        clear_zombie_thread();
    }
    corruption_prevention_unblock();
    return 0;
}



/**
 * @brief Returns the total number of quantums since the library was initialized,
 *  including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 *  should be increased by 1.
 *
 * @return The total number of quantums.
 */
int uthread_get_total_quantums() {
    return TOTAL_QUANTUMS;
}


/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING state.
 *
 * On the first time a thread runs, the function should return 1.
 *  Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING
 *  state when this function is called, include
 * also the current quantum). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
 */
int uthread_get_quantums(int tid) {
    corruption_prevention_block();
    if (all_threads.find(tid) == all_threads.end()){
        cerr << "thread library error: " << "thread with ID " << tid << " does not exist" << std::endl; 
        corruption_prevention_unblock();
        return -1;
    }
    corruption_prevention_unblock();
    return all_threads[tid]->quantums;
}

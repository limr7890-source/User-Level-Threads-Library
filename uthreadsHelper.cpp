#include "uthreadsHelper.h"
#include <iostream>

using namespace std;

/**
 * @brief Translates a logical address to a physical one for setjmp/longjmp.
 * This is a security measure in glibc that mangles pointers in jmp_buf.
 * @param addr The address to translate.
 * @return The mangled address.
 */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
        "rol    $0x11,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

/**
 * @brief Searches for a specific thread ID in the READY queue.
 * @param tid The thread ID to search for.
 * @return 1 if found, 0 otherwise.
 */
int search_in_ready_queue(int tid){
    for (int ready_tid : ready_queue) {
        if (ready_tid == tid) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Searches for a specific thread ID in the BLOCKED queue.
 * @param tid The thread ID to search for.
 * @return 1 if found, 0 otherwise.
 */
int search_in_blocked_list(int tid){
    for (int blocked_tid : blocked_queue) {
        if (blocked_tid == tid) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Deallocates the resources of a self-terminated thread.
 * This is called during a context switch to ensure we don't delete
 * a thread's stack while we are still executing on it.
 */
void clear_zombie_thread(){
    if(zombie_thread != nullptr){
        delete[] zombie_thread->stack;
        delete zombie_thread;
        zombie_thread = nullptr;
    }
}

/**
 * @brief Blocks the SIGVTALRM signal to prevent race conditions.
 * Used to ensure atomic operations during critical sections.
 */
void corruption_prevention_block(){
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    if (sigprocmask(SIG_BLOCK, &sigset, nullptr) < 0){
        cerr << "system error: sigprocmask block error." << endl;
        exit(1);
    }
}

/**
 * @brief Unblocks the SIGVTALRM signal.
 * Restores normal scheduling after a critical section.
 */
void corruption_prevention_unblock(){
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    if (sigprocmask(SIG_UNBLOCK, &sigset, nullptr) < 0){
        cerr << "system error: sigprocmask unblock error." << endl;
        exit(1);
    }
}

/**
 * @brief The signal handler for virtual timer interrupts.
 * Initiates a context switch when the current thread's quantum expires.
 * @param sig The signal number (SIGVTALRM).
 */
void timer_handler(int sig) {
    corruption_prevention_block();
    all_threads[CURRENT_TID]->state = READY;
    ready_queue.push_back(CURRENT_TID);

    // Save current state and switch to the next thread
    if (sigsetjmp(all_threads[CURRENT_TID]->env, 1) == 0) {
        switch_threads();
    }
    else {
        // Returned here from longjmp - clean up and continue
        clear_zombie_thread();
        corruption_prevention_unblock();
    }
}

/**
 * @brief Configures the virtual timer (ITIMER_VIRTUAL).
 * Sets the timer to trigger a SIGVTALRM after QUANTUM_USECS.
 */
void setTimer(){
    struct itimerval timer;
    timer.it_value.tv_sec = QUANTUM_USECS / 1000000;
    timer.it_value.tv_usec = QUANTUM_USECS % 1000000;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;

    if (setitimer(ITIMER_VIRTUAL, &timer, nullptr)) {
        cerr << "system error: setitimer error." << endl;
        exit(1);
    }
}

/**
 * @brief Sets up the signal action for SIGVTALRM and starts the timer.
 * Connects the timer_handler to the virtual alarm signal.
 */
void handleSignalTiming() {
    struct sigaction sa;
    sa.sa_handler = &timer_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGVTALRM, &sa, nullptr) < 0) {
        cerr << "system error: sigaction error." << endl;
        exit(1);
    }
    setTimer();
}
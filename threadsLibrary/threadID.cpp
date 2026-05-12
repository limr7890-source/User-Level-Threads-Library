#include "uthreads.h"
#include "threadID.h"
#include <set>

/**
 * @brief Constructor for the ThreadID class.
 * Initializes the pool of available thread IDs from 1 up to MAX_THREAD_NUM - 1.
 * TID 0 is reserved for the main thread.
 */
ThreadID::ThreadID()  {
    for (int i = 1; i < MAX_THREAD_NUM; i++) {
        availableIDs.insert(i);
    }
}

/**
 * @brief Retrieves the smallest available thread ID from the pool.
 * @return The smallest available ID (int), or -1 if the pool is empty (max threads reached).
 */
int ThreadID::getID() {
    if (availableIDs.empty()) {
        return -1; // No available IDs in the pool
    }
    // Using a set ensures we always fetch the smallest available ID efficiently
    int id = *availableIDs.begin();
    availableIDs.erase(availableIDs.begin());
    return id;
}

/**
 * @brief Returns a thread ID to the pool so it can be reused by new threads.
 * @param id The ID to be released.
 */
void ThreadID::releaseID(int id) {
    // Validate that the ID is within the valid range for non-main threads
    if (id > 0 && id < MAX_THREAD_NUM) {
        availableIDs.insert(id);
    }
}
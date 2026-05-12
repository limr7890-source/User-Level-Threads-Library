# 🧵 User-Level Threads Library (uthreads)

### What is this project?
This is a high-performance, systems-level library developed in C++ that manages user-level threads within a single process. By bypassing standard OS thread management, the library implements a custom **Round-Robin scheduler**, providing full control over thread execution, state management, and synchronization.

### Key Features & Implementation
* **Custom Scheduling:** Implements a Round-Robin scheduling algorithm to ensure fair execution time across all active threads.
* **Context Switching:** Utilizes low-level system calls (`sigsetjmp` and `siglongjmp`) to manually save and restore CPU states, enabling seamless transitions between threads.
* **Signal Handling & Safety:** Uses virtual timers (`SIGVTALRM`) to trigger quantum expiration and incorporates atomic signal blocking to prevent race conditions during critical sections.
* **Memory & Resource Management:** Features a dedicated "Zombie" cleanup mechanism to safely deallocate thread stacks and resources after self-termination.
* **Efficient ID Handling:** Manages a pool of unique thread IDs using a prioritized `std::set` to ensure new threads always receive the smallest available ID.

### Technical Deep Dive
* **Language:** C++.
* **Concepts:** Multi-threading, Operating Systems internals, Signal Handling, Context Switching, Memory Management.
* **Architecture:** Modular design with separate handlers for thread ID generation and internal helper utilities for cleaner code and maintainability.

### How it Works
1. **Initialization:** The library initializes with a user-defined quantum (in microseconds) and sets up the main thread (TID 0).
2. **Spawning:** New threads are allocated a dedicated stack and added to the READY queue.
3. **Execution:** The virtual timer triggers a signal handler, which saves the current thread's state and selects the next one to run.
4. **Termination:** Threads can terminate themselves or be terminated externally, with the library ensuring all allocated memory is released correctly.

# Project 10: Calculation of Catalan Numbers in a Multithreading System

## Course
COP 6611 — Operating Systems

## Description
This program computes Catalan numbers C(0) through C(N) using POSIX threads
(Pthreads). Each Catalan number is computed by a dedicated thread using the
recurrence relation:

    C(0) = 1
    C(n) = Σ C(i) * C(n-1-i)  for i = 0 to n-1

Synchronization is achieved using:
- A **mutex lock** (`pthread_mutex_t`) to protect the shared results array
- A **condition variable** (`pthread_cond_t`) so that thread computing C(n)
  waits until all prerequisite values C(0)..C(n-1) are available

After multithreaded computation, the program verifies results against a
single-threaded iterative computation.

## Files
| File         | Description                          |
|-------------|--------------------------------------|
| `catalan.c`  | Main source code                     |
| `Makefile`   | Build and run instructions           |
| `input.txt`  | Input file (single integer N)        |
| `README.md`  | This file                            |

## Build & Run

### Compile
```bash
make
```

### Run
```bash
./catalan input.txt
```
or
```bash
make run
```

### Clean
```bash
make clean
```

## Input Format
The input file should contain a single non-negative integer N on the first line.
The program will compute Catalan numbers C(0) through C(N).

Example `input.txt`:
```
20
```

## Output
The program prints:
1. Each thread's computation as it completes
2. A summary table comparing multithreaded results with iterative verification

## Limitations
- Values of `unsigned long long` overflow for C(n) where n > ~33
- For larger values, a big-integer library would be needed

## OS Concepts Demonstrated
- **Multithreading** via `pthread_create` / `pthread_join`
- **Mutual exclusion** via `pthread_mutex_lock` / `pthread_mutex_unlock`
- **Condition variables** via `pthread_cond_wait` / `pthread_cond_broadcast`
- **Shared memory** between threads (global arrays)
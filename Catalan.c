/*
 * =============================================================================
 *  Project 10: Calculation of Catalan Numbers in a Multithreading System
 *  Course:     COP 6611 — Operating Systems
 *  
 *  Description:
 *      Computes Catalan numbers C(0) through C(N) using POSIX threads.
 *      Each thread computes one Catalan number using the recurrence relation:
 *          C(n) = sum_{i=0}^{n-1} C(i) * C(n-1-i),  with C(0) = 1
 *      
 *      Threads are dispatched in order and use a shared results array 
 *      protected by a mutex lock and condition variable to ensure that
 *      thread computing C(n) waits until C(0)..C(n-1) are all available.
 *
 *  Compilation:
 *      make            (or)    gcc -Wall -pthread -o catalan catalan.c
 *
 *  Usage:
 *      ./catalan <input_file>
 *      (input_file contains a single integer N on line 1)
 *
 * 
 *  Date:     April 2025
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

/* ---------------------------------------------------------------------------
 *  Global shared data
 * --------------------------------------------------------------------------- */

static unsigned long long *catalan;      /* Shared array: catalan[i] = C(i)   */
static int                *computed;     /* Flag array: computed[i] = 1 if done*/
static int                 max_n;        /* We compute C(0) .. C(max_n)        */

static pthread_mutex_t     mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t      cond    = PTHREAD_COND_INITIALIZER;

/* ---------------------------------------------------------------------------
 *  Thread argument structure
 * --------------------------------------------------------------------------- */
typedef struct {
    int n;          /* This thread computes C(n) */
    int thread_id;  /* Logical thread identifier */
} thread_arg_t;

/* ---------------------------------------------------------------------------
 *  Thread function: compute the n-th Catalan number
 *
 *  Uses the recurrence:
 *      C(0) = 1
 *      C(n) = sum_{i=0}^{n-1} C(i) * C(n-1-i)
 *
 *  The thread waits (via condition variable) until all prerequisite 
 *  values C(0)..C(n-1) have been computed by other threads.
 * --------------------------------------------------------------------------- */
static void *compute_catalan(void *arg)
{
    thread_arg_t *ta = (thread_arg_t *)arg;
    int n  = ta->n;
    int id = ta->thread_id;

    /* ---- Base case ---- */
    if (n == 0) {
        pthread_mutex_lock(&mutex);
        catalan[0]  = 1;
        computed[0] = 1;
        printf("[Thread %2d]  C(%d) = %llu\n", id, n, catalan[0]);
        pthread_cond_broadcast(&cond);   /* Wake up waiting threads */
        pthread_mutex_unlock(&mutex);
        free(ta);
        return NULL;
    }

    /* ---- Wait until all C(0)..C(n-1) are available ---- */
    pthread_mutex_lock(&mutex);
    {
        int all_ready = 0;
        while (!all_ready) {
            all_ready = 1;
            for (int i = 0; i < n; i++) {
                if (!computed[i]) {
                    all_ready = 0;
                    break;
                }
            }
            if (!all_ready) {
                /* Release mutex and sleep until signalled */
                pthread_cond_wait(&cond, &mutex);
            }
        }
    }
    pthread_mutex_unlock(&mutex);

    /* ---- Compute C(n) using the recurrence (read-only on prerequisites) ---- */
    unsigned long long result = 0;
    for (int i = 0; i < n; i++) {
        result += catalan[i] * catalan[n - 1 - i];
    }

    /* ---- Store result and signal other threads ---- */
    pthread_mutex_lock(&mutex);
    catalan[n]  = result;
    computed[n] = 1;
    printf("[Thread %2d]  C(%d) = %llu\n", id, n, catalan[n]);
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);

    free(ta);
    return NULL;
}

/* ---------------------------------------------------------------------------
 *  Verification: iterative (single-threaded) Catalan computation
 *  Used to verify the multithreaded results.
 * --------------------------------------------------------------------------- */
static unsigned long long catalan_iterative(int n)
{
    if (n <= 1) return 1;
    unsigned long long *c = calloc((size_t)(n + 1), sizeof(unsigned long long));
    if (!c) { perror("calloc"); exit(EXIT_FAILURE); }

    c[0] = c[1] = 1;
    for (int i = 2; i <= n; i++) {
        c[i] = 0;
        for (int j = 0; j < i; j++) {
            c[i] += c[j] * c[i - 1 - j];
        }
    }
    unsigned long long result = c[n];
    free(c);
    return result;
}

/* ---------------------------------------------------------------------------
 *  Main
 * --------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    /* ---- Parse command-line arguments ---- */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        fprintf(stderr, "  input_file: text file with a single integer N on line 1\n");
        return EXIT_FAILURE;
    }

    /* ---- Read N from input file ---- */
    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }

    if (fscanf(fp, "%d", &max_n) != 1 || max_n < 0) {
        fprintf(stderr, "Error: input file must contain a non-negative integer N.\n");
        fclose(fp);
        return EXIT_FAILURE;
    }
    fclose(fp);

    printf("============================================================\n");
    printf("  Catalan Numbers — Multithreaded Computation (Pthreads)\n");
    printf("  Computing C(0) through C(%d)\n", max_n);
    printf("============================================================\n\n");

    /* ---- Allocate shared arrays ---- */
    catalan  = calloc((size_t)(max_n + 1), sizeof(unsigned long long));
    computed = calloc((size_t)(max_n + 1), sizeof(int));
    if (!catalan || !computed) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    /* ---- Create one thread per Catalan number ---- */
    int num_threads = max_n + 1;
    pthread_t *threads = malloc((size_t)num_threads * sizeof(pthread_t));
    if (!threads) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    printf("--- Launching %d threads ---\n\n", num_threads);

    for (int i = 0; i < num_threads; i++) {
        thread_arg_t *ta = malloc(sizeof(thread_arg_t));
        if (!ta) { perror("malloc"); return EXIT_FAILURE; }
        ta->n         = i;
        ta->thread_id = i;

        int rc = pthread_create(&threads[i], NULL, compute_catalan, (void *)ta);
        if (rc != 0) {
            fprintf(stderr, "Error: pthread_create failed for thread %d: %s\n",
                    i, strerror(rc));
            return EXIT_FAILURE;
        }
    }

    /* ---- Join all threads ---- */
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    /* ---- Print summary table ---- */
    printf("\n============================================================\n");
    printf("  RESULTS SUMMARY\n");
    printf("============================================================\n");
    printf("  %5s  %22s  %22s  %s\n", "n", "C(n) [threaded]", "C(n) [iterative]", "Match?");
    printf("  %5s  %22s  %22s  %s\n", "-----", "----------------------",
           "----------------------", "------");

    int all_match = 1;
    for (int i = 0; i <= max_n; i++) {
        unsigned long long verify = catalan_iterative(i);
        const char *status = (catalan[i] == verify) ? "  OK" : "  FAIL";
        if (catalan[i] != verify) all_match = 0;
        printf("  %5d  %22llu  %22llu  %s\n", i, catalan[i], verify, status);
    }

    printf("\n  Verification: %s\n",
           all_match ? "ALL RESULTS MATCH ✓" : "MISMATCH DETECTED ✗");
    printf("============================================================\n");

    /* ---- Cleanup ---- */
    free(catalan);
    free(computed);
    free(threads);
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);

    return EXIT_SUCCESS;
}
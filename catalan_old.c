/*
 * =============================================================================
 *  Project 10: Calculation of Catalan Numbers in a Multithreading System
 *  Course:     COP 6611 — Operating Systems
 *
 *  Description:
 *      Computes Catalan numbers C(0) through C(N) using POSIX threads.
 *      For each C(n), the program spawns n threads — one per product term
 *      C(i) * C(n-1-i) — which run in parallel and accumulate into a
 *      shared sum protected by a mutex lock.
 *
 *  Approach (matches proposal):
 *      C(n) = sum_{i=0}^{n-1} C(i) * C(n-1-i)
 *      For each n, spawn n threads. Thread i computes C(i)*C(n-1-i)
 *      and adds the result to a shared accumulator under mutex protection.
 *      The parent waits for all threads to finish, then stores C(n).
 *
 *  Compilation:
 *      make          (or)    gcc -Wall -pthread -o catalan catalan.c
 *
 *  Usage:
 *      ./catalan <input_file>
 *      (input_file contains a single integer N on line 1)
 *
 *  Authors:  Ariel Gomez Garcia, Yoel Polanco
 *  Date:     April 2025
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* ---------------------------------------------------------------------------
 *  Global shared data
 * --------------------------------------------------------------------------- */

static unsigned long long *catalan;   /* Results array: catalan[i] = C(i)      */
static int                 max_n;     /* We compute C(0) .. C(max_n)           */

/* ---------------------------------------------------------------------------
 *  Per-Catalan-number shared accumulator
 *  Each C(n) computation has its own mutex and running sum.
 * --------------------------------------------------------------------------- */
static unsigned long long  shared_sum;        /* Accumulator for current C(n)  */
static pthread_mutex_t     sum_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ---------------------------------------------------------------------------
 *  Thread argument: which product term to compute
 * --------------------------------------------------------------------------- */
typedef struct {
    int n;           /* We are computing C(n)                                  */
    int i;           /* This thread computes C(i) * C(n-1-i)                   */
    int thread_id;   /* Logical thread ID (for printing)                       */
} term_arg_t;

/* ---------------------------------------------------------------------------
 *  Thread function: compute one product term C(i) * C(n-1-i)
 *  and add it to the shared accumulator under mutex protection.
 * --------------------------------------------------------------------------- */
static void *compute_term(void *arg)
{
    term_arg_t *ta = (term_arg_t *)arg;
    int n = ta->n;
    int i = ta->i;

    /* Compute the product (read-only access to previously computed values) */
    unsigned long long product = catalan[i] * catalan[n - 1 - i];

    /* Lock mutex, update shared sum, unlock */
    pthread_mutex_lock(&sum_mutex);
    shared_sum += product;
    printf("    [Thread %2d]  C(%d)*C(%d) = %llu * %llu = %llu  |  running sum = %llu\n",
           ta->thread_id, i, n - 1 - i, catalan[i], catalan[n - 1 - i],
           product, shared_sum);
    pthread_mutex_unlock(&sum_mutex);

    free(ta);
    return NULL;
}

/* ---------------------------------------------------------------------------
 *  Compute C(n) by spawning n threads (one per product term)
 * --------------------------------------------------------------------------- */
static void compute_catalan_parallel(int n)
{
    if (n == 0) {
        catalan[0] = 1;
        printf("  C(0) = 1  (base case)\n");
        return;
    }

    /* Reset shared accumulator */
    shared_sum = 0;

    printf("  Computing C(%d) — spawning %d thread(s):\n", n, n);

    /* Allocate thread handles */
    pthread_t *threads = malloc((size_t)n * sizeof(pthread_t));
    if (!threads) { perror("malloc"); exit(EXIT_FAILURE); }

    /* Spawn one thread per product term */
    for (int i = 0; i < n; i++) {
        term_arg_t *ta = malloc(sizeof(term_arg_t));
        if (!ta) { perror("malloc"); exit(EXIT_FAILURE); }

        ta->n         = n;
        ta->i         = i;
        ta->thread_id = i;

        int rc = pthread_create(&threads[i], NULL, compute_term, (void *)ta);
        if (rc != 0) {
            fprintf(stderr, "Error: pthread_create failed: %s\n", strerror(rc));
            exit(EXIT_FAILURE);
        }
    }

    /* Wait for all threads to finish */
    for (int i = 0; i < n; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Store the result */
    catalan[n] = shared_sum;
    printf("  C(%d) = %llu\n\n", n, catalan[n]);

    free(threads);
}

/* ---------------------------------------------------------------------------
 *  Baseline: single-threaded iterative computation (for comparison)
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
 *  Timing helper: returns current time in seconds (high resolution)
 * --------------------------------------------------------------------------- */
static double get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---------------------------------------------------------------------------
 *  Main
 * --------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    /* ---- Parse command-line arguments ---- */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        fprintf(stderr, "  input_file: text file with a single integer N\n");
        return EXIT_FAILURE;
    }

    /* ---- Read N from input file ---- */
    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open '%s': %s\n", argv[1], strerror(errno));
        return EXIT_FAILURE;
    }

    if (fscanf(fp, "%d", &max_n) != 1 || max_n < 0) {
        fprintf(stderr, "Error: input file must contain a non-negative integer.\n");
        fclose(fp);
        return EXIT_FAILURE;
    }
    fclose(fp);

    printf("============================================================\n");
    printf("  Catalan Numbers — Multithreaded Computation (Pthreads)\n");
    printf("  Computing C(0) through C(%d)\n", max_n);
    printf("============================================================\n\n");

    /* ---- Allocate results array ---- */
    catalan = calloc((size_t)(max_n + 1), sizeof(unsigned long long));
    if (!catalan) { perror("calloc"); return EXIT_FAILURE; }

    /* ================================================================
     *  MULTITHREADED COMPUTATION
     * ================================================================ */
    printf("--- Multithreaded Computation ---\n\n");
    double mt_start = get_time();

    for (int n = 0; n <= max_n; n++) {
        compute_catalan_parallel(n);
    }

    double mt_end = get_time();
    double mt_time = mt_end - mt_start;

    /* ================================================================
     *  SINGLE-THREADED BASELINE (for comparison)
     * ================================================================ */
    printf("--- Single-Threaded Baseline ---\n\n");
    double st_start = get_time();

    unsigned long long *baseline = calloc((size_t)(max_n + 1),
                                          sizeof(unsigned long long));
    if (!baseline) { perror("calloc"); return EXIT_FAILURE; }

    for (int n = 0; n <= max_n; n++) {
        baseline[n] = catalan_iterative(n);
    }

    double st_end = get_time();
    double st_time = st_end - st_start;

    /* ================================================================
     *  RESULTS & VERIFICATION
     * ================================================================ */
    printf("============================================================\n");
    printf("  RESULTS SUMMARY\n");
    printf("============================================================\n");
    printf("  %5s  %22s  %22s  %s\n",
           "n", "C(n) [threaded]", "C(n) [baseline]", "Match?");
    printf("  %5s  %22s  %22s  %s\n",
           "-----", "----------------------",
           "----------------------", "------");

    int all_match = 1;
    for (int i = 0; i <= max_n; i++) {
        const char *status = (catalan[i] == baseline[i]) ? "  OK" : "  FAIL";
        if (catalan[i] != baseline[i]) all_match = 0;
        printf("  %5d  %22llu  %22llu  %s\n",
               i, catalan[i], baseline[i], status);
    }

    printf("\n  Verification: %s\n",
           all_match ? "ALL RESULTS MATCH ✓" : "MISMATCH DETECTED ✗");

    /* ================================================================
     *  PERFORMANCE COMPARISON
     * ================================================================ */
    printf("\n============================================================\n");
    printf("  PERFORMANCE COMPARISON\n");
    printf("============================================================\n");
    printf("  Multithreaded time : %.6f seconds\n", mt_time);
    printf("  Single-threaded    : %.6f seconds\n", st_time);

    if (st_time > 0) {
        double speedup = st_time / mt_time;
        printf("  Speedup            : %.2fx\n", speedup);
    }

    /* Total threads spawned: 0 + 1 + 2 + ... + max_n = max_n*(max_n+1)/2 */
    long long total_threads = (long long)max_n * (max_n + 1) / 2;
    printf("  Total threads used : %lld\n", total_threads);
    printf("============================================================\n");

    /* ---- Cleanup ---- */
    free(catalan);
    free(baseline);
    pthread_mutex_destroy(&sum_mutex);

    return EXIT_SUCCESS;
}

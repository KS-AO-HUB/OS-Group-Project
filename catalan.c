/*
 * =============================================================================
 *  Project: Calculation of Catalan Numbers in a Multithreading System
 *  Course:  COP 6611 — Operating Systems
 *
 *  Description:
 *      Computes Catalan numbers C(0) through C(N) using POSIX threads.
 *      TWO levels of parallelism:
 *
 *      Level 1 — One "manager" thread per Catalan number C(1)..C(N).
 *                Each manager waits (via condition variables) until all
 *                prerequisite values C(0)..C(n-1) are available.
 *
 *      Level 2 — Once dependencies are satisfied, the manager for C(n)
 *                spawns worker threads that compute disjoint chunks of
 *                the summation  Σ C(i)*C(n-1-i)  in parallel, then
 *                combines the partial sums.
 *
 *  OS Concepts Demonstrated:
 *      - Multithreading        (pthread_create / pthread_join)
 *      - Mutual exclusion      (pthread_mutex_lock / pthread_mutex_unlock)
 *      - Condition variables   (pthread_cond_wait / pthread_cond_broadcast)
 *      - Shared memory between threads (global arrays)
 *      - Dependency management without busy-waiting
 *      - Parallel reduction    (splitting work among sub-threads)
 *
 *  Compilation:
 *      make        (or)  gcc -Wall -Wextra -std=c11 -pthread -o catalan catalan.c
 *
 *  Usage:
 *      ./catalan <input_file>
 *      (input_file contains a single integer N on line 1)
 *
 *  Authors:  Ariel Gomez Garcia, Yoel Polanco
 *  Date:     April 2026
 * =============================================================================
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* Maximum safe index before unsigned long long overflow */
#define MAX_SAFE_N    33

/* Maximum worker threads used to parallelize the inner summation */
#define MAX_WORKERS   4

/* Output file name for saving results */
#define OUTPUT_FILE   "output.txt"

/* ---------------------------------------------------------------------------
 *  Shared state — all manager threads read/write through these globals.
 *  catalan[] holds the computed Catalan values and done[] tracks which
 *  values have been fully computed.  Both arrays are protected by 'lock'.
 * --------------------------------------------------------------------------- */
static unsigned long long *catalan;   /* catalan[i] = C(i)                   */
static int                *done;      /* done[i] = 1 once C(i) is computed   */
static int                 max_n;     /* We compute C(0) .. C(max_n)         */

/* ---------------------------------------------------------------------------
 *  Synchronization primitives for inter-manager dependency management.
 *
 *  A single mutex protects all shared state.  A single condition variable
 *  is used with broadcast: whenever ANY manager finishes computing its
 *  value, it broadcasts so that all other waiting managers re-check their
 *  dependencies.  This avoids the need for per-value condition variables
 *  while still preventing busy-waiting.
 * --------------------------------------------------------------------------- */
static pthread_mutex_t lock       = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  deps_ready = PTHREAD_COND_INITIALIZER;

/* ---------------------------------------------------------------------------
 *  Argument passed to each manager thread
 * --------------------------------------------------------------------------- */
typedef struct {
    int n;                            /* This manager computes C(n) */
} manager_arg_t;

/* ---------------------------------------------------------------------------
 *  Argument passed to each worker thread (inner-summation parallelism)
 * --------------------------------------------------------------------------- */
typedef struct {
    int                n;             /* Computing C(n)             */
    int                lo;            /* Start index (inclusive)    */
    int                hi;            /* End index (exclusive)      */
    unsigned long long partial_sum;   /* Result written by worker   */
} worker_arg_t;

/* ---------------------------------------------------------------------------
 *  check_deps_ready:  returns 1 iff all C(0)..C(n-1) are computed.
 *  MUST be called with 'lock' held.
 * --------------------------------------------------------------------------- */
static int check_deps_ready(int n)
{
    for (int i = 0; i < n; i++) {
        if (!done[i]) return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------------------
 *  Worker thread: computes a partial sum of the Catalan recurrence.
 *
 *  partial_sum = Σ  C(i) * C(n-1-i)   for i in [lo, hi)
 *
 *  By the time workers are spawned, all C(0)..C(n-1) are guaranteed to
 *  be available (the manager already waited for them), and the manager
 *  has released the global mutex.  Workers only READ from catalan[],
 *  so no additional locking is needed — multiple workers can safely
 *  read the same published values concurrently.
 * --------------------------------------------------------------------------- */
static void *worker_func(void *arg)
{
    worker_arg_t *wa = (worker_arg_t *)arg;
    unsigned long long sum = 0;

    for (int i = wa->lo; i < wa->hi; i++) {
        sum += catalan[i] * catalan[wa->n - 1 - i];
    }

    wa->partial_sum = sum;
    return NULL;
}

/* ---------------------------------------------------------------------------
 *  Manager thread: compute C(n)
 *
 *  1.  Lock the global mutex.
 *  2.  Wait (cond_wait) until all C(0)..C(n-1) are marked done.
 *  3.  UNLOCK the mutex — so that other managers whose dependencies are
 *      now satisfied can proceed concurrently while we compute.  This is
 *      critical: if we held the lock during computation, all managers
 *      would serialize and we'd lose all parallelism.
 *  4.  Spawn worker threads to compute disjoint chunks of the summation.
 *  5.  Join all workers, combine partial sums (parallel reduction).
 *  6.  Re-lock the mutex, store the result, mark done[n] = 1, broadcast
 *      to wake all managers that might depend on C(n).
 *  7.  Unlock the mutex.
 * --------------------------------------------------------------------------- */
static void *manager_func(void *arg)
{
    manager_arg_t *ma = (manager_arg_t *)arg;
    int n = ma->n;

    /* ---- Step 1-2: Wait for dependencies ---- */
    pthread_mutex_lock(&lock);
    while (!check_deps_ready(n)) {
        printf("  [Manager C(%2d)]  Waiting for dependencies...\n", n);
        pthread_cond_wait(&deps_ready, &lock);
    }
    printf("  [Manager C(%2d)]  Dependencies satisfied — spawning workers.\n", n);

    /* Step 3: Release lock so other managers can proceed concurrently
     * while we compute.  This avoids serializing all work through one
     * mutex — the whole point of the two-level parallel design. */
    pthread_mutex_unlock(&lock);

    /* ---- Step 4: Determine number of workers and partition work ---- */
    int num_terms = n;                  /* number of terms in the sum */
    int num_workers = num_terms < MAX_WORKERS ? num_terms : MAX_WORKERS;

    pthread_t     wt[MAX_WORKERS];
    worker_arg_t  wa[MAX_WORKERS];
    int chunk     = num_terms / num_workers;
    int remainder = num_terms % num_workers;

    int start = 0;
    for (int w = 0; w < num_workers; w++) {
        wa[w].n  = n;
        wa[w].lo = start;
        wa[w].hi = start + chunk + (w < remainder ? 1 : 0);
        wa[w].partial_sum = 0;
        start = wa[w].hi;

        int rc = pthread_create(&wt[w], NULL, worker_func, &wa[w]);
        if (rc != 0) {
            fprintf(stderr, "Error: pthread_create worker: %s\n", strerror(rc));
            exit(EXIT_FAILURE);
        }
    }

    /* ---- Step 5: Join workers and combine partial sums ---- */
    unsigned long long total = 0;
    for (int w = 0; w < num_workers; w++) {
        pthread_join(wt[w], NULL);
        total += wa[w].partial_sum;
    }

    /* ---- Step 6: Store result under lock, broadcast ---- */
    pthread_mutex_lock(&lock);
    catalan[n] = total;
    done[n]    = 1;
    printf("  [Manager C(%2d)]  C(%d) = %llu  ✓  (workers: %d)\n\n",
           n, n, total, num_workers);
    pthread_cond_broadcast(&deps_ready);
    pthread_mutex_unlock(&lock);

    free(ma);
    return NULL;
}

/* ---------------------------------------------------------------------------
 *  Baseline: single-threaded iterative computation (for verification)
 * --------------------------------------------------------------------------- */
static void catalan_iterative(unsigned long long *result, int n)
{
    result[0] = 1;
    for (int i = 1; i <= n; i++) {
        result[i] = 0;
        for (int j = 0; j < i; j++) {
            result[i] += result[j] * result[i - 1 - j];
        }
    }
}

/* ---------------------------------------------------------------------------
 *  Timing helper — returns wall-clock time in seconds using CLOCK_MONOTONIC
 * --------------------------------------------------------------------------- */
static double get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* ---------------------------------------------------------------------------
 *  write_results_to_file:  Writes the results summary to OUTPUT_FILE so
 *  that a persistent copy of the output is available for the submission.
 * --------------------------------------------------------------------------- */
static void write_results_to_file(unsigned long long *cat,
                                  unsigned long long *base,
                                  int n, int all_match,
                                  double mt_time, double st_time)
{
    FILE *out = fopen(OUTPUT_FILE, "w");
    if (!out) {
        fprintf(stderr, "Warning: could not open '%s' for writing: %s\n",
                OUTPUT_FILE, strerror(errno));
        return;
    }

    fprintf(out, "============================================================\n");
    fprintf(out, "  Catalan Numbers — Results Summary\n");
    fprintf(out, "  C(0) through C(%d)\n", n);
    fprintf(out, "============================================================\n\n");

    fprintf(out, "  %5s  %22s  %22s  %s\n",
            "n", "C(n) [threaded]", "C(n) [baseline]", "Match?");
    fprintf(out, "  %5s  %22s  %22s  %s\n",
            "-----", "----------------------",
            "----------------------", "------");

    for (int i = 0; i <= n; i++) {
        const char *status = (cat[i] == base[i]) ? "  OK" : "  FAIL";
        fprintf(out, "  %5d  %22llu  %22llu  %s\n",
                i, cat[i], base[i], status);
    }

    fprintf(out, "\n  Verification: %s\n",
            all_match ? "ALL RESULTS MATCH" : "MISMATCH DETECTED");

    fprintf(out, "\n============================================================\n");
    fprintf(out, "  PERFORMANCE COMPARISON\n");
    fprintf(out, "============================================================\n");
    fprintf(out, "  Multithreaded time : %.6f seconds\n", mt_time);
    fprintf(out, "  Single-threaded    : %.6f seconds\n", st_time);
    if (mt_time > 0) {
        fprintf(out, "  Ratio (ST / MT)    : %.4fx\n", st_time / mt_time);
    }
    fprintf(out, "  Manager threads    : %d\n", n);
    fprintf(out, "  Max workers/manager: %d\n", MAX_WORKERS);
    fprintf(out, "============================================================\n");

    fclose(out);
    printf("  Results written to '%s'\n", OUTPUT_FILE);
}

/* ---------------------------------------------------------------------------
 *  Main
 * --------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    /* ---- Validate command-line arguments ---- */
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

    /* ---- Overflow warning ---- */
    if (max_n > MAX_SAFE_N) {
        fprintf(stderr, "Warning: C(n) for n > %d overflows unsigned long long.\n",
                MAX_SAFE_N);
        fprintf(stderr, "Results beyond C(%d) may be incorrect.\n\n", MAX_SAFE_N);
    }

    /* ---- Thread-count feasibility note ---- */
    /*
     * Each manager thread consumes stack space (typically 2-8 MB default).
     * For very large N (thousands), the OS may refuse to create that many
     * threads.  The program handles this gracefully — pthread_create errors
     * are caught and reported.  For this project N <= 33 is the practical
     * limit due to unsigned long long overflow anyway.
     */

    printf("============================================================\n");
    printf("  Catalan Numbers — Multithreaded Computation (Pthreads)\n");
    printf("  Computing C(0) through C(%d)\n", max_n);
    printf("============================================================\n\n");
    printf("  Two levels of parallelism:\n");
    printf("    Level 1: %d manager threads (one per Catalan number)\n", max_n);
    printf("    Level 2: up to %d worker threads per manager\n\n", MAX_WORKERS);

    /* ---- Allocate shared arrays ---- */
    catalan = calloc((size_t)(max_n + 1), sizeof *catalan);
    done    = calloc((size_t)(max_n + 1), sizeof *done);
    if (!catalan || !done) { perror("calloc"); return EXIT_FAILURE; }

    /* ================================================================
     *  MULTITHREADED COMPUTATION
     * ================================================================ */
    printf("--- Multithreaded Computation ---\n\n");

    /* Base case: C(0) = 1 */
    pthread_mutex_lock(&lock);
    catalan[0] = 1;
    done[0]    = 1;
    printf("  [Main thread]  C(0) = 1  (base case)\n\n");
    pthread_cond_broadcast(&deps_ready);
    pthread_mutex_unlock(&lock);

    double mt_start = get_time();

    /* Spawn all N manager threads simultaneously */
    pthread_t *managers = NULL;
    if (max_n > 0) {
        managers = malloc((size_t)max_n * sizeof(pthread_t));
        if (!managers) { perror("malloc"); return EXIT_FAILURE; }

        for (int n = 1; n <= max_n; n++) {
            manager_arg_t *ma = malloc(sizeof *ma);
            if (!ma) { perror("malloc"); return EXIT_FAILURE; }
            ma->n = n;

            int rc = pthread_create(&managers[n - 1], NULL, manager_func, ma);
            if (rc != 0) {
                fprintf(stderr, "Error: pthread_create for C(%d): %s\n",
                        n, strerror(rc));
                return EXIT_FAILURE;
            }
        }

        for (int n = 1; n <= max_n; n++) {
            pthread_join(managers[n - 1], NULL);
        }
        free(managers);
    }

    double mt_end = get_time();
    double mt_time = mt_end - mt_start;

    /* ================================================================
     *  SINGLE-THREADED BASELINE (for verification)
     *
     *  Note: the multithreaded timer includes thread creation and
     *  synchronization overhead, while the single-threaded timer only
     *  measures raw computation.  This is intentional — the comparison
     *  shows the real-world cost of the threaded approach.
     * ================================================================ */
    printf("--- Single-Threaded Baseline ---\n\n");

    unsigned long long *baseline = calloc((size_t)(max_n + 1), sizeof *baseline);
    if (!baseline) { perror("calloc"); return EXIT_FAILURE; }

    double st_start = get_time();
    catalan_iterative(baseline, max_n);
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

    if (mt_time > 0) {
        printf("  Ratio (ST / MT)    : %.4fx\n", st_time / mt_time);
    }

    printf("  Manager threads    : %d\n", max_n);
    printf("  Max workers/manager: %d\n", MAX_WORKERS);
    printf("============================================================\n\n");

    /* ---- Write results to output file ---- */
    write_results_to_file(catalan, baseline, max_n, all_match, mt_time, st_time);

    /* ---- Cleanup ---- */
    free(catalan);
    free(done);
    free(baseline);
    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&deps_ready);

    return EXIT_SUCCESS;
}
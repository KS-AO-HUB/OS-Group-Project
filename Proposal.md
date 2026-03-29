\documentclass[12pt, letterpaper]{article}

% ---------- Packages ----------
\usepackage[margin=1in]{geometry}
\usepackage{amsmath, amssymb}
\usepackage{times}
\usepackage{setspace}
\usepackage{parskip}
\usepackage{titlesec}

% ---------- Formatting ----------
\titleformat{\section}{\normalfont\bfseries\large}{\thesection.}{0.5em}{}
\titlespacing*{\section}{0pt}{12pt}{4pt}
\pagestyle{empty}

% ==========================================================================
\begin{document}

\begin{center}
    {\LARGE \textbf{Multithreaded Computation of Catalan Numbers\\Using POSIX Threads}}

    \vspace{10pt}

    {\large
        AGG, YP

    \vspace{4pt}

    {\normalsize
        COP 6611 --- Operating Systems\\
        March 29, 2026
    }
\end{center}

\vspace{6pt}

% ---------- Problem Description ----------
\section*{Problem Description}

Catalan numbers are a well-known sequence in combinatorics with applications in
counting binary search trees, parenthesizations, lattice paths, and numerous
other discrete structures. The $n$-th Catalan number is defined by the
recurrence relation:
%
\[
    C(0) = 1, \qquad C(n) = \sum_{i=0}^{n-1} C(i) \cdot C(n-1-i)
\]
%
Because computing $C(n)$ depends on all previous values $C(0)$ through
$C(n-1)$, this problem presents a natural opportunity to explore concurrent
programming with dependency management. Our goal is to design and implement a
multithreaded system in C using the POSIX Threads (Pthreads) library that
computes Catalan numbers $C(0)$ through $C(N)$ in parallel, where $N$ is
provided as user input.

% ---------- Intended Approach ----------
\section*{Intended Approach}

We will spawn one dedicated thread for each Catalan number $C(n)$. Since $C(n)$
requires the results of $C(0)$ through $C(n-1)$, we must ensure threads
synchronize correctly. Our approach involves:

\begin{itemize}
    \item \textbf{Shared Memory}: A global array stores computed Catalan values,
          accessible by all threads.
    \item \textbf{Mutex Locks} (\texttt{pthread\_mutex\_t}): Protect concurrent
          reads and writes to the shared results array, preventing data races.
    \item \textbf{Condition Variables} (\texttt{pthread\_cond\_t}): Allow a
          thread computing $C(n)$ to sleep efficiently until all of its
          prerequisite values have been computed, avoiding busy-waiting.
\end{itemize}

Each thread will lock the mutex, check whether all prerequisite values are
available, and if not, wait on the condition variable. When a thread finishes
computing its value, it broadcasts a signal to wake all waiting threads. The
input value $N$ will be read from a user-provided text file passed as a
command-line argument.

% ---------- Evaluation Methods ----------
\section*{Evaluation Methods}

We will evaluate our solution on the following criteria:

\begin{enumerate}
    \item \textbf{Correctness}: We will verify every multithreaded result
          against an independent single-threaded iterative computation. The
          program will output a side-by-side comparison table with a pass/fail
          indicator for each value.

    \item \textbf{Synchronization Integrity}: We will test with various values
          of $N$ (including edge cases such as $N = 0$ and $N = 1$) to confirm
          that no race conditions or deadlocks occur, and that threads correctly
          wait for their dependencies.

    \item \textbf{Scalability Observation}: We will measure execution time for
          increasing values of $N$ and observe how thread overhead and
          synchronization costs affect performance relative to a single-threaded
          baseline.

    \item \textbf{Robustness}: We will test invalid inputs (negative numbers,
          non-integer values, missing files) to ensure the program handles
          errors gracefully.
\end{enumerate}

All experiments will be conducted on a Linux environment using GCC with the
\texttt{-pthread} flag. Results, including program output and timing data, will
be documented in the final report.

\end{document}
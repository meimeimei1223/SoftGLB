// OpenMP stub for Emscripten - OpenMP is not supported in WebAssembly
#pragma once

// Stub implementations of OpenMP functions
inline int omp_get_max_threads() { return 1; }
inline int omp_get_thread_num() { return 0; }
inline void omp_set_num_threads(int) {}

// Disable OpenMP pragmas by defining them as empty
#define omp_parallel
#define omp_for
#define omp_parallel_for
#define omp_critical
#define omp_atomic
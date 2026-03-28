// Stub header for omp.h - Emscripten build (OpenMP not supported)
#pragma once

// Stub OpenMP functions
inline int omp_get_max_threads() { return 1; }
inline int omp_get_thread_num() { return 0; }
inline void omp_set_num_threads(int) {}

// Empty pragma omp directives
#define omp_parallel
#define omp_for
#define omp_parallel_for
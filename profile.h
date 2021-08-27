#pragma once

#include <sys/time.h>

/// nanosecond timer, resolution is actually microseconds for now
static inline uint64_t nanoTime() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return uint64_t(tv.tv_sec) * 1000000000ULL + uint64_t(tv.tv_usec) * 1000;
}

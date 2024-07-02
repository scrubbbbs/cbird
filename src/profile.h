/* Profiling utilities
   Copyright (C) 2021 scrubbbbs
   Contact: screubbbebs@gemeaile.com =~ s/e//g
   Project: https://github.com/scrubbbbs/cbird

   This file is part of cbird.

   cbird is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   cbird is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with cbird; if not, see
   <https://www.gnu.org/licenses/>.  */
#pragma once

#include <sys/time.h>

/// nanosecond timer, resolution is actually microseconds for now
static inline uint64_t nanoTime() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return uint64_t(tv.tv_sec) * 1000000000ULL + uint64_t(tv.tv_usec) * 1000;
}

/* Get system information
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

#if defined(Q_OS_DARWIN)
#include <malloc/malloc.h>
#define malloc_size(x) malloc_size((const void*) (x))
#elif defined(Q_OS_WIN)
#include <malloc.h>
#define malloc_size(x) _msize((void*) (x))
#else
#include <malloc.h>
#define malloc_size(x) malloc_usable_size((void*) (x))
#endif

/// Get information about the operating system / environment
class Env {
  Env() = delete;
  ~Env() = delete;

 public:
  /// memory state of all processes and kernel
  static void systemMemory(float& totalKb, float& freeKb);

  /// memory state of current process
  static void memoryUsage(float& vmKb, float& workingSetKb);

  /// set calling process to lowest/idle priority
  static void setIdleProcessPriority();
};

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
#include "env.h"

#include <unistd.h>

#include <fstream>
#include <ios>
#include <iostream>
#include <string>

#ifdef Q_OS_WIN

// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on

void Env::systemMemory(float& totalKb, float& freeKb) {
  totalKb = freeKb = 0;
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) {
    (void)totalKb;
    (void)freeKb;
    totalKb = status.ullTotalPhys / 1024.0;
    freeKb = status.ullAvailPhys / 1024.0;
  }
}

void Env::memoryUsage(float& virtualKb, float& workingSetKb) {
  virtualKb = workingSetKb = 0;
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    virtualKb = pmc.PagefileUsage / 1024.0;
    workingSetKb = pmc.WorkingSetSize / 1024.0;
  }
}

void Env::setIdleProcessPriority() {
  if (!SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS) != 0)
    qWarning() << "SetPriorityClass() failed";
}

#elif defined(Q_OS_LINUX)

#  include <sys/resource.h>

void Env::systemMemory(float& totalKb, float& freeKb) {
  QFile f("/proc/meminfo");
  totalKb = freeKb = 0;
  if (!f.open(QFile::ReadOnly)) {
    qWarning() << f.fileName() << f.errorString();
    return;
  }
  QString line;
  while (!(line = f.readLine()).isEmpty()) {
    float value = line.split(" ", Qt::SkipEmptyParts).at(1).toFloat();
    if (line.startsWith("MemTotal:"))
      totalKb = value;
    else if (line.startsWith("MemAvailable:")) {
      freeKb = value;
      break;
    }
  }
}

void Env::memoryUsage(float& virtualKb, float& workingSetKb) {
  using std::ifstream;
  using std::ios_base;
  using std::string;

  virtualKb = 0.0;
  workingSetKb = 0.0;

  // /proc seems to give the most reliable results
  ifstream stat_stream("/proc/self/stat", ios_base::in);

  // dummy vars for leading entries in stat that we don't care about
  string pid, comm, state, ppid, pgrp, session, tty_nr;
  string tpgid, flags, minflt, cminflt, majflt, cmajflt;
  string utime, stime, cutime, cstime, priority, nice;
  string O, itrealvalue, starttime;

  // the two fields we want
  unsigned long vsize;
  long rss;

  stat_stream >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags >>
      minflt >> cminflt >> majflt >> cmajflt >> utime >> stime >> cutime >> cstime >> priority >>
      nice >> O >> itrealvalue >> starttime >> vsize >> rss;  // don't care about the rest

  stat_stream.close();

  // page size is configurable in kernel
  long page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024;
  virtualKb = vsize / 1024.0f;
  workingSetKb = rss * page_size_kb;
}

void Env::setIdleProcessPriority() {
  if (setpriority(PRIO_PROCESS, getpid(), 19) != 0)
    qWarning() << "setpriority() failed:" << errno << strerror(errno);
}

#elif defined(Q_OS_DARWIN)

#  include <mach/mach_host.h>
#  include <mach/mach_init.h>
#  include <mach/mach_types.h>
#  include <mach/vm_statistics.h>
#  include <sys/sysctl.h>
#  include <sys/types.h>

void Env::systemMemory(float& totalKb, float& freeKb) {
  totalKb = freeKb = 0;
  // totalKb = sysctl -a hw.memsize
  // freeKb = vm_stat + foo

  int mib[2] = {CTL_HW, HW_MEMSIZE};
  int64_t totalBytes = 0;
  size_t length = sizeof(totalBytes);

  sysctl(mib, 2, &totalBytes, &length, NULL, 0);
  totalKb = totalBytes / 1024.0;

  vm_size_t pageSize;
  vm_statistics64_data_t vm;

  mach_port_t port = mach_host_self();
  mach_msg_type_number_t count = sizeof(vm) / sizeof(natural_t);

  if (KERN_SUCCESS == host_page_size(port, &pageSize) &&
      KERN_SUCCESS == host_statistics64(port, HOST_VM_INFO, (host_info64_t)&vm, &count)) {
    // This api doesn't provide all the details of Activity Monitor
    // The free count is very small if we don't consider cached pages,
    // but the api doesn't report them.
    //
    // So it might be ok to guess that we can have a chunk of the inactive pages.
    auto freePages = vm.free_count + vm.inactive_count / 2;
    // auto usedPages = vm.active_count + vm.wire_count + vm.inactive_count/2;
    freeKb = freePages * pageSize / 1024.0;
  }
}

void Env::memoryUsage(float& virtualKb, float& workingSetKb) {
  virtualKb = 0.0;
  workingSetKb = 0.0;
}

void Env::setIdleProcessPriority() {
  if (setpriority(PRIO_PROCESS, getpid(), 19) != 0)
    qWarning() << "setpriority() failed:" << errno << strerror(errno);
}

#else

void Env::systemMemory(float& totalKb, float& freeKb) {
  totalKb = freeKb = 0;
  qCritical() << "unsupported";
}

void Env::memoryUsage(float& virtualKb, float& workingSetKb) {
  virtualKb = 0.0;
  workingSetKb = 0.0;
}

void Env::setIdleProcessPriority() { qCritical() << "unsupported"; }

#endif

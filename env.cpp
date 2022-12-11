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

#include <psapi.h>

void Env::systemMemory(float& totalKb, float& freeKb) {
  totalKb = freeKb = 0;
  MEMORYSTATUSEX status;
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) {
      (void)totalKb; (void)freeKb;
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
#else

void Env::systemMemory(float& totalKb, float& freeKb) {
  QFile f("/proc/meminfo");
  totalKb = freeKb = 0;
  if (!f.open(QFile::ReadOnly)) {
    qWarning() << f.fileName() << f.errorString();
    return;
  }
  QString line;
  while ( ! (line = f.readLine()).isEmpty() ) {
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

  stat_stream >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr >>
      tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >>
      stime >> cutime >> cstime >> priority >> nice >> O >> itrealvalue >>
      starttime >> vsize >> rss;  // don't care about the rest

  stat_stream.close();

  // page size is configurable in kernel
  long page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024;
  virtualKb = vsize / 1024.0f;
  workingSetKb = rss * page_size_kb;
}

#include <sys/resource.h>
static void setProcessPriority(int priority) {
  if (setpriority(PRIO_PROCESS, getpid(), priority) != 0)
    qWarning() << "setpriority() failed:" << errno << strerror(errno);
}

LowPriority::LowPriority() {
  setProcessPriority(19);
}

LowPriority::~LowPriority() {
  setProcessPriority(0);
}

#endif

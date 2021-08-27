#include "env.h"

#include <unistd.h>
#include <fstream>
#include <ios>
#include <iostream>
#include <string>

#ifdef __WIN32__
static void systemMemory(float& totalKb, float& freeKb) {
  // todo: win32windows port
  totalKb = freeKb = 0;
}

void Env::memoryUsage(float& virtualKb, float& workingSetKb) {
  // todo: win32 port
  virtualKb = workingSetKb = 0;
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
#endif

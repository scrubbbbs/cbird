/* Processor utilities
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

#ifdef __gnu_linux__
#include <QtCore/QFile>
#  include <unistd.h>  // usleep

/**
 * @class CPU
 * @brief Global processor utilization
 *
 * Idea was to delay indexing if the utilization was too high to avoid
 * bogging the system or starting too many index tasks.
 *
 * This has been abandoned, the user can choose how many threads to use instead
 */
class CPU {
 public:
  static CPU& instance() {
    static auto* p = new CPU;
    return *p;
  }

  CPU() { readTimes(); }

  // average usage since last measurement or constructor
  float usage() {
    uint64_t total = _total;
    uint64_t active = _active;
    readTimes();
    total = _total - total;
    active = _active - active;

    // divide by zero guard
    if (total == 0) return 1;

    return float(active) / total;
  }

  void waitUntilLower(float minUsage) {
    float cpu;
    while ((cpu = usage()) > minUsage) {
      usleep(100);
      qInfo("waiting: %.2f", double(cpu));
    }
  }

 private:
  void readTimes() {
    QFile f("/proc/stat");
    f.open(QFile::ReadOnly);
    QString line = f.readLine();
    QStringList vals = line.split(" ");

    // cpu user nice sys idle iowait irq softirq
    // cpu  2255 34 2290 22625563 6290 127 456

    uint64_t user = vals[1].toULongLong();
    uint64_t nice = vals[2].toULongLong();
    uint64_t sys = vals[3].toULongLong();
    uint64_t idle = vals[4].toULongLong();
    uint64_t iowait = vals[5].toULongLong();
    uint64_t irq = vals[6].toULongLong();
    uint64_t softirq = vals[7].toULongLong();
    uint64_t steal = vals[8].toULongLong();
    // guest time included in user time
    // uint64_t guest     = vals[9].toLongLong();
    // uint64_t guest_nice = vals[10].toLongLong();
    _active = user + nice + sys + irq + softirq + steal;
    _total = _active + idle + iowait;
  }

  uint64_t _active, _total;
};

#elif defined(__APPLE__)
/*
NSTimer *updateTimer;
NSLock *CPUUsageLock;
*.m file

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    int mib[2U] = { CTL_HW, HW_NCPU };
    size_t sizeOfNumCPUs = sizeof(numCPUs);
    int status = sysctl(mib, 2U, &numCPUs, &sizeOfNumCPUs, NULL, 0U);
    if(status)
        numCPUs = 1;

    CPUUsageLock = [[NSLock alloc] init];

    updateTimer = [[NSTimer scheduledTimerWithTimeInterval:3
                                                    target:self
                                                  selector:@selector(updateInfo:)
                                                  userInfo:nil
                                                   repeats:YES] retain];
}

- (void)updateInfo:(NSTimer *)timer
{
    natural_t numCPUsU = 0U;
    kern_return_t err = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &numCPUsU,
&cpuInfo, &numCpuInfo); if(err == KERN_SUCCESS) { [CPUUsageLock lock];

        for(unsigned i = 0U; i < numCPUs; ++i) {
            float inUse, total;
            if(prevCpuInfo) {
                inUse = (
                         (cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER]   -
prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER])
                         + (cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM] -
prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM])
                         + (cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE]   -
prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE])
                         );
                total = inUse + (cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE] -
prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE]); } else { inUse = cpuInfo[(CPU_STATE_MAX * i) +
CPU_STATE_USER] + cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM] + cpuInfo[(CPU_STATE_MAX * i) +
CPU_STATE_NICE]; total = inUse + cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE];
            }

            NSLog(@"Core: %u Usage: %f",i,inUse / total);
        }
        [CPUUsageLock unlock];

        if(prevCpuInfo) {
            size_t prevCpuInfoSize = sizeof(integer_t) * numPrevCpuInfo;
            vm_deallocate(mach_task_self(), (vm_address_t)prevCpuInfo, prevCpuInfoSize);
        }

        prevCpuInfo = cpuInfo;
        numPrevCpuInfo = numCpuInfo;

        cpuInfo = NULL;
        numCpuInfo = 0U;
    } else {
        NSLog(@"Error!");
        [NSApp terminate:nil];
    }
}

*/
#  include <mach/mach.h>
#  include <mach/mach_host.h>
#  include <mach/processor_info.h>
#  include <sys/sysctl.h>
#  include <sys/types.h>
#  include <unistd.h>

#include <QtConcurrent/QtConcurrentRun>

class CPU {
 public:
  processor_info_array_t cpuInfo, prevCpuInfo;
  mach_msg_type_number_t numCpuInfo, numPrevCpuInfo;
  unsigned numCPUs;
  float _cpuUsage;

 public:
  CPU() {
    int mib[2U] = {CTL_HW, HW_NCPU};
    size_t sizeOfNumCPUs = sizeof(numCPUs);
    int status = sysctl(mib, 2U, &numCPUs, &sizeOfNumCPUs, NULL, 0U);
    if (status) numCPUs = 1;
    prevCpuInfo = NULL;
    cpuInfo = NULL;
    numCpuInfo = 0;
    numPrevCpuInfo = 0;
    _cpuUsage = 0;

    (void)QtConcurrent::run(&CPU::poll, this);
  }

  float cpuUsage() const { return _cpuUsage; }

  void waitUntilLower(float usage) const {
    while (cpuUsage() > usage) usleep(100000);
  }

  void poll() {
    while (true) {
      natural_t numCPUsU = 0U;
      kern_return_t err = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &numCPUsU,
                                              &cpuInfo, &numCpuInfo);
      if (err == KERN_SUCCESS) {
        // float totalUsage = 0;
        float totalUsed = 0, totalAvail = 0;
        for (unsigned i = 0U; i < numCPUs; ++i) {
          float used, avail;

          if (prevCpuInfo) {
            used = ((cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER] -
                     prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER]) +
                    (cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM] -
                     prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM]) +
                    (cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE] -
                     prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE]));
            avail = used + (cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE] -
                            prevCpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE]);
          } else {
            used = cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_USER] +
                   cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_SYSTEM] +
                   cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_NICE];
            avail = used + cpuInfo[(CPU_STATE_MAX * i) + CPU_STATE_IDLE];
          }
          totalUsed += used;
          totalAvail += avail;
        }

        _cpuUsage = totalUsed / totalAvail;

        printf("CPU: Usage: %.2f\n", _cpuUsage);

        //[CPUUsageLock unlock];

        if (prevCpuInfo) {
          size_t prevCpuInfoSize = sizeof(integer_t) * numPrevCpuInfo;
          vm_deallocate(mach_task_self(), (vm_address_t)prevCpuInfo, prevCpuInfoSize);
        }

        prevCpuInfo = cpuInfo;
        numPrevCpuInfo = numCpuInfo;

        cpuInfo = NULL;
        numCpuInfo = 0U;
      } else {
        // NSLog(@"Error!");
        //[NSApp terminate:nil];
      }

      usleep(1000000);
    }
  }
};

#else

class CPU {
 public:
  static CPU& instance() {
    static auto* p = new CPU;
    return *p;
  }

  CPU() {}

  // average usage since last measurement or constructor
  float usage() { return 0.0f; }

  void waitUntilLower(float minUsage) { (void)minUsage; }
};

#endif

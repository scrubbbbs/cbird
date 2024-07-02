/* Memory Manager for QImage
   Copyright (C) 2024 scrubbbbs
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
#include "pooledimageallocator.h"
#include "../env.h"

#include <malloc.h> // aligned malloc, etc

uchar* PooledImageAllocator::alloc(const QSize& size, QImage::Format fmt) {
  QPixelFormat pFmt = QImage::toPixelFormat(fmt);
  int bytesPerPixel = pFmt.bitsPerPixel() / 8;
  Q_ASSERT(bytesPerPixel > 0);

  int bytesPerLine = size.width() * bytesPerPixel;
  bytesPerLine += bytesPerLine % 4 == 0 ? 0 : 4 - bytesPerLine % 4;
  Q_ASSERT(bytesPerLine % 4 == 0);

  size_t dataSz = size_t(bytesPerLine) * size.height();
  // TODO: round size up so slightly different images (rotated) can use the same buffers

  uchar* dataPtr = nullptr;
  {
    QMutexLocker locker(&_mutex);
    const auto it = _pool.find(dataSz);
    if (it != _pool.end())
      for (uchar* ptr : *it) {
        if (_free.contains(ptr)) {
          //qInfo() << "reuse" << size << fmt << dataSz  << _free.count()-1;
          _free.remove(ptr);
          dataPtr = ptr;
          break;
        }
      }
  }

  if (dataPtr == nullptr) {
    QMutexLocker locker(&_mutex);

    do {
      float totalKb, freeKb;
      Env::systemMemory(totalKb, freeKb);
      if (freeKb-_minSysFreeKb > dataSz / 1024) break;

      if (_compactOnFail) {
        compactInternal();
        _compactOnFail = false;
        continue;
      }

      qWarning() << "out of memory, avail:" << (size_t)freeKb << "minFree:" << _minSysFreeKb
                 << "required:" << dataSz / 1024;
      return nullptr;

    } while (true);

    int err = posix_memalign((void**) &dataPtr, qMax(sizeof(void*), (size_t) 4), dataSz);
    if (err != 0) {
      qCritical() << err << strerror(err);
      return nullptr;
    }
    _pool[dataSz].append(dataPtr);

    qDebug() << size << fmt;
  }

  _compactOnFail = false;

  return dataPtr;
}

void PooledImageAllocator::free(void* ptr) {
  Q_ASSERT(ptr);
  // TODO: maybe also zero the buffer
  QMutexLocker locker(&_mutex);
  _free.insert((uchar*) ptr);
}

size_t PooledImageAllocator::compactInternal() {

  // remove from the pool
  for (auto& list : _pool)
    list.removeIf([this](uchar* ptr) { return _free.contains(ptr); });

  size_t bytesFreed = 0;
  // free memory in reverse-sorted order, less fragmentation?
  auto ptrList = _free.values();
  std::sort(ptrList.begin(), ptrList.end(), [](uchar* a, uchar* b) { return b < a; });
  for (uchar* ptr : qAsConst(ptrList)) {
    bytesFreed += malloc_usable_size(ptr);
    ::free(ptr);
  }

  qDebug() << "freed" << _free.count() << "blocks," << bytesFreed / 1024 << "kb";
  _free.clear();

  malloc_trim(64 * 1024);

  return bytesFreed;
}

size_t PooledImageAllocator::freeKb() const {
  size_t sum = 0;
  QMutexLocker locker(&_mutex);
  for (size_t size : _pool.keys())
    for (uchar* ptr : _pool.value(size))
      if (_free.contains(ptr)) sum += size;
  return sum / 1024.0;
}

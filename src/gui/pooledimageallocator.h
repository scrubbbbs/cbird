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
#pragma once
#include "../media.h"

#include <QtCore/QMutex>

/**
 * @brief allocator for Media::loadImage() that prevents OOM and speeds
 *        up loading by reusing buffers
 */
class PooledImageAllocator : public ImageAllocator
{
  Q_DISABLE_COPY_MOVE(PooledImageAllocator);

 private:
  const size_t _minSysFreeKb;
  bool _compactOnFail = false;

  QHash<size_t, QList<uchar*>> _pool;
  QSet<uchar*> _free;
  mutable QMutex _mutex;

  /// return pointer suitable for QImage((uchar*),...)
  uchar* alloc(const QSize& size, QImage::Format fmt) override;

  ///  callback from QImage when it releases the data
  void free(void* ptr) override;

  // release the free list (no locks held)
  size_t compactInternal();

 public:
  PooledImageAllocator(size_t minSysFreeKb)
      : _minSysFreeKb(minSysFreeKb) {}

  // destruction is not supported, since you'd have to guarantee no
  // QImages were holding pointers
  virtual ~PooledImageAllocator() = delete;

  // release the free list (thread-safe version)
  bool compact() {
    QMutexLocker locker(&_mutex);
    return compactInternal();
  }

  size_t freeKb() const;

  // attempt to compact heap on the next failed alloc
  // resets on first successful alloc or compaction
  void setCompactFlag() { _compactOnFail = true; }
};

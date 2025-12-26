/* Utilties for reading/writing files
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
#include "ioutil.h"

#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFuture>
#include <QtCore/QSemaphore>
#include <QtCore/QTemporaryFile>

QCancelableIODevice::QCancelableIODevice(QIODevice* io, const QFuture<void>* future)
    : _io(io), _future(future) {
  setOpenMode(_io->openMode());
}

QCancelableIODevice::~QCancelableIODevice() { delete _io; }

bool QCancelableIODevice::open(QIODevice::OpenMode flags) {
  bool ok = _io->open(flags);
  setOpenMode(_io->openMode() | QIODevice::Unbuffered);
  _io->seek(0);
  return ok;
}

void QCancelableIODevice::close() {
  super::close();
  return _io->close();
}

qint64 QCancelableIODevice::size() const { return _io->size(); }

bool QCancelableIODevice::reset() {
  super::seek(0);
  return _io->reset();
}

// bool QCancelableIODevice::atEnd() const {
//   return _io->atEnd();
// }

// bool QCancelableIODevice::canReadLine() const {
//   if (_future->isCanceled()) return false;
//   return _io->canReadLine();
// }

// qint64 QCancelableIODevice::pos() const {
//   return _io->pos();
// }

bool QCancelableIODevice::seek(qint64 pos) {
  super::seek(pos);
  return _io->seek(pos);
}

bool QCancelableIODevice::isSequential() const { return _io->isSequential(); }

qint64 QCancelableIODevice::readData(char* data, qint64 len) {
  if (_future->isCanceled()) return -1;
  return _io->read(data, len);
}

qint64 QCancelableIODevice::writeData(const char* data, qint64 len) {
  (void)data;
  (void)len;
  return -1;
}

void loadBinaryData(const QString& path, void** data, uint64_t* len, bool compress) {
  *data = nullptr;
  *len = 0;

  try {
    QFile f(path);
    if (!f.open(QFile::ReadOnly)) {
      qCritical() << "open failed:" << path << f.error() << f.errorString();
      return;
    }

    if (compress) {
      QByteArray b = f.readAll();
      if (compress) b = qUncompress(b);
      void* ptr = malloc(size_t(b.size()));
      if (!ptr) throw std::bad_alloc();
      memcpy(ptr, b.data(), size_t(b.size()));
      *data = ptr;
      *len = size_t(b.size());
    } else {
      qint64 size = f.size();
      char* ptr = strict_malloc(ptr, size);
      if (!ptr) throw std::bad_alloc();
      if (size != f.read(ptr, size))
        qFatal("failed to read file %d: %s", f.error(), qPrintable(f.errorString()));
      *data = ptr;
      *len = size;
    }

  } catch (std::bad_alloc& e) {
    // could be from QByteArray or malloc
    qFatal("bad_alloc");
  }
}

void saveBinaryData(const void* data, uint64_t len, const QString& path, bool compress) {
  writeFileAtomically(path, [data, len, compress](QFile& f) {
    QByteArray b = QByteArray::fromRawData(reinterpret_cast<const char*>(data), int(len));

    if (compress) b = qCompress(b);

    auto wrote = f.write(b);
    if (wrote != b.length()) throw f.errorString();
  });
}

QString fullMd5(QIODevice& io) {
#define THREADED_IO (1)
#if THREADED_IO
  // TODO: qt md5 seems slower than it should be
  QSemaphore producer(2);  // TODO: maybe setting for queue depth
  QSemaphore consumer;
  QMutex mutex;
  QList<QByteArray> chunks;

  // needs its own pool, if global pool is full it will block
  static QThreadPool ioPool;

  ioPool.start([&]() {
    while (!io.atEnd()) {
      producer.acquire();
      QByteArray buf = io.read(128 * 1024);  // TODO: setting for file i/o buffer size
      {
        QMutexLocker locker(&mutex);
        chunks.append(buf);
      }
      consumer.release();
    }
    consumer.release();
  });

  QCryptographicHash md5(QCryptographicHash::Md5);
  while (true) {
    consumer.acquire();
    QByteArray buf;
    {
      QMutexLocker locker(&mutex);
      if (chunks.empty()) break;
      buf = chunks[0];  // implicit sharing, no copy of data
      chunks.removeFirst();
    }
    producer.release();
    md5.addData(buf);
  }
  return md5.result().toHex();
#else
  QCryptographicHash md5(QCryptographicHash::Md5);
  const int buffSize = 128 * 1024;
  char buffer[buffSize];

  while (!io.atEnd()) {
    qint64 amount = io.read(buffer, buffSize);
    if (amount > 0) md5.addData(buffer, int(amount));
  }
  return md5.result().toHex();
#endif
}

#ifdef DEPRECATED
QString sparseMd5(QIODevice& file) {
  // if the file is small, md5 the whole thing,
  // otherwise, distribute over the first 1MB?
  // FIXME: distribute; make sure to include
  // the head/tail and something in the middle
  QByteArray bytes;
  const qint64 size = file.size();
  if (size < 16 * 1024) {
    bytes = file.readAll();
  } else {
    for (qint64 i = 1024 * 1024; i > 4 * 1024; i /= 2) {
      qint64 pos = std::max(0LL, size - i);
      file.seek(pos);
      bytes += file.read(1024);
    }
  }

  return QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex();
}
#endif

// this isn't useful unless we add signal handling
// not going down that road yet due to platform differences
#ifdef ABANDONED
class Dangling {
 public:
  Dangling() {
    _this = this;
    std::atexit(cleanup);
  }
  ~Dangling() { _this = nullptr; }

  static void cleanup() {
    if (!_this) {
      printf("Dangling: no this*, guess we are destructed\n");
      return;
    }
    for (const QString& path : _this->_files) {
      printf("Dangling: cleanup: %s\n", qUtf8Printable(path));
      QFile::remove(path);
    }
    printf("no Dangles\n");
  }

  void add(const QString& path) {
    QMutexLocker locker(&_mutex);
    _files.insert(path);
  }

  void remove(const QString& path) { _files.remove(path); }

 private:
  QMutex _mutex;
  QSet<QString> _files;
  static const Dangling* _this;
};
const Dangling* Dangling::_this = nullptr;
#endif

void writeFileAtomically(const QString& path, const std::function<void(QFile&)>& fn) {
  // static auto* d = new Dangling;
  // FIXME: using exceptions for control flow
  try {
    QTemporaryFile f(path);
    if (!f.open()) throw f.errorString();

    // const QString tmpName = f.fileName();
    // d->add(tmpName);

    fn(f);

    if (QFile::exists(path) && !QFile::remove(path)) throw qq("failed to remove old file");

    if (!f.rename(path)) throw f.errorString();

    f.setAutoRemove(false);

    // d->remove(tmpName);

  } catch (const QString& error) {
    qFatal("file system error writing %s: %s", qUtf8Printable(path), qUtf8Printable(error));
  }
}

bool SimpleIO_QFile::open(const QString& path, bool forReading) {
  _file.reset(new QFile(path));
  _filePath = "";
  _buffer.clear();
  if (!_file->open(forReading ? QFile::ReadOnly : QFile::WriteOnly)) {
    qCritical() << _file->errorString();
    return false;
  }
  _filePath = path;
  return true;
}

bool SimpleIO_QFile::readBytes(char* into, qint64 size, const char* msg) {
  qint64 readLen = _file->read(into, size);
  if (readLen < size) {
    qCritical().noquote() << "reading" << msg << ":" << size << readLen << _file->pos()
                          << (readLen > 0 ? "truncated file" : _file->errorString());
    return false;
  }
  return true;
}

bool SimpleIO_QFile::writeBytes(const char* from, qint64 size, const char* msg) {
  if (size != _file->write(from, size)) {
    qCritical() << "writing" << msg << ":" << _file->errorString();
    return false;
  }
  return true;
}

bool SimpleIO_QFile::readline(char* into, uint maxLen, const char* msg) {
  const QByteArray bytes = _file->readLine(maxLen);
  if (bytes.length() == 0) {
    qCritical() << "readline" << msg << ":" << _file->errorString();
    return false;
  }
  memcpy(into, bytes.data(), bytes.length());
  return true;
}

bool SimpleIO_QFile::rewind() {
  if (!_file->seek(0)) {
    qCritical() << "rewind" << _file->errorString();
    return false;
  }
  return true;
}

bool SimpleIO_QFile::bufferAll() {
  _buffer = _file->readAll();
  _file.reset(new QBuffer(&_buffer));
  if (!_file->open(QFile::ReadOnly)) {
    qCritical() << "open failed" << _file->errorString();
    return false;
  }
  return true;
}

bool SimpleIO_Stdio::open(const QString& path, bool forReading) {
  close();
  _filePath = "";
#ifdef Q_OS_WIN
  _file = _wfopen(qUtf16Printable(path), forReading ? L"rb" : L"wb");
#else
  _file = fopen(qUtf8Printable(path), forReading ? "rb" : "wb");
#endif
  if (!_file) {
    qCritical() << "open failed:" << strerror(errno);
    return false;
  }
  _filePath = path;
  return true;
}

bool SimpleIO_Stdio::readBytes(char* into, size_t size, const char* msg) {
  if (1 != fread(into, size, 1, _file)) {
    qCritical() << msg << ":" << strerror(errno);
    return false;
  }
  return true;
}

bool SimpleIO_Stdio::writeBytes(const char* from, size_t size, const char* msg) {
  if (1 != fwrite(from, size, 1, _file)) {
    qCritical() << msg << ":" << strerror(errno);
    return false;
  }
  return true;
}

bool SimpleIO_Stdio::readline(char* into, uint maxLen, const char* msg) {
  maxLen--;
  into[maxLen] = '\0';
  for (uint i = 0; i < maxLen; ++i) {
    int ch = fgetc(_file);
    if (ch == EOF) {
      qCritical() << msg << ":"
                  << "eof";
      return false;
    }
    into[i] = ch;
    if (ch == '\n') return true;
  }
  return false;
}

size_t SimpleIO_Stdio::fileSize() const {
  auto pos = ftell(_file);
  if (0 != fseek(_file, 0, SEEK_END)) {
    qCritical() << "seek end failed:" << strerror(errno);
    return 0;
  }
  auto len = ftell(_file);
  if (0 != fseek(_file, pos, SEEK_SET)) {
    qCritical() << "seek backward failed:" << strerror(errno);
    return 0;
  }
  return len;
}

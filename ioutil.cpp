#include "ioutil.h"

QCancelableIODevice::QCancelableIODevice(QIODevice *io, QFuture<void>* future)
    : _io(io), _future(future) {
  setOpenMode(_io->openMode());
}

QCancelableIODevice::~QCancelableIODevice() {
  delete _io;
}

bool QCancelableIODevice::open(QIODevice::OpenMode flags) {
  bool ok = _io->open(flags);
  setOpenMode(_io->openMode()|QIODevice::Unbuffered);
  _io->seek(0);
  return ok;
}

void QCancelableIODevice::close() {
  super::close();
  return _io->close();
}

qint64 QCancelableIODevice::size() const {
  return _io->size();
}

bool QCancelableIODevice::reset() {
  super::seek(0);
  return _io->reset();
}

//bool QCancelableIODevice::atEnd() const {
//  return _io->atEnd();
//}

//bool QCancelableIODevice::canReadLine() const {
//  if (_future->isCanceled()) return false;
//  return _io->canReadLine();
//}

//qint64 QCancelableIODevice::pos() const {
//  return _io->pos();
//}

bool QCancelableIODevice::seek(qint64 pos) {
  super::seek(pos);
  return _io->seek(pos);
}

bool QCancelableIODevice::isSequential() const {
  return _io->isSequential();
}

qint64 QCancelableIODevice::readData(char* data, qint64 len) {
  if (_future->isCanceled()) return -1;
  return _io->read(data, len);
}

qint64 QCancelableIODevice::writeData(const char* data, qint64 len) {
  (void)data;
  (void)len;
  return -1;
}

void loadBinaryData(const QString& path, void** data, uint64_t* len,
                    bool compress) {
  *data = nullptr;
  *len = 0;

  QFile f(path);
  f.open(QFile::ReadOnly);
  QByteArray b = (f.readAll());
  if (compress) b = qUncompress(b);

  void* ptr = malloc(size_t(b.size()));
  memcpy(ptr, b.data(), size_t(b.size()));

  *data = ptr;
  *len = size_t(b.size());

  f.close();
}

void saveBinaryData(const void* data, uint64_t len, const QString& path,
                    bool compress) {
  QFile f(path);
  f.open(QFile::WriteOnly | QFile::Truncate);
  QByteArray b =
      QByteArray::fromRawData(reinterpret_cast<const char*>(data), int(len));
  if (compress) b = qCompress(b);
  f.write(b);
  f.close();
}

QString fullMd5(QIODevice& io) {
#define THREADED_IO (1)
#if THREADED_IO
  // todo: qt5 md5 seems slower than it should be
  QSemaphore producer(2); // todo: settings
  QSemaphore consumer;
  QMutex mutex;
  QList<QByteArray> chunks;

  // needs its own pool, if global pool is full it will block
  static QThreadPool ioPool;

  ioPool.start([&]() {
    while (!io.atEnd()) {
      producer.acquire();
      QByteArray buf = io.read(128*1024); // todo: settings
      {
        QMutexLocker locker(&mutex);
        chunks.append( buf );
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
      buf = chunks[0]; // implicit sharing, no copy of data
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

QString sparseMd5(QIODevice& file) {
  // if the file is small, md5 the whole thing,
  // otherwise, distribute over the first 1MB?
  // fixme: distribute; make sure to include
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

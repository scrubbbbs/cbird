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
#pragma once

/**
 * File I/O wrapper w/error reporting/handling 
 */
class SimpleIO_QFile
{
  Q_DISABLE_COPY_MOVE(SimpleIO_QFile);

 private:
  std::unique_ptr<QIODevice> _file;
  QByteArray _buffer;
  QString _filePath;

 public:
  static const char* name() { return "qfile"; }
  SimpleIO_QFile() {}

  // close the current file and open another
  bool open(const QString& path, bool forReading);

  // close w/o destruction
  void close() { _file->close(); }

 private:
  bool readBytes(char* into, qint64 size, const char* msg);
  bool writeBytes(const char* from, qint64 size, const char* msg);

 public:
  // read <count> records of size sizeof(*into)
  template<typename T>
  bool read(T* into, uint count, const char* msg) {
    return readBytes((char*) into, sizeof(*into) * count, msg);
  }

  // write <count> records of size sizeof(*into)
  template<typename T>
  bool write(const T* from, uint count, const char* msg) {
    return writeBytes((char*) from, sizeof(*from) * count, msg);
  }

  // read line including the newline, always null-terminated
  // if successful the file position is just after the '\n',
  // otherwise the position is undefined
  bool readline(char* into, uint maxLen, const char* msg);

  // read <count> records from the end of file
  template<typename T>
  bool readEnd(T* into, uint count, const char* msg) {
    qint64 size = sizeof(*into) * count;
    if (!_file->seek(_file->size() - size)) {
      qCritical() << "seek end" << msg << ":" << _file->errorString();
      return false;
    }
    return readBytes((char*) into, size, msg);
  }

  // set file position back to the start
  bool rewind();

  // buffer remainder of the file for reading
  bool bufferAll();

  // size of file in bytes
  size_t fileSize() const { return _file->size(); }

  QString filePath() const { return _filePath; }
};

class SimpleIO_Stdio
{
  Q_DISABLE_COPY_MOVE(SimpleIO_Stdio);

 private:
  FILE* _file = nullptr;
  // char _buffer[256 * 1024];
  QString _filePath;

 public:
  static const char* name() { return "stdio"; }

  SimpleIO_Stdio() {}
  ~SimpleIO_Stdio() { close(); }

  bool open(const QString& path, bool forReading);

  void close() {
    if (_file) fclose(_file);
    _file = nullptr;
  }

 private:
  bool readBytes(char* into, size_t size, const char* msg);
  bool writeBytes(const char* from, size_t size, const char* msg);

 public:
  template<typename T>
  bool read(T* into, uint count, const char* msg) {
    return readBytes((char*) into, sizeof(*into) * count, msg);
  }

  template<typename T>
  bool write(const T* from, uint count, const char* msg) {
    return writeBytes((char*) from, sizeof(*from) * count, msg);
  }

  bool readline(char* into, uint maxLen, const char* msg);

  template<typename T>
  bool readEnd(T* into, uint count, const char* msg) {
    int64_t size = sizeof(*into) * count;
    if (0 != fseek(_file, -size, SEEK_END)) {
      qCritical() << "seek end" << msg << ":" << strerror(errno);
      return false;
    }
    return readBytes((char*) into, size, msg);
  }

  bool rewind() { return 0 == fseek(_file, 0, SEEK_SET); }

  bool bufferAll() {
    // linux seems to do pretty well without this
    //setvbuf(_file, _buffer, _IOFBF, sizeof(_buffer));
    return true;
  }

  // size of file in bytes
  size_t fileSize() const;

  QString filePath() const { return _filePath; }
};

/// QIODevice wrapper that can fake an EOF error to halt the consumer
class QCancelableIODevice : public QIODevice
{
  NO_COPY_NO_DEFAULT(QCancelableIODevice, QIODevice)
 public:
  QCancelableIODevice(QIODevice* io, const QFuture<void>* future);
  ~QCancelableIODevice();

  bool open(QIODevice::OpenMode flags) override;
  void close() override;
  qint64 size() const override;
  // bool atEnd() const override;
  // bool canReadLine() const override;
  // qint64 pos() const override;
  bool isSequential() const override;
  bool seek(qint64 pos) override;
  bool reset() override;

 protected:
  qint64 readData(char* data, qint64 len) override;
  qint64 writeData(const char* data, qint64 len) override;

 private:
  QIODevice* _io;
  const QFuture<void>* _future;
};

/// md5 the entire file/buffer
QString fullMd5(QIODevice& io);

/// "good enough" md5 that doesn't have to read the whole file
/// @note not very useful, full md5 is still needed usually
QString sparseMd5(QIODevice& file);

/// all-or-nothing file writing, function must throw QString for errors
void writeFileAtomically(const QString& path, const std::function<void(QFile&)>& fn);

/// read binary blob
void loadBinaryData(const QString& path, void** data, uint64_t* len, bool compress);

/// write binary blob
void saveBinaryData(const void* data, uint64_t len, const QString& path, bool compress);

/// write std::map (assuming A,B are POD types)
template<typename A, typename B>
static void saveMap(const std::map<A, B>& map, const QString& path) {
  writeFileAtomically(path, [&map](QFile& f) {
    for (const auto& it : map) {
      const A& key = it.first;
      const B& value = it.second;
      auto sk = f.write(reinterpret_cast<const char*>(&key), sizeof(key));
      if (sk != sizeof(key)) throw f.errorString();
      auto sv = f.write(reinterpret_cast<const char*>(&value), sizeof(value));
      if (sv != sizeof(value)) throw f.errorString();
    }
  });
}

/// read std::map
template<typename A, typename B>
static void loadMap(std::map<A, B>& map, const QString& file) {
  QFile f(file);
  if (!f.open(QFile::ReadOnly))
    qFatal("failed to open for reading: %s", qUtf8Printable(f.fileName()));

  while (!f.atEnd()) {
    A key;
    B value;
    f.read(reinterpret_cast<char*>(&key), sizeof(key));
    f.read(reinterpret_cast<char*>(&value), sizeof(value));
    map[key] = value;
  }
}

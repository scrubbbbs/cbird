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

/// QIODevice wrapper that can EOF on command to stop the consumer
class QCancelableIODevice : public QIODevice {
  NO_COPY_NO_DEFAULT(QCancelableIODevice, QIODevice)
 public:
  QCancelableIODevice(QIODevice* io, QFuture<void>* future);
  ~QCancelableIODevice();

  bool open(QIODevice::OpenMode flags) override;
  void close() override;
  qint64 size() const override;
  //bool atEnd() const override;
  //bool canReadLine() const override;
  //qint64 pos() const override;
  bool isSequential() const override;
  bool seek(qint64 pos) override;
  bool reset() override;

 protected:
  qint64 readData(char *data, qint64 len) override;
  qint64 writeData(const char *data, qint64 len) override;

 private:
    QIODevice* _io;
    QFuture<void>* _future;
};

/// md5 the entire file/buffer
QString fullMd5(QIODevice& io);

/// "good enough" md5 that doesn't have to read the whole file
/// @note not very useful, full md5 is still needed usually
QString sparseMd5(QIODevice& file);

/// read binary blob
void loadBinaryData(const QString& path, void** data, uint64_t* len,
                    bool compress);

/// write binary blob
void saveBinaryData(const void* data, uint64_t len, const QString& path,
                    bool compress);

/// write std::map (assuming A,B are POD types)
template <typename A, typename B>
static void saveMap(const std::map<A, B>& map, const QString& file) {
  QFile f(file);
  if (!f.open(QFile::WriteOnly | QFile::Truncate))
    qFatal("failed to open for writing: %s", qPrintable(f.fileName()));

  for (const auto& it : map) {
    const A& key = it.first;
    const B& value = it.second;
    f.write(reinterpret_cast<const char*>(&key), sizeof(key));
    f.write(reinterpret_cast<const char*>(&value), sizeof(value));
  }
}

/// read std::map
template <typename A, typename B>
static void loadMap(std::map<A, B>& map, const QString& file) {
  QFile f(file);
  if (!f.open(QFile::ReadOnly))
    qFatal("failed to open for reading: %s", qPrintable(f.fileName()));

  while (!f.atEnd()) {
    A key;
    B value;
    f.read(reinterpret_cast<char*>(&key), sizeof(key));
    f.read(reinterpret_cast<char*>(&value), sizeof(value));
    map[key] = value;
  }
}

/* Directory scanning and indexing
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

#include "media.h"

/// settings to control scanning/indexing
class IndexParams {
 public:
  /// indexable types
  /// @note to speed up scanning, types can be disabled
  enum { TypeImage = 1, TypeVideo = 2, TypeAudio = 4, TypeAll = 0xF };

  int algos = 0xFFFFFFF;        // enabled search algorithms
  int types = TypeAll;          // enabled media types
  bool recursive = true;        // scan subdirs
  bool autocrop = true;         // detect and crop borders prior to processing
  int minFileSize = 1024;       // ignore files < x bytes
  int numFeatures = 400;        // max number of features to store
  int resizeLongestSide = 400;  // dimension for rescale prior to processing
  bool retainImage = false;     // retain the decompressed image
  bool retainData = false;      // retain the compressed image data
  bool useHardwareDec = false;  // try hardware decoder for supported formats
  int decoderThreads = 0;  // threads per item decoder (hardwaredec always == 1)
  int indexThreads = 0;    // total max threads (cpu) <=0 means auto detect
  int gpuThreads = 1;      // number of parallel hardware decoders
  int videoThreshold = 8;  // dct threshold for skipping similar nearby frames
  // int resizeFilter;          // filter for resizing
  int writeBatchSize = 1024;     // size of item batch when writing to database
  bool estimateCost = true;      // estimate indexing cost to schedule jobs better
  bool showUnsupported = false;  // show unsupported filetype errors
  bool dryRun = false;           // scan for changes but do not process
  bool followSymlinks = false;   // follow symlinks to files/dirs
};

/// Stores result of image/video processing, prior to saving
class IndexResult {
 public:
  bool ok = false;
  QString path;
  Media media;
  VideoContext* context = nullptr;
};

class VideoProcess;

/// Finds candidate files and processes
class Scanner : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(Scanner)

 public:
  // wip common error conditions for errors()
  static constexpr const char* ErrorJpegTruncated = "truncated jpeg";
  static constexpr const char* ErrorOpen = "open error";
  static constexpr const char* ErrorLoad = "format error";
  static constexpr const char* ErrorTooSmall = "skip small file";
  static constexpr const char* ErrorUnsupported = "unsupported file type";

  /**
   * error list writable by worker threads
   * @note use staticMutex() to protect while reading
   */
  static QMap<QString, QStringList>* errors();

  /// mutex for static members/functions
  static QMutex* staticMutex();

  Scanner();
  virtual ~Scanner();

  /**
   * checksum for exact duplicate checks
   * @note could be different than raw sum (ideally metadata is not hashed)
   * @note exif section of jpg is not hashed (only the content)
   */
  static QString hash(const QString& path, int type, qint64* bytesRead=nullptr);

  void setIndexParams(const IndexParams& params) { _params = params; }
  const IndexParams& indexParams() const { return _params; }

  /// image file extensions we will try to process
  const QStringList imageTypes() const { return _imageTypes.values(); }

  /// video file extensions we will try to process
  const QStringList& videoTypes() const { return _videoTypes; }

  /// compressed archive extensions to search for images
  const QStringList& archiveTypes() const { return _archiveTypes; }

  /**
   * search directory and subdirectories for newly added or removed media
   * @param dir Directory to scan
   * @param [in] expected files to see (from a previous scan),
   *        [out] list of what the scanner did not see (removed files)
   *
   * @note Connect signals to get the results of the scan
   */
  void scanDirectory(const QString& dir, QSet<QString>& expected);

  /**
   * process compressed image
   * @param path file path or url
   * @param bytes raw compressed data, if empty, read from path
   * @return media object containing index structures and the decompressed data
   **/
  IndexResult processImageFile(const QString& path,
                               const QByteArray& bytes = QByteArray()) const;

  /// process decompressed image
  IndexResult processImage(const QString& path, const QString& digest,
                           const QImage& qImg) const;

  /// process video
  IndexResult processVideoFile(const QString& path) const;

  /**
   * empty queues, cancel work, spin until empty (QEventLoop::exec())
   * @note only necessary if it is required to destruct scanner immediately
   */
  void flush(bool wait = true);

  /**
   * spin until all queues/work processed (QEventLoop::exec())
   * @note not necessary to call unless app can't use scanCompleted()
   */
  void finish();

  /// return part of jpeg file excluding exif data (for checksum)
  static QByteArray jpegPayload(const QByteArray& bytes);

  /**
   * determine if a buffer is (valid) jpeg or not
   * @param bytes buffer to check
   * @param path origin of bytes for error reporting
   * @note a truncated jpeg would return false (eof marker missing)
   */
  static bool findJpegMarker(const QByteArray& bytes, const QString& path);

 Q_SIGNALS:
  /**
   * emit when a new file was processed
   * @param m the media with index structures filled
   */
  void mediaProcessed(const Media& m);

  /**
   * emit when all items of a type (e.g. images) have been sent
   * @param mediaType
   */
  void typeCompleted(int mediaType);

  /// emit when scanning finished or was flushed
  void scanCompleted();

 private Q_SLOTS:
  // remove one item from queue and process; if there is still more to process,
  // schedule the next call to processOne()
  void processOne();

  // called when a QFuture<Media> finishes processing,
  // at which point we call fileAdded() and remove it from _work
  void processFinished();

  // prepare video to process in the main thread
  VideoContext* initVideoProcess(const QString& path, bool tryGpu,
                                 int cpuThreads) const;

  // process video (in a thread)
  IndexResult processVideo(VideoContext* video) const;

 private:
  void readDirectory(const QString& dir, QSet<QString>& expected);
  void readArchive(const QString& path, QSet<QString>& expected);
  void scanProgress(const QString& path) const;

  bool isQueued(const QString& path) const { return _queuedWork.contains(path); } //_imageQueue.contains(path) || _videoQueue.contains(path); }

  static void setError(const QString& path, const QString& error);

  IndexParams _params;
  QSet<QString> _imageTypes;
  QStringList _videoTypes;
  QStringList _jpegTypes;
  QStringList _archiveTypes;

  QList<QFutureWatcher<IndexResult>*> _work; // qtconcurrent-issued jobs

  // jobs exist in (only) one of these lists
  QSet<QString> _activeWork;    // qtconcurrent-issued
  QStringList _videoQueue;      // waiting video jobs
  QStringList _imageQueue;      // waiting image jobs

  // additional set for fast lookup
  QSet<QString> _queuedWork;

  QTimer _timer;

  // separate pools to manage number of threads used
  QThreadPool _gpuPool;
  QThreadPool _videoPool;
  //QThreadPool _imagePool;

  QString _topDirPath;
  int _existingFiles, _ignoredFiles;
};

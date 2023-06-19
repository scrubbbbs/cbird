/* Video decoding and metadata
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

namespace cv {
class Mat;
}

extern "C" {
struct AVCodecContext;
typedef int (*AvExec2Callback)(AVCodecContext*, void*, int, int);
};

class VideoContextPrivate;

/// Video decoding
class VideoContext {
 public:

  struct Metadata {
    bool isEmpty;
    QSize frameSize;
    float frameRate;
    QString title;
    QString videoCodec;
    QString audioCodec;
    int videoBitrate, audioBitrate;
    int sampleRate, channels;
    int duration;
    QDateTime creationTime;
    QString pixelFormat; // only valid after nextFrame()

    Metadata() {
      isEmpty = true;
      frameRate = 0.0f;
      videoBitrate = audioBitrate = sampleRate = channels = duration = 0;
    }

    /// @return if styled, return html for WidgetHelper::drawRichText
    QString toString(bool styled = false) const;

    QTime timeDuration() const { return QTime(0, 0).addSecs(duration); }
  };

  struct DecodeOptions {
    int maxW = 0;       // maximum frame w/h (downscale, both must be given)
    int maxH = 0;
    bool gray = false;  // color or yuv/grayscale output
    bool fast = false;  // faster but lower quality, maybe suitable for indexing

    int threads = 1;      // max # of threads
    bool gpu = false;     // try gpu decoding
    int deviceIndex = 0;  // gpu device index

    DecodeOptions();
  };

  /**
   * get a single frame
   * @param path file location
   * @param frame frame number (-1 means automatic)
   * @param fastSeek less accurate but faster seeking
   * @param decoder options
   * @param future if non-null use for cancellation
   * @return
   */
  static QImage frameGrab(const QString& path, int frame = -1, bool fastSeek=false,
                          const DecodeOptions& options = DecodeOptions(),
                          QFuture<void>* future = nullptr);

  /**
   * read file metadata
   * @param keys List of key names
   * @return list of key values
   */
  static QVariantList readMetaData(const QString& path,
                                   const QStringList& keys);

  /// initialize FFmpeg, once per session
  static void loadLibrary();

  /// get the compiled/runtime ffmpeg versions
  static QStringList ffVersions();

  /// list available formats/codecs
  static void listFormats();

  VideoContext();
  ~VideoContext();

  /**
   * open video for decoding
   * @return 0 if no error, <0 if error
   */
  int open(const QString& path, const DecodeOptions& opt=DecodeOptions());
  void close();

  /// seek by decoding every frame; painfully slow but reliable
  bool seekDumb(int frame);

  /// seek with one call to avcodec seek function, fast but often inaccurate
  bool seekFast(int frame);

  /**
   * accurate seek, seek to nearest I-frame and decoded frames to the target
   * @param decoded optionally store otherwise discarded frames,
   *                in order before the target
   * @param maxDecoded [in] max number of frames to store
   *                   [out] number of frames actually decoded
   */
  bool seek(int frame,
            QVector<QImage>* decoded = nullptr, int* maxDecoded=nullptr);

  /**
   * get the next frame available
   * @note overwrites the image pixels in-place if possible
   */
  bool nextFrame(QImage& imgOut);
  bool nextFrame(cv::Mat& outImg);

  QString path() const { return _path; }

  /// display aspect ratio
  /// @note only valid after nextFrame()
  float pixelAspectRatio() const;

  /// @note only valid after open()
  const Metadata& metadata() const { return _metadata; }

  int width() const { return metadata().frameSize.width(); }
  int height() const { return metadata().frameSize.height(); }
  float fps() const { return metadata().frameRate; }

  bool isHardware() const { return _isHardware; }
  int deviceIndex() const { return _deviceIndex; }
  int threadCount() const { return _numThreads; }

  /// @note only public for benchmarking
  bool decodeFrame();

 private:
  bool readPacket();
  bool convertFrame(int& w, int& h, int& fmt);
  void frameToQImg(QImage& img);
  int ptsToFrame(int64_t pts) const;
  int64_t frameToPts(int frame) const;

  static QHash<void*, QString>& pointerToFileName();
  static QMutex* avLogMutex();
  static void avLogger(void* ptr, int level, const char* fmt, va_list vl);
  static void avLoggerSetFileName(void* ptr, const QString& name);
  static void avLoggerUnsetFileName(void* ptr);

  QString _path;
  VideoContextPrivate* _p;
  int _videoFrameCount;
  bool _isKey;
  int _consumed;
  QMutex _mutex;
  int _errorCount;
  Metadata _metadata;
  int64_t _firstPts;       // pts of first frame for accurate seek
  int _deviceIndex;        // device index of the decoder
  bool _isHardware;        // using hardware codec
  bool _isHardwareScaled;  // hardware codec also does the scaling
  bool _eof;               // true when eof on input
  int _numThreads;         // max number of threads for decoding
  VideoContext::DecodeOptions _opt;

  const int _MAX_DUMBSEEK_FRAMES = 10000; // do not seek if there are too many
};

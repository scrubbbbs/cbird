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

    Metadata() {
      isEmpty = true;
      frameRate = 0.0f;
      videoBitrate = audioBitrate = sampleRate = channels = duration = 0;
    }

    /// @return if styled, string for WidgetHelper::drawRichText
    QString toString(bool styled = false) const {
      QString fmt;
      if (styled)
        fmt =
            "<span class=\"time\">%1</span> "
            "<span class=\"video\">%2fps %3 @ %4k</span> "
            "<span class=\"audio\">%5khz %6ch %7 @ %8k</span>";
      else
        fmt = "%1 %2fps %3 @ %4k / %5khz %6ch %7 @ %8k";

      return QString(fmt)
          .arg(timeDuration().toString("mm:ss"))
          .arg(double(frameRate))
          .arg(videoCodec)
          .arg(videoBitrate / 1000)
          .arg(sampleRate / 1000)
          .arg(channels)
          .arg(audioCodec)
          .arg(audioBitrate / 1000);
    }

    QTime timeDuration() const { return QTime(0, 0).addSecs(duration); }
  };

  struct DecodeOptions {
    int maxW;  // maximum frame w/h (forces rescale)
    int maxH;
    bool rgb;   // color or yuv/grayscale output
    bool fast;  // faster but lower quality, maybe suitable for indexing

    int threads;      // max # of threads
    bool gpu;         // try gpu decoding
    int deviceIndex;  // gpu device index

    DecodeOptions()
        : maxW(0),
          maxH(0),
          rgb(false),
          fast(false),
          threads(1),
          gpu(false),
          deviceIndex(0) {}
  };

  /**
   * get a single frame
   * @param path file location
   * @param frame frame number (-1 means automatic)
   * @param fast less accurate but faster seeking
   * @return
   */
  static QImage frameGrab(const QString& path, int frame = -1,
                          bool fast = false);

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

  VideoContext();
  ~VideoContext();

  /**
   * open video for decoding
   * @return 0 if no error, <0 if error
   */
  int open(const QString& path, const DecodeOptions& opt);
  void close();

  /// seek by decoding every frame; painfully slow but reliable
  bool seekDumb(int frame);

  /// seek with one call to avcodec seek function, fast but often inaccurate
  bool seekFast(int frame);

  /**
   * accurate seek, seekFast() then seekDumb() until we hit the target
   * @param opt     options for frame decoding when saving frames
   * @param decoded optionally store frames between seek point and target
   */
  bool seek(int frame, const DecodeOptions& opt = DecodeOptions(),
            QVector<QImage>* decoded = nullptr);

  /**
   * get the next frame available
   * @note overwrites the image pixels in-place if possible
   */
  bool nextFrame(QImage& imgOut);
  bool nextFrame(cv::Mat& outImg);

  QString path() const { return _path; }

  /// display aspect ratio
  /// @note only valid after nextFrame()
  float aspect() const;

  /// @note only valid after open()
  const Metadata& metadata() const { return _metadata; }

  int width() const { return metadata().frameSize.width(); }
  int height() const { return metadata().frameSize.height(); }
  float fps() const { return metadata().frameRate; }

  bool isHardware() const { return _isHardware; }
  int deviceIndex() const { return _deviceIndex; }
  int threadCount() const { return _numThreads; }

  bool decodeFrame();

 private:
  bool readPacket();
  bool convertFrame(int& w, int& h, int& fmt);

  static QHash<void*, QString>& pointerToFileName();
  static QMutex* avLogMutex();
  static void avLogger(void* ptr, int level, const char* fmt, va_list vl);
  static void avLoggerSetFileName(void* ptr, const QString& name);
  static void avLoggerUnsetFileName(void* ptr);

//  static int avExecute(struct AVCodecContext* c,
//                       int (*func)(struct AVCodecContext* c2, void* arg),
//                       void* arg2, int* ret, int count, int size);
//  static int avExecute2(struct AVCodecContext* c, AvExec2Callback func,
//                        void* arg2, int* ret, int count);

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
  bool _flush;             // true when eof on input
  int _numThreads;         // max number of threads for decoding
  VideoContext::DecodeOptions _opt;
};

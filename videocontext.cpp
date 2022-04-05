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
#include "videocontext.h"
#include "qtutil.h" // message context

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
};

#include "opencv2/imgproc/imgproc.hpp"

#include <unistd.h> // getcwd

#define AV_CRITICAL(x) \
  qCritical() << x << Qt::hex << err << avErrorString(err)
#define AV_WARNING(x) \
  qWarning() << x
#define AV_DEBUG(x) \
  qDebug() << x

static QString avErrorString(int err) {
  char str[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_make_error_string(str, sizeof(str), err);
  return str;
}

static void avImgToQImg(uint8_t *planes[4], int linesizes[4], int width,
                        int height, QImage& dst, AVPixelFormat fmt = AV_PIX_FMT_YUV420P) {

  const QSize size(width, height);
  const uchar* const data = planes[0];
  int skip = linesizes[0];

  if (fmt != AV_PIX_FMT_BGR24) {
    QImage::Format format = QImage::Format_Grayscale8;

    if (dst.size() != size || dst.format() != format)
      dst = QImage(size, format);

    for (int y = 0; y < height; y++) {
      uchar* dstLine = dst.scanLine(y);
      for (int x = 0; x < width; x++) {
        *dstLine = *(data + y * skip + x);
        dstLine++;
      }
    }
  } else {
    QImage::Format format = QImage::QImage::Format_RGB888;

    if (dst.size() != size || dst.format() != format)
      dst = QImage(size, format);

    for (int y = 0; y < height; y++) {
      uchar* dstLine = dst.scanLine(y);

      for (int x = 0; x < width; x++) {
        const uchar* const pixel = data + y * skip + x * 3;
        dstLine[0] = pixel[2];
        dstLine[1] = pixel[1];
        dstLine[2] = pixel[0];
        dstLine += 3;
      }
    }
  }
}

static void avImgToCvImg(uint8_t* planes[4], int linesizes[4], int width,
                         int height, cv::Mat& dst,
                         AVPixelFormat fmt = AV_PIX_FMT_YUV420P) {
  const QSize size(width, height);
  const uchar* const data = planes[0];
  int skip = linesizes[0];

  if (fmt != AV_PIX_FMT_BGR24) {
    if (dst.rows != height || dst.cols != width || dst.type() != CV_8UC(1))
      dst = cv::Mat(height, width, CV_8UC(1));

    for (int y = 0; y < height; y++) {
      uchar* dstLine = dst.ptr(y);
      memcpy(dstLine, data + y * skip, size_t(width));
    }
  } else {
    if (dst.rows != height || dst.cols != width || dst.type() != CV_8UC(3))
      dst = cv::Mat(height, width, CV_8UC(3));

    for (int y = 0; y < height; y++) {
      uchar* dstLine = dst.ptr(y);
      memcpy(dstLine, data + y * skip, size_t(width * 3));
    }
  }
}

static void avFrameToQImg(const AVFrame& frame, QImage& dst) {
  int w = frame.width, h = frame.height, skip = frame.linesize[0];
  const uchar* const data = frame.data[0];

  const QSize size(frame.width, frame.height);

  QImage::Format format = QImage::Format_Grayscale8;

  if (dst.size() != size || dst.format() != format) dst = QImage(size, format);

  for (int y = 0; y < h; y++) {
    uchar* dstLine = dst.scanLine(y);
    for (int x = 0; x < w; x++) {
      *dstLine = *(data + y * skip + x);
      dstLine++;
    }
  }
}

static void avFrameToCvImg(const AVFrame& frame, cv::Mat& dst) {
  int w = frame.width, h = frame.height, skip = frame.linesize[0];
  const uchar* const data = frame.data[0];

  if (dst.rows != h || dst.cols != w || dst.type() != CV_8UC(1))
    dst = cv::Mat(h, w, CV_8UC(1));

  for (int y = 0; y < h; y++) {
    uchar* dstLine = dst.ptr(y);
    for (int x = 0; x < w; x++) {
      *dstLine = *(data + y * skip + x);
      dstLine++;
    }
  }
}

void VideoContext::loadLibrary() {
  av_register_all();
  av_log_set_callback(&avLogger);
}

QStringList VideoContext::ffVersions() {
  QStringList list;
  const char* compiled = AV_STRINGIFY(LIBAVUTIL_VERSION);

  unsigned int packed = avutil_version();
  QString runtime = QString("%1.%2.%3")
                        .arg(AV_VERSION_MAJOR(packed))
                        .arg(AV_VERSION_MINOR(packed))
                        .arg(AV_VERSION_MICRO(packed));

  list << runtime << compiled;
  return list;
}

class VideoContextPrivate {
 public:
  VideoContextPrivate() { init(); }

  ~VideoContextPrivate() {
    // note: stuff gets free'd in VideoContext::close()
  }

  void init() {
    format = nullptr;
    context = nullptr;
    codec = nullptr;
    frame = nullptr;
    videoStream = nullptr;
    scaler = nullptr;
    sar = -1;
    memset(&scaled, 0, sizeof(scaled));
  }

  AVFormatContext* format;
  AVCodecContext* context;
  AVCodec* codec;
  AVFrame* frame;
  AVStream* videoStream;
  AVPacket packet;
  float sar;

  SwsContext* scaler;
  struct {
    uint8_t *data[4];
    int     linesize[4];
  } scaled;
};

QImage VideoContext::frameGrab(const QString& path, int frame, bool fast) {
  VideoContext::DecodeOptions options;
  options.rgb = true;
  options.threads = QThread::idealThreadCount();
  options.fast = true;
  QImage img;

  // note: hardware decoder is much slower to open, not worthwhile
  // todo: system configuration for grab location
  VideoContext video;
  if (0 == video.open(path, options)) {
    const auto md = video.metadata();
    const int maxFrame = int(md.frameRate * float(md.duration));
    if (frame >= maxFrame) {
      qWarning() << path << ": seek frame out of range :" << frame << ", using auto";
      frame = -1;
    }
    if (frame < 0) {
      if (md.duration > 60)
        frame = int(60 * md.frameRate);
      else
        frame = int(float(md.duration) * md.frameRate * 0.10f);
    }

    bool ok = false;
    if (fast)
      ok = video.seekFast(frame);
    else
      ok = video.seek(frame);

    if (ok) video.nextFrame(img);
  }
  return img;
}

VideoContext::VideoContext() {
  _p = new VideoContextPrivate;
  _videoFrameCount = 0;
  _consumed = 0;
  _errorCount = 0;
  _deviceIndex = -1;
  _isHardware = false;
  _eof = false;
  _numThreads = 1;
}

VideoContext::~VideoContext() {
  if (_p->context) close();
  delete _p;
}

int VideoContext::open(const QString& path, const DecodeOptions& opt) {
  _opt = opt;
  _errorCount = 0;
  _firstPts = AV_NOPTS_VALUE;
  _deviceIndex = -1;
  _isHardware = false;
  _eof = false;
  av_init_packet(&_p->packet);

  QMutexLocker locker(&_mutex);

  _path = path;

  _p->format = avformat_alloc_context();
  Q_ASSERT(_p->format);

  // set context to log source of errors
  const QString fileName = QFileInfo(_path).fileName();
  avLoggerSetFileName(_p->format, fileName);

  int err = 0;
  if ((err=avformat_open_input(&_p->format, qUtf8Printable(_path), nullptr, nullptr)) < 0) {
    AV_CRITICAL("cannot open input");
    avLoggerUnsetFileName(_p->format);
    return -1;
  }

  // firstPts is needed for seeking
  // read a few packets to find it
  _p->format->flags |= AVFMT_FLAG_GENPTS;
  for (int i = 0; i < 5;) {
    if (0 > av_read_frame(_p->format, &_p->packet)) break;

    const auto* stream = _p->format->streams[_p->packet.stream_index];
    if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) continue;
    if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) continue;

    // todo: if this is reliable we don't need this loop...
//    if (stream->start_time != AV_NOPTS_VALUE) {
//      _firstPts = stream->start_time;
//      qDebug() << "stream start_time" << _firstPts;
//      break;
//    }

    if (_firstPts == AV_NOPTS_VALUE)
      _firstPts = _p->packet.pts;
    else
      _firstPts = std::min(_firstPts, _p->packet.pts);
    i++;

    av_packet_unref(&_p->packet);
  }

  avformat_close_input(&_p->format);
  avLoggerUnsetFileName(_p->format);

  err=0;
  if (_firstPts == AV_NOPTS_VALUE) {
    AV_CRITICAL("no first PTS was found");
    return -1;
  }

  _p->format = avformat_alloc_context();
  Q_ASSERT(_p->format);
  avLoggerSetFileName(_p->format, fileName);

  if ((err=avformat_open_input(&_p->format, qUtf8Printable(_path), nullptr, nullptr)) <
      0) {
    AV_CRITICAL("cannot reopen input");
    avLoggerUnsetFileName(_p->format);
    return -1;
  }

  avLoggerSetFileName(_p->format, fileName);

  if ((err=avformat_find_stream_info(_p->format, nullptr)) < 0) {
    AV_CRITICAL("cannot find stream info");
    avLoggerUnsetFileName(_p->format);
    return -2;
  }

  // av_dump_p->format(fmt, 0, NULL, 0);
  int videoStreamIndex = -1;
  int audioStreamIndex = -1;
  for (unsigned int i = 0; i < _p->format->nb_streams; i++) {
    AVStream* stream = _p->format->streams[i];
    const AVCodecParameters* codec = stream->codecpar;

    if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
      if (stream->disposition &  AV_DISPOSITION_ATTACHED_PIC) continue;
      if (videoStreamIndex >= 0) continue;

      videoStreamIndex = int(i);
      _metadata.isEmpty = false;

      AVRational fps = stream->r_frame_rate;
      _metadata.frameRate = float(av_q2d(fps));
      _metadata.frameSize = QSize(codec->width, codec->height);
      _metadata.videoBitrate = int(codec->bit_rate);

      AVCodec* vCodec = avcodec_find_decoder(codec->codec_id);
      if (vCodec) _metadata.videoCodec = vCodec->name;

    } else if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
      if (audioStreamIndex >= 0) continue;
      audioStreamIndex = int(i);
      _metadata.isEmpty = false;
      _metadata.audioBitrate = int(codec->bit_rate);
      _metadata.sampleRate = codec->sample_rate;
      _metadata.channels = codec->channels;

      AVCodec* aCodec = avcodec_find_decoder(codec->codec_id);
      if (aCodec) _metadata.audioCodec = aCodec->name;
    }
  }

  _metadata.duration = int(_p->format->duration / AV_TIME_BASE);

  if (_p->format->metadata) {
    AVDictionaryEntry* metaTitle =
        av_dict_get(_p->format->metadata, "title", nullptr, 0);
    if (metaTitle) _metadata.title = metaTitle->value;

    AVDictionaryEntry* metaTime =
        av_dict_get(_p->format->metadata, "creation_time", nullptr, 0);
    if (metaTime) {
      _metadata.creationTime =
          QDateTime::fromString(metaTime->value, "yyyy-MM-ddThh:mm:s.zzzzzzZ");
    }
  }

  err=0;
  if (videoStreamIndex < 0) {
    AV_CRITICAL("cannot find video stream");
    avLoggerUnsetFileName(_p->format);
    return -3;
  }

  _p->videoStream = _p->format->streams[videoStreamIndex];
  _p->format->flags |= AVFMT_FLAG_GENPTS;
  _p->context = _p->videoStream->codec;

  avLoggerSetFileName(_p->context, fileName);

  // the actual format support doesn't seem to be published by ffmpeg
  // the codec says only nv12, p010le, p016le, but yuv420 works fine
  // todo: system configuration
  bool tryHardware = opt.gpu;
  if (tryHardware) {
    constexpr AVPixelFormat hwFormats[] = {AV_PIX_FMT_YUV420P,
                                           AV_PIX_FMT_YUV440P,
                                           AV_PIX_FMT_NV12,
                                           AV_PIX_FMT_NONE};
    bool supported = false;
    const AVPixelFormat* fmt = hwFormats;
    while (*fmt != AV_PIX_FMT_NONE)
      if (*fmt++ == _p->context->pix_fmt) supported = true;

    if (!supported) {
      qDebug() << "unsupported pixel format for hw codec :"
               << av_pix_fmt_desc_get(_p->context->pix_fmt)->name;
      tryHardware = false;
    }
  }

  if (tryHardware) {
    // todo: multiple device support
    // todo: system configuration
    constexpr struct {
      AVCodecID id;
      int flag;
      const char* name;
    } codecs[] = {{AV_CODEC_ID_H264, 0, "h264_cuvid"},
                  {AV_CODEC_ID_HEVC, 0, "hevc_cuvid"},
                  {AV_CODEC_ID_MPEG4, 0, "mpeg4_cuvid"},
                  {AV_CODEC_ID_VC1, 0, "vc1_cuvid"},
                  {AV_CODEC_ID_VP8, 0, "vp8_cuvid"},
                  {AV_CODEC_ID_VP9, 0, "vp9_cuvid"},
                  {AV_CODEC_ID_NONE, 0, nullptr}};

    for (int i = 0; codecs[i].id; i++)
      if (codecs[i].id == _p->context->codec_id) {
        qDebug() << "trying hw codec :" << codecs[i].name;
        _p->codec = avcodec_find_decoder_by_name(codecs[i].name);
        break;
      }
  }

  AVDictionary* codecOptions = nullptr;
  if (!_p->codec) {
    if (tryHardware) AV_DEBUG("no hw codec, trying sw");

    _p->codec = avcodec_find_decoder(_p->context->codec_id);
    if (!_p->codec) {
      AV_WARNING("no codec found");
      avLoggerUnsetFileName(_p->format);
      avLoggerUnsetFileName(_p->context);
      return -4;
    }
  } else {
    _isHardware = true;
    _deviceIndex = opt.deviceIndex;
    if (opt.maxW > 0 && opt.maxW == opt.maxH &&
        QString(_p->codec->name).endsWith("cuvid")) {
      // enable hardware scaler
      QString size = QString("%1x%2").arg(opt.maxW).arg(opt.maxH);
      qInfo() << "using gpu scaler@" << size;
      av_dict_set(&codecOptions, "resize", qPrintable(size), 0);
      _isHardwareScaled = true;
    }
  }

  if (opt.fast) {
    // it seems safe to enable this, about 20% boost.
    // the downscaler will smooth out any artifacts
    av_dict_set(&codecOptions, "skip_loop_filter", "all", 0);

    // grayscale decoding is not built-in to ffmpeg usually,
    // performance improvement is 5-10%
    //if (!opt.rgb)
    // av_dict_set(&codecOptions, "flags", "gray", 0);
  }

  // if (_p->codec->capabilities & CODEC_CAP_TRUNCATED)
  //    _p->context->flags|= CODEC_FLAG_TRUNCATED; // we do not send complete
  //    frames

  _numThreads = 1;
  if (!_isHardware && _p->codec->capabilities & (AV_CODEC_CAP_FRAME_THREADS |
                                                 AV_CODEC_CAP_SLICE_THREADS))
    _numThreads = opt.threads;

  // note: no need to set thread_type, let ffmpeg choose the best options
  if (_numThreads > 0) {
    _p->context->thread_count = _numThreads;
    //        _p->context->thread_type = 0;// FF_THREAD_FRAME|FF_THREAD_SLICE;
    //        _p->context->execute = avExecute;
    //        _p->context->execute2 = avExecute2;
  }

  if ((err=avcodec_open2(_p->context, _p->codec, &codecOptions)) < 0) {
    if (_isHardware) {
      AV_WARNING("could not open hardware codec, trying software");
      _isHardware = false;
      _p->codec = avcodec_find_decoder(_p->context->codec_id);
      if (!_p->codec) {
        AV_WARNING("no codec found");
        avLoggerUnsetFileName(_p->format);
        avLoggerUnsetFileName(_p->context);
        return -4;
      }

      if ((err=avcodec_open2(_p->context, _p->codec, nullptr)) < 0) {
        AV_CRITICAL("could not open fallback codec");
        avLoggerUnsetFileName(_p->format);
        avLoggerUnsetFileName(_p->context);
        return -5;
      }
    } else {
      AV_CRITICAL("could not open codec");
      avLoggerUnsetFileName(_p->format);
      avLoggerUnsetFileName(_p->context);
      return -5;
    }
  }

  qDebug("%s using %d threads (requested %d) threads=%s",
         _p->codec->name,
         _p->context->thread_count, _numThreads,
         _p->context->active_thread_type == FF_THREAD_FRAME ? "frame" : "slice");

  _p->frame = av_frame_alloc();
  if (!_p->frame) {
    err=0;
    AV_CRITICAL("could not allocate video frame");
    avLoggerUnsetFileName(_p->format);
    avLoggerUnsetFileName(_p->context);
    return -6;
  }

  _videoFrameCount = 0;

  if (_p->videoStream->nb_frames == 0)
    _p->videoStream->nb_frames =
        long(_p->videoStream->duration * av_q2d(_p->videoStream->time_base) /
             av_q2d(_p->videoStream->r_frame_rate));

  av_init_packet(&_p->packet);
  _p->packet.size = 0;
  _p->packet.data = nullptr;

  return 0;
}

void VideoContext::close() {
  _errorCount = 0;
  _videoFrameCount = 0;
  _numThreads = 1;
  _isHardware = false;

  QMutexLocker locker(&_mutex);

  avformat_close_input(&_p->format);

  avLoggerUnsetFileName(_p->format);
  avLoggerUnsetFileName(_p->context);

  // avcodec_close(_p->context);
  // av_free(_p->context);
  av_frame_free(&_p->frame);

  avLoggerUnsetFileName(_p->scaler);
  if (_p->scaler) sws_freeContext(_p->scaler);

  if (_p->scaled.data[0]) av_freep( &(_p->scaled.data[0]) );

  // av_free_p->packet(&_p->packet);
  _p->init();
}

int VideoContext::ptsToFrame(int64_t pts) const {
  auto timeBase = av_q2d(_p->videoStream->time_base);
  auto frameRate = av_q2d(_p->videoStream->r_frame_rate);
  return floor((pts - _firstPts) * timeBase * frameRate + 0.5);
}

int64_t VideoContext::frameToPts(int frame) const {
  auto timeBase = av_q2d(_p->videoStream->time_base);
  auto frameRate = av_q2d(_p->videoStream->r_frame_rate);
  return floor(frame / frameRate / timeBase + 0.5) + _firstPts;
}

bool VideoContext::seekDumb(int frame) {
  AV_WARNING("seek dumb decodes all frames");
  QImage tmp;
  while (frame--)
    if (!decodeFrame()) return false;
  return true;
}

bool VideoContext::seekFast(int frame) {
  if (frame <= 0) return true;

  const int64_t target = frameToPts(frame);

  qDebug("frame=%d pts=(%" PRIi64 ")", frame, target);

  int err = av_seek_frame(_p->format, _p->videoStream->index, target,
                          AVSEEK_FLAG_BACKWARD);
  if (err < 0) {
    AV_WARNING("seek error") << err;
    return false;
  }

  avcodec_flush_buffers(_p->videoStream->codec);

  _eof = false;

  return true;
}

bool VideoContext::seek(int frame, QVector<QImage>* decoded, int* maxDecoded) {

  const double frameDuration = 1.0 / av_q2d(_p->videoStream->time_base);
  const int64_t target = frameToPts(frame);

  qDebug("frame=%d pts=(%" PRIi64 ") pts=(%" PRIi64 ")", frame, target, target-_firstPts);

  int seekedFrame = 0; // frame number we actually seeked to

  if (target > _firstPts) { // do not seek before first pts; decode frames instead
    bool isKeyframe = false;
    int64_t seekTime = target;
    int tries = 0;

    // seek <= where we want to go, and try to get a keyframe
    // if we don't get a keyframe, or we overshot seek point,
    // back up and try again
    do {
      int err = av_seek_frame(_p->format, _p->videoStream->index, seekTime,
                              AVSEEK_FLAG_BACKWARD);
      if (err < 0) {
        qCritical("av_seek_frame error %d", err);
        return false;
      }
      Q_ASSERT(_p->videoStream->codec == _p->context);
      avcodec_flush_buffers(_p->context);
      _eof = false;

      // check for keyframe (stream corruption, or eos?)
      do {
        if (!readPacket()) break;

        Q_ASSERT(_p->packet.pts != AV_NOPTS_VALUE);

        isKeyframe = _p->packet.flags & AV_PKT_FLAG_KEY;
        //if (!isKeyframe)
        //  AV_WARNING("miss: seek to non-keyframe");

        err = avcodec_send_packet(_p->context, &_p->packet);
        if (err != 0) {
          AV_WARNING("miss: send_packet error") << Qt::hex << err;
          break;
        }

      } while (!isKeyframe && _p->packet.pts < target);

      tries++;
      if (!isKeyframe || _p->packet.pts > target) {
        AV_WARNING("try:") << tries << "key:" << isKeyframe << "dist:" << seekTime - _p->packet.pts;

        // guess the next time try; if we back up too much we pay the price
        // of decoding a lot of frames; not enough and we never get there...

        // back up half the amount we missed, plus one frame
        seekTime = (seekTime + (seekTime - _p->packet.pts)/2.0 - frameDuration);

        if (tries > 10) {
          AV_WARNING("failed after 10 attempts, seeking dumb");
          close();
          open(_path, _opt);
          seekDumb(frame);
          return true;
        }
      }

    } while (!isKeyframe || _p->packet.pts > target);

    seekedFrame = ptsToFrame(_p->packet.pts);
  }
  else {
    // to read frames from the start we have to reopen
    AV_WARNING("reopening stream for seek < first pts");
    close();
    if (0 != open(_path, _opt)) return false;
  }

  // accurate seek requires decoding some frames
  _consumed = 0;
  int framesLeft = frame - seekedFrame;
  if (framesLeft > 0)
    AV_DEBUG("decoding") << framesLeft << "interframes";

  if (maxDecoded)
    *maxDecoded = framesLeft;

  int i = 0;
  while (framesLeft--)
    if (decodeFrame()) {
      // store the intermediate frames, useful for backwards scrub
      if (decoded) {
        if (framesLeft < decoded->count()) {
          QImage& img = (*decoded)[ i++ ];
          frameToQImg(img);
        }
        else if (decoded->count()) {
          AV_WARNING("insufficient frames supplied" << decoded->count());
        }
      }
    } else {
      // no longer valid since we couldn't get the target
      if (decoded) decoded->clear();

      AV_WARNING("decode failed, giving up");
      return false;
    }

  return true;
}

bool VideoContext::readPacket() {
  do {
    av_packet_unref(&_p->packet);

    int err = av_read_frame(_p->format, &_p->packet);
    if (err < 0) {
      if (err != AVERROR_EOF)
        AV_CRITICAL("av_read_frame");
      _eof = true;
      return false;
    }

    if (_p->packet.flags & AV_PKT_FLAG_CORRUPT)
      AV_WARNING("corrupt packet");

  } while (_p->packet.stream_index != _p->videoStream->index);

  return true;
}

bool VideoContext::decodeFrame() {
  while (true) {
    int err = avcodec_receive_frame(_p->context, _p->frame);
    if (err == 0) return true;

    if (err == AVERROR_EOF) {
      AV_DEBUG("avcodec_receive_frame eof");
      break;
    } else if (err != AVERROR(EAGAIN)) {
      AV_CRITICAL("avcodec_receive_frame");
      break;
    }

    if (!_eof) {
      if (!readPacket()) {
        avcodec_send_packet(_p->context, nullptr);
        continue;
      }

      if (_p->packet.size == 0) {
        AV_CRITICAL("empty packet, giving up");
        break;
      }

      Q_ASSERT(_p->packet.stream_index == _p->videoStream->index);

      err = avcodec_send_packet(_p->context, &_p->packet);
      if (err != 0)
        AV_CRITICAL("avcodec_send_packet");
    }
    else {
      // attempt to prevent hang when seeking near the eof
      AV_WARNING("resending null packet, might hang here");
      avcodec_send_packet(_p->context, nullptr);
    }
  }

  return false;
}

QVariantList VideoContext::readMetaData(const QString& _path,
                                        const QStringList& keys) {
  AVFormatContext* format = nullptr;

  QVariantList values;
  for (int i = 0; i < keys.count(); ++i) values.append(QVariant());

  format = avformat_alloc_context();
  Q_ASSERT(format);

  const QString fileName = QFileInfo(_path).fileName();
  avLoggerSetFileName(format, fileName);
  int err = 0;
  if ((err = avformat_open_input(&format, qUtf8Printable(_path), nullptr, nullptr)) < 0) {
    AV_CRITICAL("cannot open input");
    avLoggerUnsetFileName(format);
    return values;
  }

  if (!format->metadata) {
    AV_WARNING("no metadata");
    avLoggerUnsetFileName(format);
    return values;
  }

  for (int i = 0; i < keys.count(); ++i) {
    AVDictionaryEntry* entry =
        av_dict_get(format->metadata, qPrintable(keys[i]), nullptr, 0);
    if (entry) values[i] = entry->value;
  }

  avformat_close_input(&format);
  avLoggerUnsetFileName(format);

  return values;
}

bool VideoContext::convertFrame(int& w, int& h, int& fmt) {
  if (_opt.rgb ||
      (_opt.maxW && _opt.maxH &&
       (_p->frame->width > _opt.maxW || _p->frame->height > _opt.maxH))) {
    w = _opt.maxW;
    h = _opt.maxH;

    if (!w || !h) {
      w = _p->frame->width;
      h = _p->frame->height;
    }

    fmt = AV_PIX_FMT_YUV420P;
    if (_opt.rgb) fmt = AV_PIX_FMT_BGR24;

    if (_p->scaler == nullptr) {
      if (!av_pix_fmt_desc_get(AVPixelFormat(_p->frame->format))) {
        int err = 0;
        AV_CRITICAL("invalid pixel format in AVFrame");
        return false;
      }

      // area filter seems the best for downscaling
      // - faster than bicubic with fewer artifacts
      // - less artifacts than bilinear
      int fastFilter = SWS_AREA;
      int fw = _p->frame->width;
      int fh = _p->frame->height;
      if (w > fw || h > fh) {
        if (fh == h) fastFilter = SWS_FAST_BILINEAR;  // bilinear in one direction
        else fastFilter = SWS_BILINEAR;
      }

      qDebug() << "scaling from:"
              << av_get_pix_fmt_name(AVPixelFormat(_p->frame->format))
              << QString("@%1x%2").arg(_p->frame->width).arg(_p->frame->height)
              << "to:" << av_get_pix_fmt_name(AVPixelFormat(fmt))
              << QString("@%1x%2").arg(w).arg(h)
               << "fast=" << _opt.fast << "fast-filter" << fastFilter;


      _p->scaler = sws_getContext(
          _p->frame->width, _p->frame->height, AVPixelFormat(_p->frame->format),
          w, h, AVPixelFormat(fmt), _opt.fast ? fastFilter : SWS_BICUBIC,
          nullptr, nullptr, nullptr);

      avLoggerSetFileName(_p->scaler, QFileInfo(_path).fileName());

      int size = av_image_alloc(_p->scaled.data, _p->scaled.linesize, w, h, AVPixelFormat(fmt), 16);
      if (size < 0)
        qFatal("av_image_alloc failed: %d", size);
    }

    sws_scale(_p->scaler, _p->frame->data, _p->frame->linesize, 0,
              _p->frame->height, _p->scaled.data, _p->scaled.linesize);

    return true;
  }

  return false;
}

QHash<void*, QString>& VideoContext::pointerToFileName() {
  static QHash<void*, QString> hash;
  return hash;
}

QMutex* VideoContext::avLogMutex() {
  static QMutex mutex;
  return &mutex;
}

void VideoContext::avLogger(void* ptr, int level, const char* fmt, va_list vl) {
  if (level > av_log_get_level()) return;

  QMutexLocker locker(avLogMutex());

  const auto& names = pointerToFileName();
  QString fileName;
  const auto it = names.find(ptr);
  if (it != names.end()) {
    fileName = it.value();
  } else {
    char path[PATH_MAX+1]={0};
    if (getcwd(path, sizeof(path)))
      fileName = QString("cwd={") + path + "}";
  }

  auto& msgCtx = qMessageContext();
  if (msgCtx.hasLocalData()) {
    QString ctx = msgCtx.localData();
    if (ctx == "")
      msgCtx.setLocalData(fileName);
  }
  else
    msgCtx.setLocalData(fileName);

  char buf[1024] = {0};
  vsnprintf(buf, sizeof(buf) - 1, fmt, vl);
  QString str = buf;
  QByteArray bytes = str.trimmed().toUtf8();
  const char* msg = bytes.data();

  //     if (level >= AV_LOG_TRACE) ;
  // else if (level >= AV_LOG_DEBUG) ;
  if (level >= AV_LOG_VERBOSE)
    qDebug() << msg;
  else if (level >= AV_LOG_INFO)
    qInfo() << msg;
  else if (level >= AV_LOG_WARNING)
    qWarning() << msg;
  // else if (level >= AV_LOG_ERROR);
  else if (level >= AV_LOG_FATAL)
    qCritical() << msg;
  else if (level >= AV_LOG_PANIC)
    qFatal("%s", msg);
}

void VideoContext::avLoggerSetFileName(void* ptr, const QString& name) {
  QMutexLocker locker(avLogMutex());
  pointerToFileName().insert(ptr, name);
}

void VideoContext::avLoggerUnsetFileName(void* ptr) {
  QMutexLocker locker(avLogMutex());
  pointerToFileName().remove(ptr);
}

void VideoContext::frameToQImg(QImage& img) {
  int w, h, fmt;

  if (convertFrame(w, h, fmt))
    avImgToQImg(_p->scaled.data, _p->scaled.linesize, w, h, img,
                AVPixelFormat(fmt));
  else
    avFrameToQImg(*(_p->frame), img);

  bool isKey = _p->frame->key_frame ||
               _p->frame->pict_type == AV_PICTURE_TYPE_I;

  img.setText("isKey", QString::number(isKey));

  auto conv = av_mul_q(_p->videoStream->time_base,
                                   _p->videoStream->r_frame_rate);
  auto pts = av_make_q(_p->frame->best_effort_timestamp, 1);
  int frameNumber = av_q2d(av_mul_q(pts, conv));

  img.setText("frame", QString::number(frameNumber));

  const char* formatName = av_get_pix_fmt_name(AVPixelFormat(_p->frame->format));
  if (formatName) {
    img.setText("format", formatName);
    _metadata.pixelFormat = formatName;
  }
}

bool VideoContext::nextFrame(QImage& outQImg) {
  bool gotFrame = decodeFrame();
  frameToQImg(outQImg);
  return gotFrame;
}

bool VideoContext::nextFrame(cv::Mat& outImg) {
  bool gotFrame = decodeFrame();

  int w, h, fmt;

  if (convertFrame(w, h, fmt))
    avImgToCvImg(_p->scaled.data, _p->scaled.linesize, w, h, outImg, AVPixelFormat(fmt));
  else
    avFrameToCvImg(*(_p->frame), outImg);

  return gotFrame;
}

float VideoContext::aspect() const {
  if (_p->sar > 0.0) return _p->sar;

  _p->sar = float(av_q2d(
      av_guess_sample_aspect_ratio(_p->format, _p->videoStream, _p->frame)));
  if (_p->sar == 0.0f) {
    AV_WARNING("unable to guess SAR, assuming 1.0");
    _p->sar = 1.0f;
  }
  return _p->sar;
}

QString VideoContext::Metadata::toString(bool styled) const {
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

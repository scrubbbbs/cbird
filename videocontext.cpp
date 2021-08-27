#include "videocontext.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
};

#include "opencv2/imgproc/imgproc.hpp"

#include <unistd.h> // getcwd

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

static void avImgToCvImg(uint8_t *planes[4], int linesizes[4], int width, int height,
                             cv::Mat& dst,
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

static int pts2frame(int64_t pts, int64_t firstPTS, double frameRate,
                     double timeBase) {
  return int(floor((pts - firstPTS) * timeBase * frameRate + 0.5));
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
    memset(&scaled, 0, sizeof(scaled));
  }

  AVFormatContext* format;
  AVCodecContext* context;
  AVCodec* codec;
  AVFrame* frame;
  AVStream* videoStream;
  AVPacket packet;

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

  QImage img;

  // note: hardware decoder is much slower to open
  // todo: system configuration for grab location
  VideoContext video;
  if (0 == video.open(path, options)) {
    const auto md = video.metadata();
    const int maxFrame = int(md.frameRate * float(md.duration));
    if (frame >= maxFrame) {
      qWarning() << path << ": seek frame out of range:" << frame << ", using auto";
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
  } else
    qWarning("VideoContext::grabFrame: failed to open: %s", qPrintable(path));

  return img;
}

VideoContext::VideoContext() {
  _p = new VideoContextPrivate;
  _videoFrameCount = 0;
  _consumed = 0;
  _errorCount = 0;
  _deviceIndex = -1;
  _isHardware = false;
  _flush = false;
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
  _flush = false;
  av_init_packet(&_p->packet);

  QMutexLocker locker(&_mutex);

  _path = path;

  _p->format = avformat_alloc_context();
  Q_ASSERT(_p->format);

  const QString fileName = QFileInfo(_path).fileName();
  avLoggerSetFileName(_p->format, fileName);

  if (avformat_open_input(&_p->format, qPrintable(_path), nullptr, nullptr) <
      0) {
    qCritical("%s: Cannot open input", qPrintable(_path));
    avLoggerUnsetFileName(_p->format);
    return -1;
  }

  // firstPts is needed for seeking
  // read a few frames, they could be out of order
  _p->format->flags |= AVFMT_FLAG_GENPTS;
  for (int i = 0; i < 5;) {
    if (0 > av_read_frame(_p->format, &_p->packet)) break;

    if (_p->format->streams[_p->packet.stream_index]->codec->codec_type ==
        AVMEDIA_TYPE_VIDEO) {
      if (_firstPts == AV_NOPTS_VALUE)
        _firstPts = _p->packet.pts;
      else
        _firstPts = std::min(_firstPts, _p->packet.pts);
      i++;
    }
    av_packet_unref(&_p->packet);
  }

  avformat_close_input(&_p->format);
  avLoggerUnsetFileName(_p->format);

  if (_firstPts == AV_NOPTS_VALUE) {
    qCritical("%s: No PTS was found", qPrintable(_path));
    return -1;
  }

  _p->format = avformat_alloc_context();
  Q_ASSERT(_p->format);
  avLoggerSetFileName(_p->format, fileName);

  if (avformat_open_input(&_p->format, qPrintable(_path), nullptr, nullptr) <
      0) {
    qCritical("%s: Cannot open input", qPrintable(_path));
    avLoggerUnsetFileName(_p->format);
    return -1;
  }

  avLoggerSetFileName(_p->format, fileName);

  if (avformat_find_stream_info(_p->format, nullptr) < 0) {
    qCritical("%s: Cannot find stream info", qPrintable(_path));
    avLoggerUnsetFileName(_p->format);
    return -2;
  }

  // av_dump_p->format(fmt, 0, NULL, 0);
  int videoStreamIndex = -1;
  for (unsigned int i = 0; i < _p->format->nb_streams; i++) {
    AVStream* stream = _p->format->streams[i];
    const AVCodecContext* context = stream->codec;

    if (context->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoStreamIndex = int(i);
      _metadata.isEmpty = false;

      AVRational fps = stream->r_frame_rate;
      _metadata.frameRate = float(av_q2d(fps));
      _metadata.frameSize = QSize(context->width, context->height);
      _metadata.videoBitrate = int(context->bit_rate);

      AVCodec* vCodec = avcodec_find_decoder(context->codec_id);
      if (vCodec) _metadata.videoCodec = vCodec->name;
    } else if (context->codec_type == AVMEDIA_TYPE_AUDIO) {
      _metadata.isEmpty = false;
      _metadata.audioBitrate = int(context->bit_rate);
      _metadata.sampleRate = context->sample_rate;
      _metadata.channels = context->channels;

      AVCodec* aCodec = avcodec_find_decoder(context->codec_id);
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

  if (videoStreamIndex < 0) {
    qCritical("%s: Cannot find video stream", qPrintable(_path));
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
      qDebug("%s: unsupported pixel format for hw codec %s", qPrintable(_path),
             qPrintable(av_pix_fmt_desc_get(_p->context->pix_fmt)->name));
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
        qDebug("%s: trying hw codec '%s'", qPrintable(_path), codecs[i].name);
        _p->codec = avcodec_find_decoder_by_name(codecs[i].name);
        break;
      }
  }

  AVDictionary* codecOptions = nullptr;
  if (!_p->codec) {
    if (tryHardware) qDebug("%s: no hw codec, trying sw", qPrintable(_path));

    _p->codec = avcodec_find_decoder(_p->context->codec_id);
    if (!_p->codec) {
      qWarning("%s: no codec found", qPrintable(_path));
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

  if (avcodec_open2(_p->context, _p->codec, &codecOptions) < 0) {
    if (_isHardware) {
      qWarning("%s: could not open hardware codec, trying software",
               qPrintable(_path));
      _isHardware = false;
      _p->codec = avcodec_find_decoder(_p->context->codec_id);
      if (!_p->codec) {
        qWarning("%s: no codec found", qPrintable(_path));
        avLoggerUnsetFileName(_p->format);
        avLoggerUnsetFileName(_p->context);
        return -4;
      }

      if (avcodec_open2(_p->context, _p->codec, nullptr) < 0) {
        qCritical("%s: could not open fallback codec", qPrintable(_path));
        avLoggerUnsetFileName(_p->format);
        avLoggerUnsetFileName(_p->context);
        return -5;
      }
    } else {
      qCritical("%s: could not open codec", qPrintable(_path));
      avLoggerUnsetFileName(_p->format);
      avLoggerUnsetFileName(_p->context);
      return -5;
    }
  }

  qDebug("%s: using %d threads (requested %d) active_type=%d",
         qPrintable(_path), _p->context->thread_count, _numThreads,
         _p->context->active_thread_type);

  _p->frame = av_frame_alloc();
  if (!_p->frame) {
    qCritical("%s: Could not allocate video frame", qPrintable(_path));
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

  // avLoggerSetFileName(_p->codec, "codec-name");
  // avLoggerSetFileName(_p->videoStream, "stream-name");

  //    printf("_metadata.title=%s\n", qPrintable(_metadata.title));
  //    printf("_metadata.duration=%d\n", _metadata.duration);
  //
  //    printf("_metadata.frameRate=%.2f\n", _metadata.frameRate);
  //    printf("_metadata.frameSize=%dx%d\n", _metadata.frameSize.width(),
  //    _metadata.frameSize.height()); printf("_metadata.videoCodec=%s\n",
  //    qPrintable(_metadata.videoCodec)); printf("_metadata.videoBitrate=%d\n",
  //    _metadata.videoBitrate);
  //
  //    printf("_metadata.audioCodec=%s\n", qPrintable(_metadata.audioCodec));
  //    printf("_metadata.audioBitrate=%d\n", _metadata.audioBitrate);
  //    printf("_metadata.sampleRate=%d\n", _metadata.sampleRate);
  //    printf("_metadata.channels=%d\n", _metadata.channels);

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

bool VideoContext::seekDumb(int frame) {
  qWarning("%s: seek dumb: %d", qPrintable(_path), frame);
  QImage tmp;
  while (frame--)
    if (!decodeFrame()) return false;
  return true;
}

bool VideoContext::seekFast(int frame) {
  if (frame <= 0) return true;

  // av_dump_format(_p->format, _p->videoStream->index, NULL, 0);

  const AVRational timeBase = _p->videoStream->time_base;
  const AVRational frameRate = _p->videoStream->r_frame_rate;

  const double fps = av_q2d(frameRate);
  const double tb = av_q2d(timeBase);

  const double startSeconds = frame / fps;

  const int64_t targetTS = int64_t(floor(startSeconds / tb + 0.5)) + _firstPts;

  qDebug("seek frame=%d time=%.2f tb=(%" PRIi64 ")", frame, startSeconds,
         targetTS);

  // seek <= where we want to go
  int err = av_seek_frame(_p->format, _p->videoStream->index, targetTS,
                          AVSEEK_FLAG_BACKWARD);
  if (err < 0) {
    qWarning() << _path << "seek error" << err;
    return false;
  }

  avcodec_flush_buffers(_p->videoStream->codec);

  return true;
}

bool VideoContext::seek(int frame, const DecodeOptions& opt,
                        QVector<QImage>* decoded) {
  if (frame <= 0) {
    close();
    open(_path, opt);
    return true;
  }

  // av_dump_format(_p->format, _p->videoStream->index, NULL, 0);

  const AVRational timeBase = _p->videoStream->time_base;
  const AVRational frameRate = _p->videoStream->r_frame_rate;

  const double fps = av_q2d(frameRate);
  const double tb = av_q2d(timeBase);

  int framesSeeked = 0;

  const double startSeconds = frame / fps;

  const int64_t targetTS = int64_t(floor(startSeconds / tb + 0.5)) + _firstPts;

  qDebug("%s: seek: frame=%d time=%.2f tb=(%" PRIi64 ")", qPrintable(_path),
         frame, startSeconds, targetTS);

  if (startSeconds / tb > _firstPts) {
    bool isKeyframe = false;
    int64_t seekTime = targetTS;
    int tries = 0;

    // seek <= where we want to go, and try to get a keyframe
    // if we don't get a keyframe, or we overshot seek point,
    // back up and try again
    do {
      int err = av_seek_frame(_p->format, _p->videoStream->index, seekTime,
                              AVSEEK_FLAG_BACKWARD);
      Q_ASSERT(err >= 0);

      avcodec_flush_buffers(_p->videoStream->codec);

      // we might not get a keyframe (stream corruption, or eos?)
      do {
        if (!readPacket()) break;

        Q_ASSERT(_p->packet.pts != AV_NOPTS_VALUE);

        isKeyframe = _p->packet.flags & AV_PKT_FLAG_KEY;
        if (!isKeyframe)
          qWarning("%s: seek: not a keyframe", qPrintable(_path));

        err = avcodec_send_packet(_p->context, &_p->packet);
        if (err != 0) {
          qWarning("%s: seek: send_packet error %d", qPrintable(_path), err);
          break;
        }

      } while (!isKeyframe && _p->packet.pts < targetTS);

      seekTime -= 1.0 / tb;
      tries++;

      if (tries > 1)
        qWarning("%s: seek try loop: try=%d", qPrintable(_path), tries);

      if (tries > 10) {
        qWarning("%s: seek: failed to seek after 10 attempts, seeking dumbly",
                 qPrintable(_path));
        close();
        open(_path, opt);
        seekDumb(frame);
        return true;
      }

    } while (!isKeyframe || _p->packet.pts > targetTS);

    framesSeeked = pts2frame(_p->packet.pts, _firstPts, fps, tb);
  }

  _consumed = 0;
  int framesLeft = frame - framesSeeked;
  qDebug("%s: seek: decoding %d frames", qPrintable(_path), framesLeft);

  while (framesLeft--)
    if (decodeFrame()) {
      // store the intermediate frames if so desired
      if (decoded) {
        QImage tmp;
        int w, h, fmt;

        if (convertFrame(w, h, fmt)) {
          //avPictureToQImg(*_p->scaled, w, h, tmp, AVPixelFormat(fmt));
          avImgToQImg(_p->scaled.data, _p->scaled.linesize, w, h, tmp, AVPixelFormat(fmt));
          decoded->append(tmp);
        } else
          qWarning("%s: seek: failed to convert frame", qPrintable(_path));
      }
    } else {
      // no longer valid since we couldn't get the target
      if (decoded) decoded->clear();

      qWarning("%s: seek: failure", qPrintable(_path));
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
        qCritical("%s: av_read_frame error: %s", qPrintable(_path),
                  qPrintable(avErrorString(err)));
      return false;
    }

    if (_p->packet.flags & AV_PKT_FLAG_CORRUPT)
      qWarning("%s: Corrupt packet", qPrintable(_path));

  } while (_p->packet.stream_index != _p->videoStream->index);

  return true;
}

bool VideoContext::decodeFrame() {
  while (true) {
    int err = avcodec_receive_frame(_p->context, _p->frame);
    if (err == 0) return true;

    if (err == AVERROR_EOF) {
      qDebug("%s: avcodec_receive_frame eof", qPrintable(_path));
      break;
    } else if (err != AVERROR(EAGAIN)) {
      qCritical("%s: avcodec_receive_frame error 0x%x: %s", qPrintable(_path),
                err, qPrintable(avErrorString(err)));
      break;
    }

    if (!_flush) {
      if (!readPacket()) {
        _flush = true;
        avcodec_send_packet(_p->context, nullptr);
        continue;
      }

      if (_p->packet.size == 0) {
        qCritical("%s: Empty packet, giving up", qPrintable(_path));
        break;
      }

      Q_ASSERT(_p->packet.stream_index == _p->videoStream->index);

      err = avcodec_send_packet(_p->context, &_p->packet);
      if (err != 0)
        qWarning("%s: avcodec_send_packet error 0x%x : %s", qPrintable(_path),
                 err, qPrintable(avErrorString(err)));
    }
    else {
      // attempt to prevent hang when seeking near the eof
      qWarning() << "resending null packet, might hang here";
      avcodec_send_packet(_p->context, nullptr);
    }
  }

  return false;
}

QVariantList VideoContext::readMetaData(const QString& path,
                                        const QStringList& keys) {
  AVFormatContext* format = nullptr;

  QVariantList values;
  for (int i = 0; i < keys.count(); ++i) values.append(QVariant());

  format = avformat_alloc_context();
  Q_ASSERT(format);

  const QString fileName = QFileInfo(path).fileName();
  avLoggerSetFileName(format, fileName);

  if (avformat_open_input(&format, qPrintable(path), nullptr, nullptr) < 0) {
    qCritical("%s: Cannot open input", qPrintable(path));
    avLoggerUnsetFileName(format);
    return values;
  }

  if (!format->metadata) {
    qWarning("%s: No metadata", qPrintable(path));
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
        qCritical("%s: error while decoding: invalid pixel format in AVFrame",
                  qPrintable(_path));
        return false;
      }

      qInfo() << _path << "scaling from: "
              << av_get_pix_fmt_name(AVPixelFormat(_p->frame->format))
              << QString("@%1x%2").arg(_p->frame->width).arg(_p->frame->height)
              << "to:" << av_get_pix_fmt_name(AVPixelFormat(fmt))
              << QString("@%1x%2").arg(w).arg(h)
              << "fast=" << _opt.fast;

      _p->scaler = sws_getContext(
          _p->frame->width, _p->frame->height, AVPixelFormat(_p->frame->format),
          w, h, AVPixelFormat(fmt), _opt.fast ? SWS_FAST_BILINEAR : SWS_BICUBIC,
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

  auto& names = pointerToFileName();
  QString fileName;
  if (names.contains(ptr))
    fileName = names[ptr];
  else {
    char path[PATH_MAX+1]={0};
    getcwd(path, sizeof(path));
    fileName = QString("cwd={") + path + "}";
  }

  char buf[1024] = {0};
  vsnprintf(buf, sizeof(buf) - 1, fmt, vl);
  QString str = fileName + ": " + buf;
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

#if 0
int VideoContext::avExecute(AVCodecContext* c,
                            int (*func)(AVCodecContext*, void*), void* arg2,
                            int* ret, int count, int size) {
  qDebug() << count;
  Q_ASSERT(0);
  QVector<QFuture<int>> work;
  for (int i = 0; i < count; ++i)
    work.append(QtConcurrent::run(
        [=]() { return func(c, static_cast<char*>(arg2) + i * size); }));
  for (int i = 0; i < count; ++i) {
    auto& w = work[i];
    w.waitForFinished();
    ret[i] = w.result();
  }
  return 0;
}

int VideoContext::avExecute2(AVCodecContext* c, AvExec2Callback func,
                             void* arg2, int* ret, int count) {
  //
  // this cannot work as written
  // - threadNumber must be (0 - c->thread_count] which QThreadPool cannot
  // guarantee,
  //   it will allocate more than maxThreadCount threads for some reason
  //
  //    qDebug() << count;
  //    Q_ASSERT(0);

  QHash<QThread*, int> threads;
  QMutex mutex;
  int numThreads = 0;

  QVector<int> results;
  for (int i = 0; i < count; ++i) results.append(i);

  QFuture<void> f =
      QtConcurrent::map(results.begin(), results.end(),
                        [=, &mutex, &numThreads, &threads](int i) {
                          int threadNumber = -1;
                          {
                            QMutexLocker locker(&mutex);
                            QThread* thread = QThread::currentThread();
                            auto it = threads.find(thread);
                            if (it != threads.end())
                              threadNumber = it.value();
                            else {
                              threadNumber = numThreads++;
                              threads.insert(thread, threadNumber);
                            }
                          }
                          //                    qDebug() << arg2 << i <<
                          //                    threadNumber << c->thread_count;
                          Q_ASSERT(threadNumber < c->thread_count);

                          // emms_c();
                          int val = func(c, arg2, i, threadNumber);
                          // emms_c();
                          return val;
                        });

  f.waitForFinished();
  if (ret)
    for (int i = 0; i < count; ++i) ret[i] = results[i];

  qDebug() << "finished";
  return 0;
}
#endif

bool VideoContext::nextFrame(QImage& outQImg) {
  bool gotFrame = decodeFrame();

  int w, h, fmt;

  if (convertFrame(w, h, fmt))
    avImgToQImg(_p->scaled.data, _p->scaled.linesize, w, h, outQImg, AVPixelFormat(fmt));
  else
    avFrameToQImg(*(_p->frame), outQImg);

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
  float aspect = float(av_q2d(
      av_guess_sample_aspect_ratio(_p->format, _p->videoStream, _p->frame)));
  if (aspect == 0.0f) {
    qWarning() << _path << ": unable to guess SAR, assuming 1.0";
    aspect = 1.0f;
  }
  return aspect;
}

/* Side-by-side Video Display
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
#include "../media.h"
#include "../videocontext.h"
#include "mediawidget.h"

class FrameCache;
class Frame;
class Database;

/**
 * @brief Visual compare of two videos (side-by-side or interleaved)
 */
class VideoCompareWidget : public QWidget {
  Q_OBJECT
  NO_COPY_NO_DEFAULT(VideoCompareWidget, QWidget)

 public:
  /**
   * @param left   first video (A)
   * @param right  second video (B)
   * @param range  matching frames if known for initial seekpoint (src=left,
   * dst=right)
   * @param parent
   * @param f
   */
  VideoCompareWidget(const Media& left, const Media& right, const MatchRange& range = MatchRange(),
                     const MediaWidgetOptions& options = MediaWidgetOptions(),
                     QWidget* parent = nullptr);

  ~VideoCompareWidget();

  void show();
  void showFullscreen() = delete;
  void showNormal() = delete;
  void showMaximized() = delete;
  void showMinimized() = delete;

 private:
  void paintEvent(QPaintEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

  void moveCursor(int pos) { _cursor = pos; }
  void seekFrame(int pos) {
    moveCursor(pos);
    update();
  };
  void skipSeconds(int seconds) { seekFrame(seconds * floorf(_fps + 0.5) + _cursor); };

  void offsetCursor(int offset) { _video[0].offset += offset; }
  void offsetFrames(int frames) {
    offsetCursor(frames);
    update();
  };
  void offsetSeconds(int seconds) { offsetFrames(seconds * floorf(_fps + 0.5)); };

  void findQualityScores();
  void alignTemporally();
  void alignSpatially();
  void playSideBySide();
  void compareInKdenlive();
  void writeThumbnail(int index);

  void drawFrame(QPainter& painter, const FrameCache& cache, const QImage& img, int iw, int ih,
                 int matchIn, int matchLen, int currPos, const QString& text, int x, int y, int w,
                 int h) const;

  int64_t sad128(int i) const;

  int _cursor = 0;  // displayed frame number
  int _endPos = 0;  // end frame number of range or shortest video
  float _fps = 0;   // fps of highest-fps video

  struct {
    Media media;                         // source media
    QString label, side;                 // file name and A/B side label
    QVector<QImage> visual;              // analysis visual
    int visualFrame;                     // frame number of corresponding to analysis visuals
    bool crop;                           // if true enable de-letterbox cropping
    int in, out, offset;                 // match range and cursor offset (temporal align)
    const VideoContext::Metadata* meta;  // video metadata (fps/codec etc)
    std::unique_ptr<FrameCache> cache;   // decoder/frame cache
  } _video[2];

  int _visualIndex = 0;  // 0==disable, >0 => analysis image index-1

  bool _stacked = false;               // show one video and flip between them manually
  bool _sameSize = false;              // scale right to match left
  bool _swap = false;                  // swap left/right side
  float _alignX = 0, _alignY = 0;      // spatial alignment, factor of image width/height
  int _scrub = 0;                      // scrub forward or backward until a key is pressed
  bool _maximized = false;             // use to restore maximized window
  double _zoom = 0.0;                  // zoom in
  const MediaWidgetOptions& _options;  // for thumbnailer
};

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

class FrameCache;
class Frame;

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
  VideoCompareWidget(const Media& left, const Media& right,
                     const MatchRange& range = MatchRange(),
                     QWidget* parent = nullptr, Qt::WindowFlags f = 0);

  ~VideoCompareWidget();

  // force using show() to restore saved state
  void show() { _maximized ? super::showMaximized() : super::showNormal(); }
  void showMaximized() = delete;
  void showNormal() = delete;

 private:
  void paintEvent(QPaintEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

  void loadFrameIfNeeded(int frame);
  void findQualityScores();
  void alignTemporally();
  void alignSpatially();
  void seekFrame(int frame);
  void shiftFrames(int offset);
  void playSideBySide();

  Media _left, _right;
  MatchRange _range;
  QString _leftLabel, _rightLabel;
  int _lastFrame = -1;
  int _selectedFrame = 0;

  FrameCache* _leftFrames;
  FrameCache* _rightFrames;

  QVector<QImage> _leftQualityVisual;  // visualization images from analysis
  QVector<QImage> _rightQualityVisual;

  int _visualIndex = 0;   // 0==disable, >0 => analysis image index-1
  int _visualFrame = -1;  // frame number of left side corresponding to visual

  QFuture<Frame*> _leftThread, _rightThread;
  int _frameOffset = 0;       // add offset to left video to align temporally
  bool _interleaved = false;  // show one video and flip between them
  bool _sameSize = false;     // scale right to match left
  bool _swap = false;         // swap left/right side
  float _alignX = 0, _alignY = 0;  // align spatially
  int _scrub = 0;           // scrub forward or backward until a key is pressed
  bool _maximized = false;  // use to restore maximized window
  double _zoom = 0.0;       // zoom in
  bool _cropLeft = false;   // auto-crop left/right (remove black bars)
  bool _cropRight = false;
};

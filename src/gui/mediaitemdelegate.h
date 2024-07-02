/* Delegate for MediaGroup-based Views
   Copyright (C) 2024 scrubbbbs
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
class MediaPage;

/// Custom painting and layout of list view items, maybe generalized to any view
class MediaItemDelegate : public QAbstractItemDelegate {
  NO_COPY_NO_DEFAULT(MediaItemDelegate, QAbstractItemDelegate);

 public:
  MediaItemDelegate(QAbstractItemView* parent);

  virtual ~MediaItemDelegate() {}

  void setPage(const MediaPage* page); // must be set before sizeHint() etc

  void setZoom(double zoom) { _zoom = zoom; }  // 0.0-1.0; 1.0==no zoom
  void setPan(const QPointF& pan) { _pan = pan; }
  void setTextHeight(int height) { _textHeight = height; }

  void setScaleMode(int mode) { _scaleMode = mode; }
  int scaleMode() const { return _scaleMode; }
  void cycleScaleMode() { _scaleMode = (_scaleMode + 1) % SCALE_NUMMODES; }

  void cycleMinFilter() { _minFilter = (_minFilter + 1) % _filters.count(); }
  void cycleMagFilter() { _magFilter = (_magFilter + 1) % _filters.count(); }

  int spacing() const { return SPACING; }

 protected:
  /**
   * @brief Get the scale factor, destination rect, and
   * image-to-viewport transform for <full> to fit inside <rect>,
   * accounting for scale-to-fit and zoom/pan state
   *
   * @param imgRect   Full size (unscaled) image
   * @param itemRect  List item paint (sub) rectangle
   * @param dpr       Device Pixel Ratio
   * @param scale     Scale-to-fit factor from image->viewport (ignoring zoom)
   * @param dstRect   Destination rectangle in view coordinates
   * @param i2v       Image-to-viewport item transformation (0,0)==dstRect.topLeft()
   */
  void calculate(const QRect& imgRect,
                 const QRect& itemRect_,
                 const double dpr,
                 double& scale,
                 QRect& dstRect,
                 QTransform& i2v) const;

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const;

 private:
  /// Filter for resizing images (bicubic, nearest, etc)
  struct ScaleFilter {
    int id;
    QString name;
  };
  QVector<ScaleFilter> _filters;

  double _avgItemRatio =  2.0/3.0;
  double _zoom = 1.0;
  QPointF _pan;
  int _equalFilter = 0, _minFilter = 0, _magFilter = 0;
  int _textHeight = 100;
  bool _debug = false;

  enum { SCALE_DOWN = 0, // scale large images to fit
         SCALE_UP = 1,   // scale small images to fit
         SCALE_NONE = 2,  // actual pixels / no scaling
         SCALE_NUMMODES = 3
  };
  int _scaleMode = SCALE_DOWN;

  const MediaPage* _page = nullptr;

  const int SPACING = 8; // space between items; if too small breaks the layout logic (sizeHint())
  const int MIN_IMAGE_HEIGHT = 16;  // do not draw image below this
  const int HISTOGRAM_PADDING = 16; // distance from item edge
  const int HISTOGRAM_SIZE = 32;    // width of histogram plot
  const Qt::TextElideMode ELIDE_FILENAME = Qt::ElideMiddle;
};

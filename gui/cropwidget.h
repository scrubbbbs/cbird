/* Get a rectangle selection from an image
   Copyright (C) 2022 scrubbbbs
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
class Database;
class Media;

class CropWidget : public QLabel {
  Q_OBJECT
  NO_COPY_NO_DEFAULT(CropWidget, QLabel)

 public:
  /**
   * @brief Crop and save index thumbnail
   * @param db  Database to create thumbnail for
   * @param m Media which must support loadImage()
   * @param async If true then save in another thread, then execute after(ok)
   *
   * @note if Media was queried from database, then additional EXIF metadata
   *       is saved to the thumbnail.
   */
  static bool setIndexThumbnail(
      const Database& db, const Media& m, QWidget* parent = nullptr, bool async = false,
      const std::function<void(bool)>& after = [](bool) { ; });

  /**
   * @brief Widget for cropping an image
   * @param img
   * @param fullscreen Fill the screen of the parent or primary screen
   * @param parent Parent widget for screen detection or embedding
   * @note The widget hides itself when the selection is made,
   *       which is currently when the mouse button is released.
   * @note When the widget is hidden, use image() to get the crop, which will be null
   *       if there was a problem
   * @note the image is displayed in device pixels, which could be small on hi dpi screens
   */
  CropWidget(const QImage& img, bool fullscreen = false, QWidget* parent = nullptr);
  virtual ~CropWidget() = default;

  /**
   * @brief Cropped image
   * @note only valid after widget hides itself
   */
  const QImage& image() const { return _image; }

  /**
   * @brief Change selection rectangle constraint
   * @param enable
   * @param num Numerator of aspect ratio
   * @param den Denominator of aspect ratio
   *
   * @note The rectangle will never go out of bounds, regardless of constraint.
   *       However it may cross into background color, and this will be included
   *       in the crop
   */
  void setConstraint(bool enable, int num = 4, int den = 3);

 private:
  void repaintSelection();

  void keyPressEvent(QKeyEvent* ev) override;
  void keyReleaseEvent(QKeyEvent* ev) override;
  void mousePressEvent(QMouseEvent* ev) override;
  void mouseMoveEvent(QMouseEvent* ev) override;
  void mouseReleaseEvent(QMouseEvent* ev) override;

  bool _dragging = false;
  QRect _selection;      // selection rectangle
  QLabel* _selectLabel;  // selection indicator
  QImage _image;         // output
  QPoint _lastMousePos;  // mouse pos before mouseMoveEvent()
  QPixmap _background;   // selection area / undimmed

  bool _constrain = true;
  int _aspect_num = 4, _aspect_den = 3;

  const qreal _BG_OPACITY = 0.5;
  const Qt::GlobalColor _BG_COLOR = Qt::black;
};

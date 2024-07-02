/* Display for groups of related Media
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
#include "mediawidget.h"

class Database;

class MediaFolderListWidget : public QListWidget {
  Q_OBJECT
  NO_COPY_NO_DEFAULT(MediaFolderListWidget, QListWidget)

 public:
  MediaFolderListWidget(
      const MediaGroup& list,
      const MediaWidgetOptions& options = MediaWidgetOptions(),
      QWidget* parent = nullptr);
  virtual ~MediaFolderListWidget();

  void show();
  void showFullscreen() = delete;
  void showNormal() = delete;
  void showMaximized() = delete;
  void showMinimized() = delete;

 Q_SIGNALS:
  void mediaSelected(const MediaGroup& group);
  void beginHover(int index);
  void endHover();

 private Q_SLOTS:
  void chooseAction();

 private:
  void mouseMoveEvent(QMouseEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void close();

  MediaGroup selectedMedia() const;

  MediaGroup _list;
  MediaWidgetOptions _options;
  QPoint _hoverPos;
  QTimer* _hoverTimer = nullptr;
  bool _hovering = false;
};

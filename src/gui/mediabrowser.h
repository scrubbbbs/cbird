/* Display list of Media in different ways
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
#include "../index.h"
#include "mediawidget.h"

class MediaBrowser : public QObject {
  Q_OBJECT
  NO_COPY_NO_DEFAULT(MediaBrowser, QObject)

 public:
  enum {
    ShowNormal = 0,  /// use MediaGroupListWidget, no grouping
    ShowPairs = 1,   /// use MediaFolderWidget, group search results by folder pairs
    ShowFolders = 2  /// use MediaFolderWidget, group by folder
  };

  /**
   * @brief Display results browser modal dialog
   * @param list search results or media list
   * @param mode how to display the items
   * @return qApp->exec()
   */
  static int show(const MediaGroupList& list, int mode, const MediaWidgetOptions& options);

 private Q_SLOTS:
  /// connected to selected action of MFLW or MGLW
  void mediaSelected(const MediaGroup& group);

 private:
  MediaBrowser(const MediaWidgetOptions& options);

  void show(const MediaGroupList& list);
  void showIndex(const MediaGroup& index, const QHash<QString, MediaGroupList>& groups);

  static int showList(const MediaGroupList& list, const MediaWidgetOptions& options);
  static int showSets(const MediaGroupList& list, const MediaWidgetOptions& options);
  static int showFolders(const MediaGroupList& list, const MediaWidgetOptions& options);

  MediaWidgetOptions _options;
  const QHash<QString, MediaGroupList>* _groups = nullptr;
};

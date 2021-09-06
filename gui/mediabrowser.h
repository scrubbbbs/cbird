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

class MediaBrowser : public QObject {
  Q_OBJECT
  NO_COPY_NO_DEFAULT(MediaBrowser, QObject)

 public:
  enum {
    ShowNormal = 0, /// use MediaGroupListWidget
    ShowPairs = 1,  /// group results that match between two folders
    ShowFolders = 2 /// group results from the same folder
  };

  enum {
    SelectSearch = 0,   /// on select item, search for it
    SelectExitCode = 1, /// on select item, set exit code to item index + 1 and quit
  };

  /**
   * @brief Display results browser modal dialog
   * @param list search results or media list
   * @param params search parameters (used iif selectionMode = SelectSearch)
   * @param mode how to display the items
   * @param selectionMode action when an item is selected
   * @return qApp->exec()
   */
  static int show(const MediaGroupList& list, const SearchParams& params,
                  int mode = ShowNormal, int selectionMode = SelectSearch);

 private Q_SLOTS:
  /// connected to selected action of MFLW or MGLW
  void mediaSelected(const MediaGroup& group);

 private:
  MediaBrowser(const SearchParams& params, bool sets = false,
               int selectionMode = SelectSearch);

  void show(const MediaGroupList& list);
  void showIndex(const MediaGroup& index, const QString& basePath);

  static int showList(const MediaGroupList& list, const SearchParams& params,
                      int selectionMode);
  static int showSets(const MediaGroupList& list, const SearchParams& params);
  static int showFolders(const MediaGroupList& list,
                         const SearchParams& params);

  SearchParams _params;
  bool _sets;
  int _selectionMode;
};

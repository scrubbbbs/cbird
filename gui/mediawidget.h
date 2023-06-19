/* Common stuff for Media widgets
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
class Database;
class MediaWidgetOptions {
 public:
  enum Flags {
    FlagsNone = 0,
    FlagFastSeek = 1 << 1,       /// use fast but inaccurate seek for thumbnails
    FlagSelectFirst = 1 << 2,    /// set initial selection to first item
    FlagDisableDelete = 1 << 3,  /// do not allow deletion of files
  };
  int flags = FlagsNone;

  enum {
    SelectSearch = 0,   /// on select item, search for it
    SelectExitCode = 1, /// on select item, set exit code to item index + 1 and quit
    SelectOpen = 2,     /// on select item, open the item
  } selectionMode = SelectSearch;

  QString basePath;
  SearchParams params;    /// params for SelectSearch or other things
  Database* db = nullptr; /// database for SelectSearch or other things
  Media selectOnOpen;     /// media to reveal/select on open

  // todo: WidgetParams
  bool trackWeeds = true; /// if true, remember user deletions
  int maxPerPage = 12;    /// max images for paged views
  int iconSize = 256;     /// thumbnail size for icon views
  int iconTextWidth = 40; /// max icon text width (characters per line)
};

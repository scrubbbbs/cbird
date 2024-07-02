/* Helpers for MediaGroup
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
#include "../media.h"
#include "../profile.h"
#include "mediawidget.h"

// container for MediaGroup
class MediaPage
{
 public:
  Q_DISABLE_COPY_MOVE(MediaPage);

  MediaPage(int _id, const MediaGroup& _group, const MediaWidgetOptions& options)
      : id(_id)
      , group(_group)
      , row(-1)
      , _options(options){};

  const int id;
  MediaGroup group;
  int row;

 private:
  const MediaWidgetOptions _options;

 public:
  int count() const { return group.count(); }

  static bool isLoaded(const Media& m) { return !m.image().isNull(); }

  static bool isReloadable(const Media& m) { return isAnalysis(m) || m.isReloadable(); }

  bool isLoaded() const;

  static void unload(Media& m);

  void unloadData(bool unloadAll = true);

  // unload starting from the end -- most likely place to find unstarted loader
  bool unloadOne();

  double avgAspect() const;

  /// path to containing folder of all items
  QString folderPath() const;

  /// summary info like group-by prop value
  QString info() const;

  /// analysis items are fake Media with a tag on the name

  static bool isDifferenceAnalysis(const Media& m) { return m.path().endsWith("-diff***"); }

  static bool isAnalysis(const Media& m) { return m.path().endsWith("***"); }

  static Media newDifferenceAnalysis() {
    // needs unique "path" for image loader, this is probably fine
    QString id = QString::number(nanoTime(), 16);
    Media m(id + "-diff***", Media::TypeImage);
    return m;
  }

  int countNonAnalysis() const {
    return std::count_if(group.begin(), group.end(), [](const Media& m) { return !isAnalysis(m); });
  }

  void removeAnalysis() {
    if (isAnalysis(group.last())) group.removeLast();
  }

  void addDifferenceAnalysis() {
    if (group.count() == 2 && !isAnalysis(group.last())) group.append(newDifferenceAnalysis());
  }

  bool isPair() const { return countNonAnalysis() == 2; }

  // we need to select an item, so keyboard nav works immediately
  // - the last item in the group seems like a reasonable choice
  //   since when using -similar-to the needle is the first one
  // - also need to make sure the item is selectable (not analysis item)
  int defaultModelIndex() const;

  // pair: add weed for other file
  void addWeed(int weedIndex) const;

  // pair: replace deleted with other file
  // not const because path is updated in group[otherIndex];
  void replaceFile(int deletedIndex);

  void removeIds(const QSet<int> ids) {
    group.removeIf([&ids](const Media& m) { return ids.contains(m.id()); });
  }

  void removeIndices(const QSet<int>& indices);

  // update file path after reparenting (moving or renaming parent)
  void setParentPath(const QString& oldPath, const QString& newPath);

  void setNegativeMatch(int first, int second) const;

  void setNegativeMatch() const {
    for (int i = 0; i < countNonAnalysis() - 1; ++i)
      setNegativeMatch(i, i + 1);
  }

  // reset template match/analysis image?
  void reset();

  // move each item to the left, wrap around
  void rotate();

  // overwrite matching items
  void setMediaWithPath(const QString& path, const Media& value);
};

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
#include "mediapage.h"
#include "../database.h"

bool MediaPage::isLoaded() const {
  for (const Media& m : group)
    if (!isLoaded(m)) return false;
  return true;
}

void MediaPage::unload(Media& m) {
  m.setData(QByteArray());
  m.setImage(QImage());
}

void MediaPage::unloadData(bool unloadAll) {
  for (Media& m : group) {
    if (!unloadAll && !isReloadable(m)) continue;
    unload(m);
  }
}

bool MediaPage::unloadOne() {
  for (int i = group.count() - 1; i >= 0; --i) {
    Media& m = group[i];
    if (isReloadable(m) && isLoaded(m)) {
      unload(m);
      return true;
    }
  }
  return false;
}

double MediaPage::avgAspect() const {
  double sum = 0;
  for (const Media& m : group)
    sum += double(m.width()) / m.height();
  return sum > 0 ? sum / group.count() : 2.0/3.0;
}

QString MediaPage::folderPath() const {
  QString prefix = Media::greatestPathPrefix(group);
  return prefix.mid(0, prefix.lastIndexOf('/') + 1);
}

QString MediaPage::info() const {
  QString info;
  if (count() <= 0) return info;
  QString key = group[0].attributes().value("group"); // prop of group-by
  if (key.isEmpty()) return info;
  return QString("[%1]").arg(key);
}

int MediaPage::defaultModelIndex() const { // default selected model index
  if (_options.flags & MediaWidgetOptions::FlagSelectFirst) return 0;
  if (group.isEmpty()) return -1;
  int index = group.count() > 0 ? group.count() - 1 : 0;
  while (index > 0 && isAnalysis(group.at(index)))
    index--;
  return index;
}

void MediaPage::addWeed(int weedIndex) const {
  Q_ASSERT(isPair() && (weedIndex == 0 || weedIndex == 1));
  int otherIndex = (weedIndex + 1) % 2; // works because isPair() and analysis always comes after
  const Media& other = group[otherIndex];
  const Media& weed = group[weedIndex];
  if (weed.md5() != other.md5() && !_options.db->addWeed(weed, other))
    qWarning() << "Failed to add weed" << weed.md5() << other.md5();
}

void MediaPage::replaceFile(int deletedIndex) {
  Q_ASSERT(isPair() && (deletedIndex == 0 || deletedIndex == 1));
  int otherIndex = (deletedIndex + 1) % 2;
  const Media& deleted = group[deletedIndex];
  Media& other = group[otherIndex];
  const QFileInfo info(deleted.path());
  const QFileInfo otherInfo(other.path());

  // the new name must keep the suffix, could be different
  QString newName = info.completeBaseName() + "." + otherInfo.suffix();

  // rename (if needed) and then move
  if (otherInfo.fileName() == newName || _options.db->rename(other, newName))
    _options.db->move(other, info.dir().absolutePath());
}

void MediaPage::removeIndices(const QSet<int>& indices) {
  MediaGroup newGroup;
  const int oldCount = group.count();
  for (int i = 0; i < oldCount; ++i)
    if (!indices.contains(i)) newGroup.append(group[i]);
  group = newGroup;
}

void MediaPage::setParentPath(const QString& oldPath, const QString& newPath) {
  for (Media& m : group)
    if (m.path().startsWith(oldPath)) m.setPath(newPath + m.path().mid(oldPath.length()));
}

void MediaPage::setNegativeMatch(int first, int second) const {
  Q_ASSERT(first >= 0 && first < group.count());
  Q_ASSERT(second >= 0 && second < group.count());
  Q_ASSERT(first != second);

  const Media& m1 = group[first];
  const Media& m2 = group[second];
  Q_ASSERT(!isAnalysis(m1) && !isAnalysis(m2));

  if (_options.db) _options.db->addNegativeMatch(m1, m2);
}

void MediaPage::reset() {
  removeAnalysis();
  for (Media& m : group) {
    m.setRoi(QVector<QPoint>());
    m.setTransform(QTransform());
  }
}

void MediaPage::rotate() {
  int offset = 1;
  if (isAnalysis(group.last())) // do not rotate the analysis image
    offset = 2;
  group.move(0, group.count() - offset);
}

void MediaPage::setMediaWithPath(const QString& path, const Media& value) {
  for (Media& m : group)
    if (m.path() == path) m = value;
}

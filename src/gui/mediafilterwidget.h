/* Item filtering for MediaGroupTableWidget
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
   <https://www.gnu.org/licenses/>.
*/
#pragma once

#include <QtWidgets/QWidget>

class QPushButton;
class QTableView;

class MediaFilterWidget : public QWidget {
  Q_OBJECT
  NO_COPY_NO_DEFAULT(MediaFilterWidget, QWidget)

 public:
  MediaFilterWidget(QWidget* parent = Q_NULLPTR);
  virtual ~MediaFilterWidget();

  void connectModel(QObject* model, const char* slot);

 Q_SIGNALS:
  /// caller connects this to respond to UI state changes
  void filterChanged(int match, int size, const QString& path);

 private Q_SLOTS:
  void matchButtonPressed();
  void matchMenuTriggered();
  void sizeTextChanged(const QString& text);
  void pathTextChanged(const QString& text);

 private:
  int _match;
  int _size;
  QString _path;
  QMenu* _matchMenu;
  QPushButton* _menuButton;
};

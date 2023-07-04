/* Table display for list of Media
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

class Database;

/**
 * @brief The MediaGroupTableModel class is the model component of
 *        MediaGroupTableWidget that does most of the work.
 */
class MediaGroupTableModel : public QAbstractTableModel {
  Q_OBJECT
  NO_COPY_NO_DEFAULT(MediaGroupTableModel, QAbstractTableModel)

 public:
  enum {
    ColIcon = 0,
    ColOrderAdded,
    ColMegaPixels,
    ColDimensions,
    ColAlt,      /// attributes()["alt"]
    ColSubdir,   /// attributes()["group"]
    ColComment,  /// attributes()["comment"]
    ColOrigin,   /// attributes()["origin"]
    ColPath,
    ColScore,
    NumCols
  };

  enum { ShowAll = 1, ShowNoMatch = 2, ShowAnyMatch = 4, ShowBigger = 8, ShowSmaller = 16 };

  // not all combinations of match flags make sense,
  // if a flag wants to be added, some might be removed first
  static int validMatchFlags(int oldFlags, int newFlag);

  MediaGroupTableModel(QObject* parent);
  virtual ~MediaGroupTableModel() {}

 protected:
  // QAbstractTableModel interface
  int rowCount(const QModelIndex& parent = QModelIndex()) const;
  int columnCount(const QModelIndex& parent = QModelIndex()) const;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;
  void sort(int column, Qt::SortOrder order);
  bool removeRows(int row, int count, const QModelIndex& parent);

 public:
  void addMedia(const Media& m);
  void addMediaGroup(const MediaGroup& g);
  void updateMedia(const Media& m);
  void setMark(const QString& path, bool mark);

  Media mediaWithPath(const QString& path) const;

  void memoryUsage(int& objects, size_t& bytes) const;

 public Q_SLOTS:
  // like removeRows() but find the rows to remove
  void removeMediaWithPath(const QString& path);
  void applyFilter(int match, int size, const QString& path);
  void removeAll();

 private:
  void removeData(const Media& m);
  void setSortFunction(int column, Qt::SortOrder order);
  void addIcon(const Media& m);

  QMap<QString, QIcon> _icons;
  QMap<QString, bool> _mark;

  QStringList _header;
  QMap<QString, Media> _data;
  QStringList _filtered;  // refers back to _data;
  int _sortColumn;
  Qt::SortOrder _sortOrder;
  std::function<bool(const Media& a, const Media& b)> _compareFunc;
  std::function<bool(const Media& a)> _filterFunc;

  int _pos;
};

/**
 * @brief The MediaGroupTableWidget class provides a table view for MediaGroup
 * (search result) objects and hooks to do operations on them. The intended use
 * is for MediaGroup(s) that come from a browser plugin or webview. The table
 * displays the results which can be filtered, additional searches can be
 * performed, items downloaded etc.
 */
class MediaGroupTableWidget : public QTableView {
  Q_OBJECT
  NO_COPY_NO_DEFAULT(MediaGroupTableWidget, QTableView)

 public:
  MediaGroupTableWidget(QWidget* parent = nullptr);
  virtual ~MediaGroupTableWidget();

  /**
   * @brief Set model of the QTableView
   * @param model probably must be MediaGroupTableModel
   */
  void setModel(QAbstractItemModel* model);

  /**
   * @brief Set root path for downloadToFolder,moveToFolder
   * @param path path on local filesystem
   */
  void setIndexPath(const QString& path) { _indexPath = path; }

  // force using show() to restore saved state
  void show() { _maximized ? super::showMaximized() : super::showNormal(); }
  void showMaximized() = delete;
  void showNormal() = delete;

 Q_SIGNALS:
  // hooks for the application
  void downloadUrl(const QUrl& url, const QString& savePath, int sequence, const QString& alt);
  void moveUrl(const QUrl& url, const QString& dstPath);
  void openUrl(const QUrl& url);
  void searchMedia(const Media& media);
  void revealMedia(const Media& media);
  void altSearchMedia(const Media& media);

 private Q_SLOTS:
  void deleteAction();            /// remove from view
  void downloadAction();          /// => downloadUrl()
  void downloadToFolderAction();  /// => downloadUrl()
  void moveToFolderAction();      /// => moveUrl()
  void openAction();              /// => openUrl()
  void searchAction();            /// => searchMedia()
  void altSearchAction();         /// => altSearchMedia()
  void copyUrlAction();           /// copy url to clipboard
  void copyImageAction();         /// copy image to clipboard
  void revealAction();            /// => revealMedia()

 private:
  void expandRow(const QModelIndex& index);
  void execContextMenu(const QPoint& p);
  QStringList selectedPaths() const;
  Media firstSelectedMedia();
  QAction* addAction(const QString& label, const QKeySequence& shortcut, const char* slot);
  int _defaultRowHeight;
  QString _indexPath;
  bool _maximized = false;
};

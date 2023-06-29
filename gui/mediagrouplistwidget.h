/* Grid display for list of MediaGroup (search results)
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
class MediaItemDelegate;

/**
 * @class MediaGroupListWidget
 * @brief The MediaGroupListWidget class is used to display and manage a list of
 *        Media objects. Each MediaGroup is displayed on one screenful,
 *        and is resized to fit so there is no vertical scroll.
 *
 *        Operations are in the right-click context menu and apply
 *        to the selected items.
 */
class MediaGroupListWidget : public QListWidget {
  Q_OBJECT
  NO_COPY_NO_DEFAULT(MediaGroupListWidget, QListWidget)

  friend class MediaItemDelegate;

 public:
  MediaGroupListWidget(const MediaGroupList& list,
                       const MediaWidgetOptions& options = MediaWidgetOptions(),
                       QWidget* parent = nullptr);

  virtual ~MediaGroupListWidget();

  /// move to item's page and select the item; call before show()
  bool selectItem(const Media& item);

  // @note force using show() to ensure save/restore of min/max state
  void show();
  void showFullscreen() = delete;
  void showNormal() = delete;
  void showMaximized() = delete;
  void showMinimized() = delete;

  void close();

  bool fastSeek() const { return _options.flags & MediaWidgetOptions::FlagFastSeek; }

 Q_SIGNALS:
  /// Emitted by chooseAction()
  void mediaSelected(const MediaGroup& group);

 private Q_SLOTS:
  void execContextMenu(const QPoint& p);

  /// Open selected files with desktop service
  void openAction();

  /// Show selected item in filemanager
  void openFolderAction();

  /// Move selected files to trash
  void deleteAction();

  /// Delete selected file, replace with other file (if there a pair)
  void replaceAction();

  /// Move selected file to subdir of its index
  void moveFileAction();

  /// Move folder/zip of selected file to subdir
  void moveFolderAction();

  /// Rename selected file
  void renameFileAction();

  /// Rename folder/container of selected file
  void renameFolderAction();

  /// Copy name from the other (unselected) item in a pair
  void copyNameAction();

  /// Add no-reference quality score to item descriptions
  void qualityScoreAction();

  /// Scale-up smaller items to match the largest item
  void normalizeAction();

  /// Template match first item to selected item, removing other items
  void templateMatchAction();

  /// Compare the first item to selected item
  void compareVideosAction();

  /// Compare first item to selected item
  void compareAudioAction();

  /// Remove selected items from the view (do not delete files)
  void clearAction() { removeSelection(false); }

  /// Cycle positions of items
  void rotateAction() { rotateGroup(_currentRow); }

  /// When enter key is pressed, emit mediaSelected()
  void chooseAction();

  /// Reset changes to the current row
  void reloadAction();

  /// Copy image to clipboard
  void copyImageAction();

  /// Set a thumbnail for the index
  void thumbnailAction();

  /// Record positive match to csv file
  void recordMatchTrueAction() { recordMatch(true); }

  /// Record negative match to csv file
  void recordMatchFalseAction() { recordMatch(false); }

  /// Forget selection is a weed
  void forgetWeedsAction();

  /// Add first item and selected items to negative matches
  void negMatchAction() {
    addNegMatch(false);
    removeSelection(false);
  }

  /// Add all items to negative matches
  void negMatchAllAction() {
    addNegMatch(true);
    nextGroupAction();
  }

  // navigation
  void nextGroupAction() { loadRow(_currentRow + 1); }
  void prevGroupAction() { loadRow(_currentRow - 1); }
  void jumpForwardAction() { loadRow(_currentRow + 100); }
  void jumpBackAction() { loadRow(_currentRow - 100); }
  void jumpToStartAction() { loadRow(0); }
  void jumpToEndAction() { loadRow(_list.count() - 1); }

  /// Move window to next available screen
  void moveToNextScreenAction();
  // void toggleFullscreenAction();

  // item zoom/pan
  void zoomInAction();
  void zoomOutAction();
  void panLeftAction();
  void panRightAction();
  void panUpAction();
  void panDownAction();
  void resetZoomAction();

  /// Cycle minification filter (scale < 100%)
  void cycleMinFilter();

  /// Cycle magnification filter (scale > 100%)
  void cycleMagFilter();

  /// Toggle image-pair differences visualization
  void toggleAutoDifferenceAction();

  void increasePageSize() { resizePage(true); }
  void decreasePageSize() { resizePage(false); }

  /// Open new window with all images in the selection's folder
  void browseParentAction();

private:
  void closeEvent(QCloseEvent* event);
  void keyPressEvent(QKeyEvent* event);
  void wheelEvent(QWheelEvent* event);

  /// Clear the list view and add new set of items
  void loadRow(int row);

  /// Update list view items, for example when images are loaded
  void updateItems();

  /**
   * @brief Remove current row if nothing useful is left to see, or reload it to
   * reflect changes
   * @param group modified group of the current row
   */
  void updateCurrentRow(const MediaGroup& current);

  /**
   * @brief Advance to next row if it is possible
   * @param exit if true, then exit the viewer at the end
   */
  void loadNextRow(bool closeAtEnd);

  /**
   * @brief Remove items from the current group
   * @param deleteFiles If true then move files to trash and remove from index
   * @param replace If delete == true and group size == 2, the other item is
   * renamed to the deleted item's name
   * @details If the item is deleted successfully and group contains one item,
   * move to the next group
   */
  void removeSelection(bool deleteFiles, bool replace = false);

  /**
   * @brief Build file for verifying a dataset
   * @param matched If true the selected item matched, if false did not match
   * @details The first item in the group is assumed to be the needle/query
   * image
   */
  void recordMatch(bool matched);

  /**
   * @brief Add items to match exclusion list
   * @param all If true, add all items, not just the selected one
   * @return true if successful
   * @details The negative match list (as a filter) is enabled in the search
   * query options. The exclusion list is always available regardless.
   * todo: apply exclusions to the current data
   */
  bool addNegMatch(bool all);

  /// Move positions of items forward, the last item becomes the first
  void rotateGroup(int row);

  /// Update _list for changes to media (e.g. rename)
  void updateMedia(const QString& path, const Media& m);

  /// Start background jobs for the given row
  void loadMedia(int row);

  /// Block and finish loaders for row (-1 for all rows)
  void waitLoaders(int row = -1, bool cancel=true);

  /// Cancel image loaders except for the given row
  void cancelOtherLoaders(int row);

  /// @return List of selected items as Media objects
  MediaGroup selectedMedia();

  /// Remove analysis media items from all groups
  void removeAnalysis();

  /// Add difference media item to all groups
  void addDifferenceAnalysis();

  /// Estimate additional memory requirement of unloaded or partially loaded row
  float requiredMemory(int row) const;

  /// Try to restore selected item after a layout change
  void restoreSelectedItem(const QModelIndex& last);

  /// Return true if there is a pair displayed and one is selected
  bool selectedPair(Media **selected, Media **other);

  /// Warn about renaming w/o database present
  bool renameWarning();

  /// Move parent of child(file) to newName
  void moveDatabaseDir(const Media& child, const QString& newName);

  /// Menu tree for moving stuff
  QMenu* dirMenu(const char* slot);

  /// Reset zoom and pan
  void resetZoom();

  /// Increase/decrease items per page
  void resizePage(bool more);

  MediaGroupList _list;
  MediaWidgetOptions _options;
  MediaItemDelegate* _itemDelegate;

  QList<QFutureWatcher<void>*> _loaders; // loadMedia() threadpool tasks
  QList<int> _lruRows;                   // least-recently-used rows
  QHash<QString, int> _archiveFileCount; // cache # files in an archive

  int _currentRow = -1;
  double _zoom = 1.0;
  double _panX = 0, _panY = 0;
  bool _autoDifference = false;
  bool _maximized = false;

  QTimer _updateTimer;
};

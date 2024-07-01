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
class MediaPage;
class ImageWork;

/**
 * @class MediaGroupListWidget
 * @brief The MediaGroupListWidget class is used to display and manage a list of
 *        Media objects. Each MediaGroup is displayed on one screenful,
 *        and is resized so there is no horizontal/vertical scroll.
 *
 *        Operations are in the context menu and apply to selected items
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

  /// Rename selected file
  void renameFileAction();

  /// Rename; take name from the other (unselected) item in a pair (keep extension)
  void copyNameAction();

  /// Move folder/zip of selected file to subdir
  void moveFolderAction();

  /// Rename folder/container of selected file
  void renameFolderAction();

  /// Copy image to clipboard
  void copyImageAction();

  /// Set a thumbnail for the index
  void thumbnailAction();

  /// Cycle positions of items
  void rotateAction();

  /// Remove selected items from the view (do not delete files)
  void clearAction() { removeSelection(false); }

  /// Add no-reference quality score to item descriptions
  void qualityScoreAction();

  /// Template match first item to selected item, removing other items
  void templateMatchAction();

  /// Toggle image-pair differences visualization
  void toggleAutoDifferenceAction();

  /// Compare the first item to selected item
  void compareVideosAction();

  /// Compare first item to selected item
  void compareAudioAction();

  /// Reset changes to the current row
  void reloadAction();

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

  /// Scale-up smaller items to match the largest item
  void scaleModeAction();

  // item zoom/pan
  void zoomInAction();
  void zoomOutAction();
  void panLeftAction();
  void panRightAction();
  void panUpAction();
  void panDownAction();
  void resetZoomAction();

  // scaling filter
  void cycleMinFilter();
  void cycleMagFilter();

  // change items per page
  void increasePageSize() { resizePage(true); }
  void decreasePageSize() { resizePage(false); }

  /// When enter key is pressed, emit mediaSelected()
  void chooseAction();

  /// Toggle lock on the selected folder
  void toggleFolderLockAction();

  /// Open new window with all images in the selection's folder
  void browseParentAction();

  // Page navigation
  void nextGroupAction() { loadRow(_currentRow + 1); }
  void prevGroupAction() { loadRow(_currentRow - 1); }
  void jumpForwardAction() { loadRow(_currentRow + 100); }
  void jumpBackAction() { loadRow(_currentRow - 100); }
  void jumpToStartAction() { loadRow(0); }
  void jumpToEndAction() { loadRow(_list.count() - 1); }

 private:
  void closeEvent(QCloseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

  /// Menu tree for moving stuff
  QMenu* dirMenu(const char* slot);

  /**
   * @brief Remove items from the current group
   * @param deleteFiles If true then move files to trash and remove from index
   * @param replace If delete == true and group size == 2, the other item is
   * renamed to the deleted item's name
   * @details If the item is deleted successfully and group contains one item,
   * move to the next group
   */
  void removeSelection(bool deleteFiles, bool replace = false);

  /// Warn about renaming w/o database present
  bool renameWarning();

  /// Move parent of child(file) to newName
  void moveDatabaseDir(const Media& child, const QString& newName);

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

  /// Reset zoom and pan
  void resetZoom();

  /// Increase/decrease items per page
  void resizePage(bool more);

  /// Test if move action should be enabled
  bool selectionIsMoveable();

  /// Test if move action should be enabled
  bool selectionParentIsMoveable();

  /// Remember locked folders
  void loadFolderLocks();
  void saveFolderLocks() const;

  MediaPage* currentPage() const { return _list.at(_currentRow); }

  /// List of media corresponding to and in the same order as list view items
  const MediaGroup& currentGroup() const;

  /// List of selected items as Media objects
  MediaGroup selectedMedia();

  /// true if there is a pair displayed and one is selected
  bool selectedPair(Media** selected, Media** other);

  /// Try to restore selected item after a layout change
  void restoreSelectedItem(const QModelIndex& last);

  /// Set the text block and call update(; call when items change (image loaded etc)
  void updateItems();

  /// Call after deleting or adding items to the current row
  void itemCountChanged();

  /// Replace all items in all rows with the given path (e.g. after move/rename)
  void updateMedia(const QString& path, const Media& m);

  // Sanity check memory usage
  void checkMemoryUsage() const;

  /// Move all pages from _list into _deletedPages
  void deletePages();

  /// Move one page from _list into _deletedPages
  void deletePage(int index);

  /// Block and finish all loaders (not advised, use cancel and timers instead)
  void waitLoaders();

  /// Cancel image loaders except for the given pages
  void cancelOtherLoaders(QSet<const MediaPage*> keep);

  /// Image loader out of memory; free stuff and try again
  void loaderOutOfMemory();

  /// Load one image/videothumb/analysis in the background
  /// @section loading
  void loadOne(MediaPage* page, int index);

  /// Start background jobs for the given page, not necessarily the displayed page
  void loadMedia(MediaPage* page);

  /**
   * @brief Clear the list view and add new set of items, slow parts are processed in the background
   * @param row index into _list we are going to display
   * @param preloadNextRow if true then guess the next row index and
   *                       fetch once this row completes
   */
  void loadRow(int row, bool preloadNextRow=true);


  QVector<MediaPage*> _list;        // model data; each page is a MediaGroup
  MediaWidgetOptions _options;      // global ui options
  MediaItemDelegate* _itemDelegate; // layout and paint items

  QList<ImageWork*> _loaders;       // loadMedia() threadpool tasks
  QList<MediaPage*> _loadedPages;   // least-recently-used rows

  QHash<QString, int> _archiveFileCount;  // cache # files in an archive

  int _currentRow = -1; // index into _list[] for the displayed page

  double _zoom = 1.0; // scale factor (added to scale modes scaling)
  double _panX = 0, _panY = 0;  // translate the view
  bool _autoDifference = false; // if true show the difference image on the right side
  const int _origCount = 0; // count before any deletions for top progress bar

  bool _maximized = false; // true if window was maximized on last exit

  QTimer _updateTimer; // delayed calls to updateItems()

  MediaPage* _preloadPage=nullptr; // optional additional page for load timer
  QTimer _loadTimer; // delayed call to loadMedia()

  QSet<QString> _lockedFolders; // folders we disallow modifications on
  const char* const _FOLDER_LOCKS_FILE="locks.txt";

  QSet<MediaPage*> _deletedPages; // removed and unloaded pages

  QTimer _oomGuard;   // fires occasionally to prevent system oom condition

  QTimer _oomTimer; // fires when we are oom on the image loader
};

#pragma once
#include "index.h"

/**
 * @brief The MediaBrowser class is used to display a MediaGroupList
 *        in different ways.
 */
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

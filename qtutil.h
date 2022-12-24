/* General reusable qt bits and platform abstractions
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

/// portable interface for desktop/platform environment
class DesktopHelper {
 public:
  /// open filesystem location of path, ideally also scroll to
  /// and highlight the item
  static void revealPath(const QString& path);

  /// open video player with optional starting time
  static void openVideo(const QString& path, double seekSeconds = 0.0);

  /// open audio track (visual) comparison tool
  static void compareAudio(const QString& path1, const QString& path2);

  /// play two videos side-by-side with seek for alignment
  static void playSideBySide(const QString& path1, double seek1,
                             const QString& path2, double seek2);

  /// location of settings file
  static QString settingsFile();

  /// move file to trash, do not overwrite existing files
  static bool moveToTrash(const QString& path);

  /**
   * @brief get temporary file path in /tmp that cleans itself up
   * @param nameTemplate QTemporaryFile template
   * @param parent       if non-null, delete temporary with parent
   * @param maxLifetime  seconds, if > 0, delete temporary this
   * @return path to temporary file
   * @note this only exists because QTemporaryFile holds the file
   *       open, which makes it impossible to pass the file name
   *       to another process on win32 (due to exclusive open)
   */
  static QString tempName(const QString& templateName, QObject* parent=nullptr, int maxLifetime=60);

 private:
  static void runProgram(QStringList& args, bool wait,
                         const QString& inPath = "", double seek = 0.0,
                         const QString& inPath2 = "", double seek2 = 0.0);
  static QVariant getSetting(const QString& key, const QVariant& defaultValue);
  static void putSetting(const QString& key, const QVariant& value);
  static bool chooseProgram(QStringList& args,
                            const QVector<QStringList>& options,
                            const char* settingsKey, const char* dialogTitle,
                            const char* dialogText);
  static bool moveFile(const QString& path, const QString& dir);
};

/// common tasks for widgets
class WidgetHelper {
 public:
  /// save position/size and maximized state of a top-level widget
  static void saveGeometry(const QWidget* w, const char* id = nullptr);

  /// restore position/size and maximized state of a top-level widget
  /// @return true if widget was maximized
  static bool restoreGeometry(QWidget* w, const char* id = nullptr);

  /// save table column widths, sort column etc
  static void saveTableState(const QTableView* w, const char* id = nullptr);

  /// restore table properties
  /// @return true if properties were set
  static bool restoreTableState(QTableView* w, const char* id = nullptr);

  /// one-liner for adding actions
  static QAction* addAction(QSettings& settings, const QString& label,
                            const QKeySequence& shortcut, QWidget* target,
                            const char* slot);

  static QAction* addAction(QSettings& settings, const QString& label,
                            const QKeySequence& shortcut, QWidget* target,
                            std::function<void(void)> fn);

  static QAction* addSeparatorAction(QWidget* parent);

  /// draw rich text using global style
  static void drawRichText(QPainter* painter, const QRect& r,
                           const QString& text);

  static QKeySequence getShortcut(QSettings& settings, const QString& label,
                                  const QKeySequence& defaultShortcut);
};

class DBHelper {
 public:
  static bool isCacheFileStale(const QSqlDatabase& db, const QString& cacheFile) {
    QFileInfo cacheInfo(cacheFile);
    return !cacheInfo.exists() ||
           lastModified(db) > cacheInfo.lastModified();
  }

  static QDateTime lastModified(const QSqlDatabase& db);
};

/// helpers for menus/context menus
class MenuHelper {
 public:
  /// build menu for a directory tree
  static QMenu* dirMenu(const QString& root, QObject* target,
                        const char* slot, int maxDepth=INT_MAX);

 private:
  static QMenu* makeDirMenu(const QString& root, QObject* target,
                            const char* slot, int maxDepth, int depth);
};

/// elide string in the middle
QString qElide(const QString& str, int maxLen = 80);

/// get rotation angle represented by matrix
double qRotationAngle(const QTransform& mat);

/// use with QObject::installEventFilter to log events
/// and signal/slot invocations
class DebugEventFilter : public QObject {
 public:
  DebugEventFilter();
  virtual ~DebugEventFilter();
  bool eventFilter(QObject* object, QEvent* event);
};

/// custom logger with compression, color, etc
void qColorMessageOutput(QtMsgType type, const QMessageLogContext& context,
                         const QString& msg);

/// flush log messages, required before using printf/scanf etc
void qFlushOutput();

/// log message extra per-thread context
QThreadStorage<QString>& qMessageContext();

/// scoped log message extra context (preferred method)
class MessageContext {
  NO_COPY_NO_DEFAULT(MessageContext, QObject);
 public:
  MessageContext(const QString& context);
  ~MessageContext();
};

#if QT_VERSION_MAJOR > 5
QPartialOrdering qVariantCompare(const QVariant& a, const QVariant& b);

Q_ALWAYS_INLINE bool operator<(const QVariant& a, const QVariant& b) {
  return qVariantCompare(a,b) == QPartialOrdering::Less;
}

Q_ALWAYS_INLINE bool operator>(const QVariant& a, const QVariant& b) {
  return qVariantCompare(a,b) == QPartialOrdering::Greater;
}

Q_ALWAYS_INLINE bool operator<=(const QVariant& a, const QVariant& b) {
  auto ord = qVariantCompare(a,b);
  return ord == QPartialOrdering::Less || ord == QPartialOrdering::Equivalent;
}

Q_ALWAYS_INLINE bool operator>=(const QVariant& a, const QVariant& b) {
  auto ord = qVariantCompare(a,b);
  return ord == QPartialOrdering::Greater || ord == QPartialOrdering::Equivalent;
}
#endif

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
  static void openVideo(const QString& path, double seekSeconds);

  /// open video player
  static void openVideo(const QString& path);

  /// open audio track (visual) comparison tool
  static void compareAudio(const QString& path1, const QString& path2);

  /// play two videos side-by-side with seek for alignment
  static void playSideBySide(const QString& path1, double seek1, const QString& path2,
                             double seek2);

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
  static QString tempName(const QString& templateName, QObject* parent = nullptr,
                          int maxLifetime = 60);

 private:
  static bool runProgram(QStringList& args, bool wait, const QString& inPath = "",
                         double seek = 0.0, const QString& inPath2 = "", double seek2 = 0.0);
  static QVariant getSetting(const QString& key, const QVariant& defaultValue);
  static void putSetting(const QString& key, const QVariant& value);
  static bool chooseProgram(QStringList& args, const QVector<QStringList>& options,
                            const QString& settingsKey, const QString& dialogTitle,
                            const QString& dialogText);
  static bool moveFile(const QString& path, const QString& dir);
};

class QTableView;
class QSettings;
class QAction;
class QSqlDatabase;
class QSqlQuery;
class QMenu;
class QCollator;
template<class T>
class QThreadStorage;

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
  static QAction* addAction(QSettings& settings, const QString& label, const QKeySequence& shortcut,
                            QWidget* target, const char* slot);

  static QAction* addAction(QSettings& settings, const QString& label, const QKeySequence& shortcut,
                            QWidget* target, std::function<void(void)> fn);

  static QAction* addSeparatorAction(QWidget* parent);

  static QKeySequence getShortcut(QSettings& settings, const QString& label,
                                  const QKeySequence& defaultShortcut);

  /// apply hacks to show window, like avoiding white flash on windows
  static void hackShowWindow(QWidget* window, bool maximized = false);

  /// if true, make an offscreen window. used to hide white flash on Windows 10
  static void setWindowCloak(QWidget* window, bool enable);

  /// style platform titlebar, etc not accessible to Qt/stylesheet
  static void setWindowTheme(QWidget* window, bool dark = true);
};

class DBHelper {
 public:
  static bool isCacheFileStale(const QSqlDatabase& db, const QString& cacheFile);

  static size_t rowCount(QSqlQuery& query, const QString& tableName);

  static QDateTime lastModified(const QSqlDatabase& db);
};

/// helpers for menus/context menus
class MenuHelper {
 public:
  /// build menu for a directory tree
 static QMenu* dirMenu(
     const QString& root, QWidget* target, const char* slot, int maxFolders=20, int maxDepth = INT_MAX);

 private:
 static QMenu* makeDirMenu(const QString& root,
                           QWidget* target,
                           const char* slot,
                           int maxFolders,
                           int maxDepth,
                           int depth);
};

/**
 * @brief compare strings by segmenting into numeric and non-numeric parts
 * @param cmp (probably locale-aware) compare function for the non-numeric part
 * @return -1, 0, or 1
 */
int qNumericSubstringCompare(const QCollator& cmp, const QStringView& a, const QStringView& b);

/// elide string in the middle
QString qElide(const QString& str, int maxLen = 80);

/// rotation angle represented by matrix
double qRotationAngle(const QTransform& mat);

/// use with QObject::installEventFilter to log events
/// and signal/slot invocations
class DebugEventFilter : public QObject {
 public:
  DebugEventFilter();
  virtual ~DebugEventFilter();
  bool eventFilter(QObject* object, QEvent* event);
};

/// custom log handler with compression, color, etc,
/// enable with qInstallMessageHandler(qColorMessageOutput)
void qColorMessageOutput(QtMsgType type, const QMessageLogContext& context, const QString& msg);

/// flush logger, required before using printf/scanf etc
void qFlushMessageLog();

/// if enable=false, ignore logs with matching QMessageLogContext::category
void qMessageLogCategoryEnable(const QString& category, bool enable);

/// log message extra per-thread context, e.g. the currently active file
/// note: const because not using MessageContext stack will mess it up
const QThreadStorage<QString>& qMessageContext();

/// scoped log message extra context (preferred over qMessageContext())
/// note: must always be a stack allocated object
class MessageContext {
  NO_COPY_NO_DEFAULT(MessageContext, QObject);
  QString _savedContext;
 public:
  MessageContext(const QString& context);  // set current message context
  ~MessageContext();
  /// set current context; saved context is unmodified
  void reset(const QString& context);
};

#if QT_VERSION_MAJOR > 5
QPartialOrdering qVariantCompare(const QVariant& a, const QVariant& b);

Q_ALWAYS_INLINE bool operator<(const QVariant& a, const QVariant& b) {
  return qVariantCompare(a, b) == QPartialOrdering::Less;
}

Q_ALWAYS_INLINE bool operator>(const QVariant& a, const QVariant& b) {
  return qVariantCompare(a, b) == QPartialOrdering::Greater;
}

Q_ALWAYS_INLINE bool operator<=(const QVariant& a, const QVariant& b) {
  auto ord = qVariantCompare(a, b);
  return ord == QPartialOrdering::Less || ord == QPartialOrdering::Equivalent;
}

Q_ALWAYS_INLINE bool operator>=(const QVariant& a, const QVariant& b) {
  auto ord = qVariantCompare(a, b);
  return ord == QPartialOrdering::Greater || ord == QPartialOrdering::Equivalent;
}
#endif

class ProgressLogger {
  Q_DISABLE_COPY_MOVE(ProgressLogger);
  ProgressLogger() = delete;

  static bool _alwaysShow;

  const QString _format;
  const uint64_t _max;
  const QMessageLogContext _context;
  // const QLocale _locale;

  QElapsedTimer _hideTimer, _rateLimitTimer;
  bool _showLast = false;

  void formatString(QString& str, uint64_t step, const QVariantList& args={}) const;

 public:
  static void setAlwaysShow(bool show) { _alwaysShow = show; }

  ProgressLogger(const QString& format, uint64_t maxStep, const char* contextFunc);

  // thread-safe version
  void step(uint64_t step, const QVariantList& args={}) const;

  // not thread-safe, limits logs to 10 per second
  void stepRateLimited(uint64_t step, const QVariantList& args = {});

  // final log (100% if step==0)
  void end(uint64_t step = 0, const QVariantList& args = {}) const;

  /// always show final progress line
  void showLast() { _showLast = true; }
};

#define PROGRESS_LOGGER(__name, __format, __max) \
  ProgressLogger __name((__format), (__max), QT_MESSAGELOG_FUNC)

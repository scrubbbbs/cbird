/* Theme management
   Copyright (C) 2023 scrubbbbs
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

/**
 * @brief The Theme class initialized the theme, provides style information,
 *        and provides utilities for common tasks like dialogs
 */
class Theme : public QWidget {
  Q_OBJECT
  NO_COPY_NO_DEFAULT(Theme, QWidget);

  /// various properties read from cbird.qss
  /// @see cbird-richtext.css for how this gets used
  Q_PROPERTY(QString style MEMBER _style);

  Q_PROPERTY(QColor more_base MEMBER _more_base); // background color of "more" color
  Q_PROPERTY(QColor less_base MEMBER _less_base); // background color of "less" color
  Q_PROPERTY(QColor same_base MEMBER _same_base);
  Q_PROPERTY(QColor time_base MEMBER _time_base);
  Q_PROPERTY(QColor video_base MEMBER _video_base);
  Q_PROPERTY(QColor audio_base MEMBER _audio_base);
  Q_PROPERTY(QColor archive_base MEMBER _archive_base);
  Q_PROPERTY(QColor file_base MEMBER _file_base);
  Q_PROPERTY(QColor weed_base MEMBER _weed_base);
  Q_PROPERTY(QColor locked_base MEMBER _locked_base);

  Q_PROPERTY(QColor more_altbase MEMBER _more_altbase); // alternate background color
  Q_PROPERTY(QColor less_altbase MEMBER _less_altbase);
  Q_PROPERTY(QColor same_altbase MEMBER _same_altbase);
  Q_PROPERTY(QColor time_altbase MEMBER _time_altbase);
  Q_PROPERTY(QColor video_altbase MEMBER _video_altbase);
  Q_PROPERTY(QColor audio_altbase MEMBER _audio_altbase);
  Q_PROPERTY(QColor archive_altbase MEMBER _archive_altbase);
  Q_PROPERTY(QColor file_altbase MEMBER _file_altbase);
  Q_PROPERTY(QColor weed_altbase MEMBER _weed_altbase);
  Q_PROPERTY(QColor locked_altbase MEMBER _locked_altbase);

 public:
  /// set default style; could be "Auto" in which case the final style would be different
  static void setDefaultStyle(const QString& style);

  /// initialize system theme, call before creating windows
  static void setup();

  /// singleton pattern
  static Theme& instance();

  /// css stylesheet for QTextDocument etc (not widget theme)
  QString richTextStyleSheet() const;

  /// draw themed rich text using QTextDocument
  void drawRichText(QPainter* painter, const QRect& r, const QString& text);

  void showWindow(QWidget* window, bool maximized = false) const;

  int execDialog(QDialog* dialog) const;

  int execDialog(QMessageBox* dialog) const;

  int execInputDialog(QInputDialog* dialog, const QString& title, const QString& label,
                      const QString& text, const QStringList& completions = {}) const;

  QString getExistingDirectory(const QString& action, const QString &label, const QString& dirPath, QWidget* parent) const;

  static constexpr float INFO_OPACITY = 0.5; // opacity for extra information (not primary)
  static constexpr float SELECTION_OPACITY = 0.5; // opacity for selections

 private:
  Theme(QWidget* parent);
  ~Theme();

  /// read QSS or otherwise style information from realized widget
  void probe();

  /// apply our own polish separate from QStyle
  void polishWindow(QWidget* window) const;

  /// display color picker tool for our own colors (not supplied by QStyle)
  static void showToolbox();

  static QString* _defaultStyle;

  QStyle* _baseStyle = nullptr;  // style before any stylesheets added

  QString _style;  // "Auto", "Qt", "Dark", "Light"

  QColor _base, _altBase, _text;

  QColor _more_base, _less_base, _same_base, _time_base, _video_base, _audio_base, _archive_base,
      _file_base, _weed_base, _locked_base;
  QColor _more_altbase, _less_altbase, _same_altbase, _time_altbase, _video_altbase, _audio_altbase,
      _archive_altbase, _file_altbase, _weed_altbase, _locked_altbase;

  bool _toolboxActive = false;
};

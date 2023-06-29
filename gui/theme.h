
class Theme : public QWidget {
  Q_OBJECT
  NO_COPY_NO_DEFAULT(Theme, QWidget);

  Q_PROPERTY(QString style MEMBER _style);

  Q_PROPERTY(QColor more_base MEMBER _more_base);
  Q_PROPERTY(QColor less_base MEMBER _less_base);
  Q_PROPERTY(QColor same_base MEMBER _same_base);
  Q_PROPERTY(QColor time_base MEMBER _time_base);
  Q_PROPERTY(QColor video_base MEMBER _video_base);
  Q_PROPERTY(QColor audio_base MEMBER _audio_base);
  Q_PROPERTY(QColor archive_base MEMBER _archive_base);
  Q_PROPERTY(QColor file_base MEMBER _file_base);
  Q_PROPERTY(QColor default_base MEMBER _default_base);
  Q_PROPERTY(QColor weed_base MEMBER _weed_base);

  Q_PROPERTY(QColor more_altbase MEMBER _more_altbase);
  Q_PROPERTY(QColor less_altbase MEMBER _less_altbase);
  Q_PROPERTY(QColor same_altbase MEMBER _same_altbase);
  Q_PROPERTY(QColor time_altbase MEMBER _time_altbase);
  Q_PROPERTY(QColor video_altbase MEMBER _video_altbase);
  Q_PROPERTY(QColor audio_altbase MEMBER _audio_altbase);
  Q_PROPERTY(QColor archive_altbase MEMBER _archive_altbase);
  Q_PROPERTY(QColor file_altbase MEMBER _file_altbase);
  Q_PROPERTY(QColor default_altbase MEMBER _default_altbase);
  Q_PROPERTY(QColor weed_altbase MEMBER _weed_altbase);

 public:
  static void setup();
  static Theme& instance();

  /// get css stylesheet for QTextDocument etc (not widget theme)
  QString richTextStyleSheet() const;
  
  /// draw themed rich text using QTextDocument
  void drawRichText(QPainter* painter, const QRect& r, const QString& text);

  /// void themed window show
  void showWindow(QWidget* window, bool maximized=false) const;

  /// apply theme hacks and call exec()
  int execDialog(QDialog* dialog) const;

  /// themed input dialog
  int execInputDialog(QInputDialog* dialog, const QString& title,
                      const QString& label, const QString& text,
                      const QStringList& completions = {}) const;

  QString getExistingDirectory(const QString& title, const QString& dirPath,
                               QWidget* parent) const;

 private:
  Theme(QWidget* parent);
  ~Theme();

  void probe();
  void polishWindow(QWidget* window) const;

  static void showToolbox();

  QStyle* _baseStyle = nullptr; // style before any stylesheets added

  QString _style; // "Qt", "Dark", "Light"

  QColor _base, _altBase, _text;

  QColor _more_base, _less_base, _same_base, _time_base, _video_base,
      _audio_base, _archive_base, _file_base, _default_base, _weed_base;
  QColor _more_altbase, _less_altbase, _same_altbase, _time_altbase, _video_altbase,
      _audio_altbase, _archive_altbase, _file_altbase, _default_altbase, _weed_altbase;

  bool _toolboxActive = false;
};


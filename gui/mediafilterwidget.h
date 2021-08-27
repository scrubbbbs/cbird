#pragma once

/**
 * @brief The MediaFilterWidget class provides a UI for
 *        filtering MediaGroupTableWidget
 */
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

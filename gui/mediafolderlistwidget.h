#pragma once
#include "media.h"

class Database;

class MediaFolderListWidget : public QListWidget {
  Q_OBJECT
  NO_COPY_NO_DEFAULT(MediaFolderListWidget, QListWidget)

 public:
  MediaFolderListWidget(const MediaGroup& list, const QString& basePath,
                        Database* db = nullptr, QWidget* parent = nullptr);
  virtual ~MediaFolderListWidget();

 Q_SIGNALS:
  void mediaSelected(const MediaGroup& group);

 private Q_SLOTS:
  void chooseAction();
  void moveFolderAction();

 private:
  void closeEvent(QCloseEvent* event);
  void close();

  MediaGroup selectedMedia() const;

  MediaGroup _list;
  QString _basePath;
  Database* _db = nullptr;
  float requiredMemory(int row) const;
};

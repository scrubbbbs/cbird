#pragma once
#include "index.h"

/**
 * @class ColorDescIndex
 * @brief Index for ColorDescriptor
 *
 * Detects images with similar colors
 */
class ColorDescIndex : public Index {
  Q_DISABLE_COPY_MOVE(ColorDescIndex)

 public:
  ColorDescIndex();
  ~ColorDescIndex() override;

  void createTables(QSqlDatabase& db) const override;
  void addRecords(QSqlDatabase& db, const MediaGroup& media) const override;
  void removeRecords(QSqlDatabase& db,
                     const QVector<int>& mediaIds) const override;

  bool isLoaded() const override;
  size_t memoryUsage() const override;
  int count() const override;
  void load(QSqlDatabase& db, const QString& cachePath,
            const QString& dataPath) override;
  void save(QSqlDatabase& db, const QString& cachePath) override;
  void add(const MediaGroup& media) override;
  void remove(const QVector<int>& id) override;
  QVector<Index::Match> find(const Media& m, const SearchParams& p) override;
  bool findIndexData(Media& m) const override;

  Index* slice(const QSet<uint32_t>& mediaIds) const override;

 private:
  void unload();

  int _count;
  uint32_t* _mediaId;
  ColorDescriptor* _descriptors;
};

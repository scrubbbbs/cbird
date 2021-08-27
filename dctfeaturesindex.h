#pragma once

#include "index.h"

class HammingTree;

/**
 * @class DctFeaturesIndex
 * @brief Index of a feature-based matcher using DCT descriptors
 *
 * The descriptors do not support rotation, unlike ORB. However
 * they are much smaller and good at detecting cropping
 */
class DctFeaturesIndex : public Index {
  Q_DISABLE_COPY_MOVE(DctFeaturesIndex)

 public:
  DctFeaturesIndex();
  ~DctFeaturesIndex() override;

  void createTables(QSqlDatabase& db) const override;
  void addRecords(QSqlDatabase& db, const MediaGroup& media) const override;
  void removeRecords(QSqlDatabase& db,
                     const QVector<int>& mediaIds) const override;

  int count() const override;
  bool isLoaded() const override;
  size_t memoryUsage() const override;

  void load(QSqlDatabase& db, const QString& cachePath,
            const QString& dataPath) override;
  void save(QSqlDatabase& db, const QString& cachePath) override;

  void add(const MediaGroup& media) override;
  void remove(const QVector<int>& id) override;

  QVector<Index::Match> find(const Media& m, const SearchParams& p) override;

  Index* slice(const QSet<uint32_t>& mediaIds) const override;

 private:
  void init();
  void unload();
  HammingTree* _tree;
};

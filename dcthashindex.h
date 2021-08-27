#pragma once

#include "index.h"

/**
 * @class DctHashIndex
 * @brief Index for 64-bit dct hash that uses hamming distance
 */
class DctHashIndex : public Index {
  Q_DISABLE_COPY_MOVE(DctHashIndex)

 public:
  DctHashIndex();
  ~DctHashIndex() override;

 public:
  bool isLoaded() const override { return _isLoaded; }
  int count() const override { return _numHashes; }
  size_t memoryUsage() const override;

  void add(const MediaGroup& media) override;
  void remove(const QVector<int>& ids) override;

  void load(QSqlDatabase& db, const QString& cacheFile,
            const QString& dataPath) override;
  void save(QSqlDatabase& db, const QString& cachePath) override;

  QVector<Index::Match> find(const Media& m, const SearchParams& p) override;

  Index* slice(const QSet<uint32_t>& mediaIds) const override;

  uint64_t hashForMedia(const Media& m) { return m.dctHash(); }
  QString hashQuery() const {
    return "select id,phash_dct from media where type=1";
  }

 private:
  void unload();
  class DctTree* _tree;
  uint64_t* _hashes;
  uint32_t* _mediaId;
  int _numHashes;
  bool _isLoaded;
  void init();
  void buildTree();
};

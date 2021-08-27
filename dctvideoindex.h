#pragma once
#include "index.h"

class HammingTree;

/**
 * @class DctVideoIndex
 * @brief Detect similar videos with full-frame dct hashes
 */
class DctVideoIndex : public Index {
  Q_DISABLE_COPY_MOVE(DctVideoIndex)

 public:
  DctVideoIndex();
  virtual ~DctVideoIndex();

 public:
  virtual bool isLoaded() const;
  virtual int count() const;
  virtual size_t memoryUsage() const;
  virtual void load(QSqlDatabase& db, const QString& cachePath,
                    const QString& dataPath);
  virtual void save(QSqlDatabase& db, const QString& cachePath);
  virtual void add(const MediaGroup& media);
  virtual void remove(const QVector<int>& id);
  virtual QVector<Index::Match> find(const Media& m, const SearchParams& p);
  virtual Index* slice(const QSet<uint32_t>& mediaIds) const;

 private:
  QVector<Index::Match> findFrame(const Media& needle,
                                  const SearchParams& params);
  QVector<Index::Match> findVideo(const Media& needle,
                                  const SearchParams& params);
  void insertHashes(int mediaIndex, HammingTree* tree,
                     const SearchParams& params);
  void buildTree(const SearchParams& params);

  HammingTree* _tree;
  std::vector<uint32_t> _mediaId;
  QString _dataPath;
  std::map<uint32_t, HammingTree*> _cachedIndex;
  QMutex _mutex;
  bool _isLoaded;
};

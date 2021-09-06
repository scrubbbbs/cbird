/* Binary-ish search tree for DCT hashes
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
#include "../hamm.h"

#include <malloc.h>
#include <unordered_set>

/**
 * @class HammingTree
 * @brief Clustered binary tree for 64-bit dct hash
 *
 * Divides the search space using a binary tree. The division is by
 * the least-significant bit of the hash, which works pretty well since
 * they represent the lower spatial frequencies.
 *
 * However, this means a search can go down the wrong path, so this is not
 * an ideal solution in the general case. It is best if there are multiple
 * hashes for each target (e.g. video). The probability of missing goes up as
 * the depth of the tree increases.
 *
 * Since each tree level tests a single bit (of a 64-bit hash), the depth of the tree
 * is limited to 64, in which case the leaves of tree can grow arbitrarily large.
 *
 * The leaves of the tree are large chunks (CLUSTER_SIZE) which can be searched
 * very quickly and reduce the miss rate somewhat.
 */
class HammingTree {
 public:
  // minimum size in bytes of a leaf node
  // on a full tree (depth 64) can grow indefinitely
  enum { CLUSTER_SIZE = 64 * 1024 };

  typedef uint32_t index_t;
  typedef uint64_t hash_t;
  typedef int distance_t;

  /// Node value type
  struct Value {
    index_t index;
    hash_t hash;
    Value(index_t index_, hash_t hash_) : index(index_), hash(hash_) {}
  };

  /// Search traversal type
  struct Match {
    Value value;
    distance_t distance;
    Match() : value(-1, 0), distance(-1) {}
    Match(const Value& value_, distance_t distance_)
        : value(value_), distance(distance_) {}
    bool operator<(const Match& m) const { return distance < m.distance; }
  };

  /// Stats traversal type
  struct Stats {
    size_t memory;
    int numNodes;
    int maxHeight;
    int numValues;
    Stats()
        : memory(sizeof(HammingTree)),
          numNodes(0),
          maxHeight(0),
          numValues(0) {}
  };

  HammingTree() { init(); }
  ~HammingTree() { clear(); }

  /// Find hash with distance(hash, cand) < threshold
  void search(hash_t hash, distance_t threshold,
              std::vector<Match>& matches) const {
    if (_root) {
      search(_root, hash, threshold, matches);
      std::sort(matches.begin(), matches.end());
    }
  }

  /// Find Value with index
  void findIndex(index_t index, std::vector<hash_t>& results) const {
    if (_root) findIndex(_root, index, results);
  }

  /// Add more nodes
  void insert(std::vector<Value>& values) {
    _count += values.size();

    if (!_root) _root = new Level;

    insert(_root, values, 0);
  }

  /// Remove nodes
  void remove(std::unordered_set<index_t>& indexSet) {
    if (_root) remove(_root, indexSet);
  }

  /// Copy a subtree; method to multithread searches
  HammingTree* slice(const std::unordered_set<index_t>& indexSet) {
    HammingTree* tree = new HammingTree;
    if (_root) {
      std::vector<HammingTree::Value> values;
      tree->slice(_root, indexSet, tree, values);
      tree->insert(values);
    }
    return tree;
  }

  /// Get some stats, like memory usage
  Stats stats() const {
    Stats st;

    if (_root) stats(_root, st, 0);

    return st;
  }

  /// Read tree from file
  void read(const char* file) {
    FILE* fp = fopen(file, "rb");
    Q_ASSERT(fp);
    _root = read(fp);
    fclose(fp);
  }

  /// Write tree to file
  void write(const char* file) {
    if (_root) {
      FILE* fp = fopen(file, "wb");
      Q_ASSERT(fp);
      write(_root, fp);
      fclose(fp);
    }
  }

  /// Print the tree structure
  void print() {
    size_t bytes = printLevel(_root, 0);

    qInfo("size=%d MB", (int)(bytes / 1024 / 1024));
    qInfo("memory factor=%d", (int)(bytes / (_count * sizeof(Value))));
  }

  /// @return number of Values
  size_t size() const { return _count; }

 private:
  struct Level {
    Level* left;
    Level* right;
    int bit;
    hash_t* hashes;
    size_t count;
    index_t* indices;

    Level()
        : left(nullptr),
          right(nullptr),
          bit(-1),
          hashes(nullptr),
          count(0),
          indices(nullptr){};

    ~Level() {
      delete left;
      delete right;
      if (indices) free(indices);
      if (hashes) free(hashes);
    }
  };

  static void partition(int bit, const std::vector<Value>& values,
                        std::vector<Value>& left, std::vector<Value>& right) {
    for (const Value& v : values)
      if (v.hash & (1 << bit))
        left.push_back(v);
      else
        right.push_back(v);
  }

  static int getBit(int depth) { return depth; }

  static void search(const Level* level, hash_t hash, distance_t threshold,
                     std::vector<Match>& matches) {
    if (level->left != nullptr) {
      if ((1 << level->bit) & hash)
        search(level->left, hash, threshold, matches);
      else
        search(level->right, hash, threshold, matches);
    } else {
      const hash_t* hashes = level->hashes;
      const index_t* indices = level->indices;
      const size_t count = level->count;

      Q_ASSERT(malloc_usable_size((void*)hashes) >= count * sizeof(*hashes));
      Q_ASSERT(malloc_usable_size((void*)indices) >= count * sizeof(*indices));

      for (size_t i = 0; i < count; i++) {
        distance_t distance = hamm64(hash, hashes[i]);
        if (distance < threshold)
          matches.push_back(Match(Value(indices[i], hashes[i]), distance));
      }
    }
  }

  static void findIndex(const Level* level, index_t index,
                        std::vector<hash_t>& results) {
    if (level->left) {
      findIndex(level->left, index, results);
      findIndex(level->right, index, results);
    } else {
      const hash_t* hashes = level->hashes;
      const index_t* indices = level->indices;
      const size_t count = level->count;

      for (size_t i = 0; i < count; i++)
        if (indices[i] == index) results.push_back(hashes[i]);
    }
  }

  static void slice(const Level* level,
                    const std::unordered_set<index_t>& indexSet,
                    HammingTree* tree,
                    std::vector<HammingTree::Value>& values) {
    if (level->left) {
      slice(level->left, indexSet, tree, values);
      slice(level->right, indexSet, tree, values);
    } else {
      index_t* indices = level->indices;
      const size_t count = level->count;
      const auto& end = indexSet.cend();

      for (size_t i = 0; i < count; i++)
        if (indexSet.find(indices[i]) != end)
          values.push_back(
              HammingTree::Value(level->indices[i], level->hashes[i]));

      if (values.size() > 100000) {
        tree->insert(values);
        values.clear();
      }
    }
  }

  static void remove(Level* level,
                     const std::unordered_set<index_t>& indexSet) {
    if (level->left) {
      remove(level->left, indexSet);
      remove(level->right, indexSet);
    } else {
      index_t* indices = level->indices;
      const size_t count = level->count;
      const auto& end = indexSet.cend();

      for (size_t i = 0; i < count; i++)
        if (indexSet.find(indices[i]) != end) indices[i] = 0;
    }
  }

  static void insert(Level* level, const std::vector<Value>& values,
                     int depth) {
    Q_ASSERT(depth < 64);
    if (values.size() <= 0) return;

    if (depth < 63 && level->left) {
      // level is internal, keep traversing
      std::vector<Value> left, right;
      partition(level->bit, values, left, right);

      insert(level->left, left, depth + 1);
      insert(level->right, right, depth + 1);
    }
    else if (depth < 63 &&
             level->count + values.size() > (CLUSTER_SIZE / sizeof(hash_t))) {
      // level (cluster) is full, chop it up
      int bit = getBit(depth);
      level->bit = bit;
      std::vector<Value> left, right;
      partition(level->bit, values, left, right);

      // this is almost the same as partition, can it be changed?
      for (size_t i = 0; i < level->count; i++) {
        index_t index = level->indices[i];
        hash_t hash = level->hashes[i];
        Value value(index, hash);
        if (hash & (1 << bit))
          left.push_back(value);
        else
          right.push_back(value);
      }

      free(level->indices);
      free(level->hashes);
      level->indices = nullptr;
      level->hashes = nullptr;
      level->count = 0;

      level->left = new Level;
      level->right = new Level;

      insert(level->left, left, depth + 1);
      insert(level->right, right, depth + 1);
    } else {
      // leaf is not full, add some more
      size_t offset = level->count;

      level->count += values.size();
      level->indices = strict_realloc(level->indices, level->count);
      level->hashes = strict_realloc(level->hashes, level->count);

      Q_ASSERT(level->count);
      Q_ASSERT(level->indices);
      Q_ASSERT(level->hashes);
      Q_ASSERT(malloc_usable_size((void*)level->hashes) >=
               level->count * sizeof(*level->hashes));
      Q_ASSERT(malloc_usable_size((void*)level->indices) >=
               level->count * sizeof(*level->indices));

      for (size_t i = 0; i < values.size(); i++) {
        level->indices[offset + i] = values[i].index;
        level->hashes[offset + i] = values[i].hash;
      }
    }
  }

  static Level* read(FILE* fp) {
    Level* level = new Level;

    bool isLeaf;
    Q_ASSERT(fread(&isLeaf, sizeof(isLeaf), 1, fp) == 1);

    if (feof(fp))
      ;
    else if (!isLeaf) {
      Q_ASSERT(fread(&level->bit, sizeof(level->bit), 1, fp) == 1);

      level->left = read(fp);
      level->right = read(fp);
    } else {
      Q_ASSERT(fread(&level->count, sizeof(level->count), 1, fp) == 1);
      if (level->count > 0) {
        size_t size = sizeof(*level->indices) * level->count;
        level->indices = (index_t*)malloc(size);
        Q_ASSERT(fread(level->indices, size, 1, fp) == 1);

        size = sizeof(*level->hashes) * level->count;
        level->hashes = (hash_t*)malloc(size);
        Q_ASSERT(fread(level->hashes, size, 1, fp) == 1);

        Q_ASSERT(malloc_usable_size((void*)level->hashes) >=
               level->count * sizeof(*level->hashes));
        Q_ASSERT(malloc_usable_size((void*)level->indices) >=
               level->count * sizeof(*level->indices));
      }
    }

    return level;
  }

  static void write(const Level* level, FILE* fp) {
    if (level->left) {
      bool isLeaf = false;
      fwrite(&isLeaf, sizeof(isLeaf), 1, fp);
      fwrite(&level->bit, sizeof(level->bit), 1, fp);
      write(level->left, fp);
      write(level->right, fp);
    } else {
      // todo: compact entries that have been "removed" (index == 0)
      bool isLeaf = true;
      fwrite(&isLeaf, sizeof(isLeaf), 1, fp);
      fwrite(&level->count, sizeof(level->count), 1, fp);
      if (level->count > 0) {
        fwrite(level->indices, sizeof(*level->indices) * level->count, 1, fp);
        fwrite(level->hashes, sizeof(*level->hashes) * level->count, 1, fp);
      }
    }
  }

  static void stats(const Level* level, Stats& st, int height) {
    st.numNodes++;
    st.maxHeight = std::max(st.maxHeight, height);
    st.memory += sizeof(Level);
    st.memory += level->count * (sizeof(index_t) + sizeof(hash_t));
    st.numValues += level->count;

    if (level->left) {
      stats(level->left, st, height + 1);
      stats(level->right, st, height + 1);
    }
  }

  void init() { _root = nullptr, _count = 0; }
  void clear() {
    delete _root;
    init();
  }
  bool empty() const { return _root == nullptr; }

  size_t printLevel(Level* level, int depth) {
    size_t bytes =
        sizeof(*level) + sizeof(Value) * level->count + sizeof(Level*) * 2;

    qInfo("%-*s bit=%d nChildren=%d nHash=%d",
          depth, "", level->bit, level->left ? 2 : 0,
           (int)level->count);

    if (level->left) {
      bytes += printLevel(level->left, depth + 1);
      bytes += printLevel(level->right, depth + 1);
    }

    return bytes;
  }

  Level* _root;
  size_t _count;
};

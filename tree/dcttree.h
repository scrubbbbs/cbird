/* DCT hash search tree wrappers
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
#include "../index.h"

#define VPTREE (1)
//#define LIBVPTREE (1)
//#define HAMMINGTREE (1)

// least-significant-bit tree for dct hash,
// very fast but lower hit rate ~90%
// runtime does not vary with threshold value
#if HAMMINGTREE
#include "hammingtree.h"

class DctTree {
 public:
  HammingTree _tree;

  void create(uint64_t* hashes, uint32_t* ids, int numHashes) {
    std::vector<HammingTree::Value> values;
    for (int i = 0; i < numHashes; ++i)
      values.push_back( HammingTree::Value(ids[i], hashes[i]) );
    _tree.insert(values);
    HammingTree::Stats stats = _tree.stats();
    qInfo() << "height" << stats.maxHeight;
  }

  QVector<Index::Match> search(uint64_t target, int threshold) {
    QVector<Index::Match> matches;
    std::vector<HammingTree::Match> results;
    _tree.search(target, threshold, results);
    for (auto& r : results)
      matches.append( Index::Match(r.value.index, r.distance) );
    return matches;
  }
};

#endif

// vptree library, slower but has knn
// runtime gets much worse as threshold increases
// use to validate our own implementation
#if LIBVPTREE
#include "lib/vptree/include/vptree/vptree.hh"
class DctPoint {
 public:
  uint64_t hash;
  uint32_t id;
};

class DctTree : public VPTree< DctPoint > {
 public:
  double distance(const DctPoint& p1, const DctPoint& p2) override {
    return hamm64(p1.hash, p2.hash);
  }

  void create(uint64_t* hashes, uint32_t* ids, int numHashes) {
    std::vector<DctPoint> points;
    for (int i = 0; i < numHashes; ++i)
      points.push_back( DctPoint{hashes[i], ids[i]} );

    this->addMany(points.begin(), points.end());
  }

  QVector<Index::Match> search(uint64_t target, int threshold) {
    QVector<Index::Match> matches;

    auto points = this->neighborhood({target,0}, threshold);

    for (auto& p : points) {
      int d = hamm64(p->hash, target);
      Q_ASSERT (d < threshold);
      matches.append( Index::Match(p->id, d) );
    }
    return matches;
  }

};
#endif

// diy vptree, faster than libvptree and tuned for dct hash
// runtime gets much worse as threshold increases
#if VPTREE
#include "vptree.h"

class DctTree {
  struct vpValue {
    uint64_t hash;
    uint32_t id;
    vpValue() : hash(0), id(0) {}
    vpValue(uint64_t h, uint32_t i) : hash(h), id(i) {}
    static vpValue min() { return vpValue(0,0); }
    static vpValue max() { return vpValue(UINT64_MAX, 0); }
  };
  static inline int vpDistance(vpValue v1, vpValue v2) {
    return hamm64(v1.hash, v2.hash);
  };

  VpTree<vpValue, int, vpDistance> _tree;

 public:
  void create(uint64_t* hashes, uint32_t* ids, int numHashes) {
    std::vector<vpValue> values;
    for (int i = 0; i < numHashes; ++i)
      values.push_back( vpValue(hashes[i], ids[i]) );
    _tree.create(values);
    //_tree.printStats();
  }

  QVector<Index::Match> search(uint64_t target, int threshold) {
    std::vector<int> distances;
    std::vector<vpValue> results;
    _tree.search( vpValue{target, 0}, threshold, &results, &distances );

    Q_ASSERT(results.size() == distances.size());

    QVector<Index::Match> matches;
    for (size_t i = 0; i < distances.size(); ++i) {
      Q_ASSERT(distances[i] < threshold);
      matches.append(Index::Match(results[i].id, distances[i]));
    }
    return matches;
  }
};
#endif

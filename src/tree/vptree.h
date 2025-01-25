/* Vantage-point search tree
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

#include <stdlib.h>
#include <algorithm>
#include <vector>
#include <stdio.h>
#include <queue>
#include <limits>

/**
 * @class VpTree
 * @brief Vantage-Point Tree tuned for 64-bit dct hashes
 */
template <typename ValueType, typename DistanceType,
          DistanceType (*distance)(ValueType, ValueType)>
class VpTree {
 public:
  VpTree() {
    //qDebug("node == %d bytes, value == %d bytes, distance == %d bytes",
    //       int(sizeof(Node)), int(sizeof(ValueType)), int(sizeof(DistanceType)));
  }

  ~VpTree() { delete _root; }

  void create(std::vector<ValueType>& items) {
    delete _root;
    _root = buildFromPoints(items, 0, items.size(), nullptr);
  }

  void search(const ValueType target, const DistanceType threshold,
              std::vector<ValueType>* results,
              std::vector<DistanceType>* distances) {

    std::priority_queue<HeapItem> heap;
    thresholdSearch(_root, target, threshold, heap);

    results->clear();
    distances->clear();

    // the heap stores worst match at the top, reverse it
    while (!heap.empty()) {
      results->push_back(heap.top().value);
      distances->push_back(heap.top().dist);
      heap.pop();
    }

    std::reverse(results->begin(), results->end());
    std::reverse(distances->begin(), distances->end());
  }

  void printStats() const {
    int maxDepth = depth(_root);
    int numNodes = count(_root);
    qInfo("hashes=%d depth=%d 2^d=%d", numNodes, maxDepth, 1 << maxDepth);
  }

 private:
  struct Node {
    ValueType value;
    DistanceType threshold = 0;
    Node* left = nullptr;
    Node* right = nullptr;
    std::vector<ValueType> leaf;
    ~Node() {
      delete left;
      delete right;
    }
  } *_root = nullptr;

  enum {
    // tuning: minimum 3, maximum number of elements in a leaf node
    MaxLeafSize = 10
  };

  struct HeapItem {
    DistanceType dist;
    ValueType value;
    HeapItem(DistanceType dist, ValueType value) : dist(dist), value(value) {}
    bool operator<(const HeapItem& o) const { return dist < o.dist; }
  };

  struct DistanceComparator {
    ValueType item;
    DistanceComparator(const ValueType& item) : item(item) {}
    bool operator()(const ValueType& a, const ValueType& b) {
      return distance(item, a) < distance(item, b);
    }
  };

  struct RevDistanceComparator {
    ValueType item;
    RevDistanceComparator(const ValueType& item) : item(item) {}
    bool operator()(const ValueType& a, const ValueType& b) {
      return distance(item, a) > distance(item, b);
    }
  };

  Node* buildFromPoints(std::vector<ValueType>& items,
                        int lower, const int upper, Node* parent) {

    Q_ASSERT(lower >= 0 && lower < int(items.size()));
    Q_ASSERT(upper > 0 && upper <= int(items.size()));
    Q_ASSERT(upper > lower);

    //if (upper == lower) return nullptr;

    Node* node = new Node();
    //node->value = items[lower];

    if (upper - lower > MaxLeafSize) {

      // vantage point selection
      // max distance from parent seems best
#define MAX_FROM_PARENT (1)

#if MAX_FROM_PARENT
      if (parent) {
        auto it = std::max_element(items.begin()+lower, items.begin()+upper,
                                   DistanceComparator(parent->value));
        std::swap(items[lower], *it);
      } else
#endif
      // TODO: is this an oopsie?
      {
        // furthest from maximum value
        auto it = std::max_element(items.begin()+lower, items.begin()+upper,
                                   DistanceComparator(ValueType::max()));
        std::swap(items[lower], *it);
      }

      const ValueType value = items[lower];
      lower++;

      // partitioning scheme
      // one set far from vp (outside) the other near vp (inside)
#define FIXED_PARTITION (1)

#if FIXED_PARTITION
      // choose a fixed value for partitioning
      // random dct hashes have median distance 32
      // empirically 23-26 is best, works better even though
      // we don't get a 50% cut
      DistanceType midDist = 23;
#else
      // median partition, the middle distance
      // sort makes the median distance closer to 50% cut
      std::sort(items.begin()+lower, items.begin()+upper, DistanceComparator(value));
      int m1 = ((upper - lower) / 2) + lower + 1;
      Q_ASSERT(m1 < upper && m1 > lower);
      const DistanceType midDist = distance(value, items[m1]);
#endif

      auto it = std::partition(items.begin() + lower,
                               items.begin() + upper,
                               [&value,midDist](const  ValueType& v) {
                                 return distance(value, v) < midDist;
                               });
      int median = it - items.begin(); // it==first element of second group


      if (median == lower || median == upper) {
//        qWarning() << "partition failed, size=" << upper-lower << "midDist" << midDist;
        node->leaf.assign(items.begin() + lower-1, items.begin() + upper);
        return node;
      }
      else {

#ifdef FIXED_PARTITION
        // the actual middle could be less than midDist,
        // especially for small partitions
        // it does not seem to matter much if we do this, and
        // takes longer to build the tree
//        auto it = std::max_element(items.begin()+lower, items.begin()+median,
//                                   DistanceComparator(value));
//        int m2 = distance(value, *it) + 1;
//        Q_ASSERT(m2 <= midDist);
//        midDist = m2;
#endif
      }

      node->threshold = midDist;
      node->value = value;

      //const int split = (median-lower)*100 / (upper-lower);

      //printf("thresh = %d, median=%d %% sizes=%d,%d\n", node->threshold,
      //       split, median-lower, upper-median);


//      for (auto it = items.begin() + lower; it != items.begin() + median; ++it)
//        Q_ASSERT( distance(value, *it) < node->threshold );

//      for (auto it = items.begin() + median; it != items.begin() + upper; ++it)
//        Q_ASSERT( distance(value, *it) >= node->threshold );

        Q_ASSERT(lower < median);
        Q_ASSERT(median < upper);

        node->left = buildFromPoints(items, lower, median, node);
        node->right = buildFromPoints(items, median, upper, node);

    } else
      node->leaf.assign(items.begin() + lower, items.begin() + upper);

    return node;
  }

  void thresholdSearch(const Node* node, const ValueType& target, const DistanceType threshold,
                       std::priority_queue<HeapItem>& matches) const {

    const std::vector<ValueType>& leaf = node->leaf;

    if (leaf.size()) {
      Q_ASSERT(node->left == nullptr && node->right == nullptr);
      const std::vector<ValueType>& leaf = node->leaf;
      for (auto it = leaf.begin(); it != leaf.end(); ++it) {
        const ValueType& value = *it;
        const DistanceType dist = distance(value, target);
        if (dist < threshold)
          matches.push(HeapItem(dist, value));
      }
      return;
    }

    const DistanceType t = node->threshold;
    const DistanceType d = distance(node->value, target);

    if (d < threshold)
      matches.push(HeapItem(d, node->value));

    if ( d - threshold < t )
      thresholdSearch(node->left, target, threshold, matches);
    if ( d + threshold >= t )
      thresholdSearch(node->right, target, threshold, matches);
  }

  static int depth(Node* node) {
    int left = 0;
    int right = 0;
    if (node->left) left = 1 + depth(node->left);
    if (node->right) right = 1 + depth(node->right);
    return std::max(left, right);
  }

  static int count(Node* node) {
    int val = 0;
    if (node->leaf.size()) val += int(node->leaf.size());
    else val += 1;
    if (node->left) val += count(node->left);
    if (node->right) val += count(node->right);
    return val;
  }
};

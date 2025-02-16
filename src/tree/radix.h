/* Radix search for DCT hashes
   Copyright (C) 2025 scrubbbbs
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

/**
 * @brief Direct-mapped, Single-level Radix earch
 * 
 * The least significant bits (LSB) of dct hashes represent the structure
 * of the signal, while the higher represent the detail. Therefore, the
 * LSB is a good predictor of matches and should be used to partition
 * the search space.
 * 
 * By changing the radix value we also have a knob to turn to dramatically
 * decrease the search time, at the expense of losing some matches.
 */
template<typename index_type = uint32_t>
class RadixMap_t
{
  Q_DISABLE_COPY_MOVE(RadixMap_t);

 public:
  typedef index_type index_t;
  typedef dcthash_t hash_t;
  typedef char distance_t;

  /// Input/output type
  struct Value
  {
    index_t index;
    hash_t hash;
    Value(index_t index_, hash_t hash_)
        : index(index_)
        , hash(hash_) {}
  };

  struct Match
  {
    Value value;
    distance_t distance;
    Match()
        : value(-1, 0)
        , distance(-1) {}
    Match(const Value& value_, distance_t distance_)
        : value(value_)
        , distance(distance_) {}
    bool operator<(const Match& m) const { return distance < m.distance; }
  };

  struct Bucket
  {
    Q_DISABLE_COPY_MOVE(Bucket); // try not to copy/move

    std::vector<hash_t> hashes; // using pointers to save memory here
    std::vector<index_t> indices;

    Bucket(){
        // hashes.reserve(4096 / sizeof(hash_t));
        // indices.reserve(4096 / sizeof(hash_t));
    };

    size_t size() const {
      return sizeof(*this) + hashes.capacity() * sizeof(hash_t)
             + indices.capacity() * sizeof(index_t);
    }
  };

  struct Stats
  {
    size_t memory = 0;
    uint numBuckets = 0;
    uint mean = 0;  // mean bucket size
    uint sigma = 0; // standard deviation
    uint min = 0;   // min/max bucket size
    uint max = 0;
    uint empty = 0; // if radix is too big, lots of empty buckets
  };

 private:
  uint _radix = 1;
  hash_t _radixMask = 1;
  std::vector<Bucket*> _buckets;
  Bucket _emptyBucket;

 public:
  RadixMap_t(uint radix) {
    // limit buckets minimum memory usage to ~1GB
    uint maxRadix = 30 - std::ceil(log2(sizeof(Bucket) + sizeof(Bucket*)));

    if (radix > maxRadix) {
      radix = maxRadix;
      qWarning("radix too large, limiting to 2^%u buckets", radix);
    }

    _radix = radix;
    _radixMask = 0;
    for (uint i = 0; i < radix; ++i)
      _radixMask |= 1 << i;

    _buckets.resize(1U << _radix);
    for (uint i = 0; i < 1U << _radix; ++i)
      _buckets[i] = &_emptyBucket;
  }

  ~RadixMap_t() {
    for (auto* b : _buckets)
      if (b != &_emptyBucket) delete b;
  }

  uintptr_t addressOf(hash_t hash) const {
    auto& hashes = _buckets[indexOf(hash)].hashes;
    auto* p = Q_LIKELY(hashes.size()) ? hashes.data() : nullptr;
    return uintptr_t(p);
  }

  size_t indexOf(hash_t hash) const {
    //Q_ASSERT((hash & 1) == 0); // FIXME: wasting a bit in dct hashes
    size_t i = (hash >> 1) & _radixMask;
    //if (i >= (1U << _radix))
    //  qFatal("index oob: %u %u 0x%u", (uint) i, (uint) 1 << _radix, (uint) _radixMask);
    return i;
  }

  void insert(const std::vector<Value>& values) {
    for (auto& v : values) {
      size_t index = indexOf(v.hash);
      Bucket* bucket = _buckets[index];
      if (Q_UNLIKELY(bucket == &_emptyBucket)) {
        bucket = new Bucket;
        _buckets[index] = bucket;
      }

      bucket->hashes.push_back(v.hash);
      bucket->indices.push_back(v.index);
    }
  }

  Stats stats() const {
    uint64_t sum = 0;
    uint min = UINT_MAX, max = 0, empty = 0;
    for (size_t i = 0; i < (1U << _radix); ++i) {
      uint bytes = _buckets[i] != &_emptyBucket ? _buckets[i]->size() : 0;
      sum += bytes;
      min = std::min(min, bytes);
      max = std::max(max, bytes);
      if (_buckets[i]->hashes.size() == 0) empty++;
    }
    uint64_t memory = sum + sizeof(Bucket*) * (1U << _radix);

    const uint mean = sum / (1U << (_radix & 0x1F));
    sum = 0;
    for (size_t i = 0; i < (1U << _radix); ++i) {
      size_t bytes = _buckets[i]->size();
      int64_t x = (bytes - mean);
      sum += x * x;
    }
    uint stdDev = sqrt(sum / (1U << (_radix & 0x1F)));

    return Stats{.memory = memory,
                 .numBuckets = 1U << _radix,
                 .mean = mean,
                 .sigma = stdDev,
                 .min = min,
                 .max = max,
                 .empty = empty};
  }

  void search(hash_t hash, distance_t threshold, std::vector<Match>& matches) const {
    const Bucket* bucket = _buckets[indexOf(hash)];
    const std::vector<hash_t>& hashes = bucket->hashes;
    const std::vector<index_t>& indices = bucket->indices;
    const size_t count = hashes.size();

    // gcc doesn't want to unroll this, but it helps a lot
#define STEP(d, n) \
  distance_t d = hamm64(hash, hashes[i + n]); \
  if (Q_UNLIKELY(d < threshold)) matches.push_back(Match(Value(indices[i + n], hashes[i + n]), d))

    size_t i = 0;
    for (; i < (count / 4) * 4; i += 4) {
      STEP(d0, 0);
      STEP(d1, 1);
      STEP(d2, 2);
      STEP(d3, 3);
    }

    for (; i < count; ++i) {
      STEP(d0, 0);
    }
#undef STEP
  }

  enum { vectorSize = 8 }; // 8 may enable AVX512 8x64 popcnt

  void search(const hash_t* __restrict queryHashes,
              distance_t threshold,
              std::vector<Match>* matches) const {
    const Bucket* bucket = _buckets[indexOf(queryHashes[0])];
    const hash_t* __restrict hashes = bucket->hashes.data();
    const index_t* indices = bucket->indices.data();
    size_t count = bucket->hashes.size();

    for (size_t i = 0; i < count; ++i) {
      hash_t hash = hashes[i];
      index_t index = indices[i];
      for (size_t j = 0; j < vectorSize; ++j) {
        distance_t d = hamm64(queryHashes[j], hash);
        if (Q_UNLIKELY(d < threshold)) matches[j].push_back(Match(Value(index, hash), d));
      }
    }
  }
};

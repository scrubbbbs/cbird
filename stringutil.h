/* Useful string functions
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

// https://en.wikipedia.org/wiki/Levenshtein_distance#Computing_Levenshtein_distance
// l_s and l_t are the number of characters in string s and t respectively
static inline int levenshteinDistance(const char* s, int l_s, const char* t,
                                      int l_t) {
  // degenerate cases
  if (strncmp(s, t, size_t(std::min(l_s, l_t))) == 0) return 0;
  if (l_t == 0) return l_t;
  if (l_t == 0) return l_s;

  // create two work vectors of integer distances
  int l_v0 = l_t + 1, l_v1 = l_t + 1;

  int v0[l_v0];
  int v1[l_v1];

  // initialize v0 (the previous row of distances)
  // this row is A[0][i]: edit distance for an empty s
  // the distance is just the number of characters to delete from t
  for (int i = 0; i < l_v0; i++) v0[i] = i;

  for (int i = 0; i < l_s; i++) {
    // calculate v1 (current row distances) from the previous row v0

    // first element of v1 is A[i+1][0]
    //   edit distance is delete (i+1) chars from s to match empty t
    v1[0] = i + 1;

    // use formula to fill in the rest of the row
    for (int j = 0; j < l_t; j++) {
      int cost = (s[i] == t[j]) ? 0 : 1;
      v1[j + 1] = std::min(v1[j] + 1, std::min(v0[j + 1] + 1, v0[j] + cost));
    }

    // copy v1 (current row) to v0 (previous row) for next iteration
    for (int j = 0; j < l_v0; j++) v0[j] = v1[j];
  }

  return v1[l_t];
}

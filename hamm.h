#pragma once

/// 64-bit hamming distance using special x86 instruction
inline int hamm64(uint64_t a, uint64_t b) {
  return __builtin_popcountll(a ^ b);
}

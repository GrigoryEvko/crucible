#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace crucible {

// Dedup table for Python call sites.
//
// Each ATen op in the ring carries a callsite_hash identifying the
// Python source location that triggered it. The CallSiteTable maps
// these hashes to (filename, funcname, lineno) triples.
//
// Hot path: has() — open-addressing probe, ~5-10ns.
// Cold path: insert() — string copy, ~100ns (only on first encounter).
//
// Typical training loop has ~50-200 unique call sites, so the table
// stays small. After the first iteration, has() always returns true.
struct CallSiteTable {
  struct Entry {
    uint64_t hash;
    std::string filename;
    std::string funcname;
    int32_t lineno;
  };

  std::vector<Entry> entries;

  // Open-addressing hash set for fast "already seen" check.
  static constexpr uint32_t SET_CAP = 4096;
  static constexpr uint32_t SET_MASK = SET_CAP - 1;
  uint64_t seen[SET_CAP] = {};

  bool has(uint64_t hash) const {
    uint32_t idx = static_cast<uint32_t>(hash) & SET_MASK;
    for (uint32_t p = 0; p < SET_CAP; p++) {
      const auto& h = seen[(idx + p) & SET_MASK];
      if (h == hash) return true;
      if (h == 0) return false;
    }
    return false;
  }

  void insert(
      uint64_t hash,
      std::string filename,
      std::string funcname,
      int32_t lineno) {
    if (has(hash)) return;
    uint32_t idx = static_cast<uint32_t>(hash) & SET_MASK;
    for (uint32_t p = 0; p < SET_CAP; p++) {
      auto& h = seen[(idx + p) & SET_MASK];
      if (h == 0) {
        h = hash;
        entries.push_back(
            {hash, std::move(filename), std::move(funcname), lineno});
        return;
      }
    }
  }

  uint32_t size() const { return static_cast<uint32_t>(entries.size()); }
};

} // namespace crucible

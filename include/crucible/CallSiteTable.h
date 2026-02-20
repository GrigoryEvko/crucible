#pragma once

#include <crucible/Types.h>

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
    CallsiteHash hash;           // strong-typed callsite identity
    std::string filename;
    std::string funcname;
    int32_t lineno = 0;
  };

  std::vector<Entry> entries;

  // Open-addressing hash set for fast "already seen" check.
  // Sentinel: CallsiteHash{} (raw 0) means empty slot.
  static constexpr uint32_t SET_CAP = 4096;
  static constexpr uint32_t SET_MASK = SET_CAP - 1;
  CallsiteHash seen[SET_CAP]{};

  [[nodiscard]] bool has(CallsiteHash hash) const {
    uint32_t idx = static_cast<uint32_t>(hash.raw()) & SET_MASK;
    for (uint32_t p = 0; p < SET_CAP; p++) {
      const auto& h = seen[(idx + p) & SET_MASK];
      if (h == hash) return true;
      if (h == CallsiteHash{}) return false;
    }
    return false;
  }

  void insert(
      CallsiteHash hash,
      std::string filename,
      std::string funcname,
      int32_t lineno) {
    if (has(hash)) return;
    uint32_t idx = static_cast<uint32_t>(hash.raw()) & SET_MASK;
    for (uint32_t p = 0; p < SET_CAP; p++) {
      auto& h = seen[(idx + p) & SET_MASK];
      if (h == CallsiteHash{}) {
        h = hash;
        entries.push_back(
            {hash, std::move(filename), std::move(funcname), lineno});
        return;
      }
    }
  }

  [[nodiscard]] uint32_t size() const { return static_cast<uint32_t>(entries.size()); }
};

} // namespace crucible

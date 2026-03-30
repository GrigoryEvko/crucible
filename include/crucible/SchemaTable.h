#pragma once

// SchemaTable: SchemaHash → op name string lookup.
//
// The Vessel registers (hash, name) pairs at startup for every ATen op
// it intercepts. The standalone library starts with an empty table.
// After registration completes, the table is read-only (no locking).
//
// Same lifecycle as CKernelTable: all registrations MUST complete before
// BackgroundThread::start(). After that, only lookup() is called.
//
// Used by:
//   - TraceVisualizer for DOT graph labels
//   - Diagnostics / debug printing
//   - .crtrace export (string table section)

#include <crucible/Platform.h>
#include <crucible/Types.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>

namespace crucible {

static constexpr uint32_t SCHEMA_TABLE_CAP = 512;

struct SchemaEntry {
  SchemaHash hash;
  const char* name = nullptr;  // owned: malloc'd copy, freed in clear()
};

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(SchemaEntry);

struct SchemaTable {
  SchemaEntry entries[SCHEMA_TABLE_CAP]{};
  uint32_t size = 0;

  SchemaTable() = default;

  // Not copyable — owns malloc'd strings.
  SchemaTable(const SchemaTable&) = delete("owns malloc'd name strings");
  SchemaTable& operator=(const SchemaTable&) = delete("owns malloc'd name strings");
  SchemaTable(SchemaTable&&) = delete("owns malloc'd name strings");
  SchemaTable& operator=(SchemaTable&&) = delete("owns malloc'd name strings");

  ~SchemaTable() { clear(); }

  // Register a schema_hash → name mapping. Idempotent: re-registering
  // the same hash updates the name. Silently no-ops beyond cap.
  void register_name(SchemaHash hash, const char* name) {
    if (!name) return;

    // Check for existing entry (idempotent update).
    for (uint32_t i = 0; i < size; i++) {
      if (entries[i].hash == hash) {
        std::free(const_cast<char*>(entries[i].name));
        entries[i].name = strdup_(name);
        return;
      }
    }
    if (size >= SCHEMA_TABLE_CAP) return;
    entries[size++] = {.hash = hash, .name = strdup_(name)};
    std::ranges::sort(std::span<SchemaEntry>{entries, size},
                      {}, &SchemaEntry::hash);
  }

  // Lookup: returns nullptr for unknown hashes. O(log n) binary search.
  [[nodiscard]] const char* lookup(SchemaHash hash) const {
    uint32_t lo = 0, hi = size;
    while (lo < hi) {
      const uint32_t mid = lo + (hi - lo) / 2;
      if (entries[mid].hash == hash) return entries[mid].name;
      if (entries[mid].hash < hash) lo = mid + 1;
      else                          hi = mid;
    }
    return nullptr;
  }

  // Short name: strip "aten::" prefix if present, return suffix.
  // Returns static hex string for unknown hashes.
  [[nodiscard]] const char* short_name(SchemaHash hash) const {
    const char* full = lookup(hash);
    if (!full) return nullptr;
    // Strip "aten::" prefix
    if (std::strncmp(full, "aten::", 6) == 0)
      return full + 6;
    return full;
  }

  [[nodiscard]] uint32_t count() const { return size; }

  void clear() {
    for (uint32_t i = 0; i < size; i++) {
      std::free(const_cast<char*>(entries[i].name));
      entries[i].name = nullptr;
    }
    size = 0;
  }

 private:
  [[nodiscard]] static char* strdup_(const char* s) {
    const auto len = std::strlen(s);
    auto* p = static_cast<char*>(std::malloc(len + 1));
    if (!p) [[unlikely]] std::abort();
    std::memcpy(p, s, len + 1);
    return p;
  }
};

// Global singleton — same pattern as global_ckernel_table().
//
// TODO(thread-safety): register_name() is NOT thread-safe. Current usage is
// safe because only one foreground thread records ops (SPSC model), and the
// background thread only calls lookup(). If multi-threaded recording is ever
// needed, add a spinlock or use a concurrent hash map.
[[nodiscard]] inline SchemaTable& global_schema_table() {
  static SchemaTable table;
  return table;
}

// Convenience: register hash → name in the global table.
inline void register_schema_name(SchemaHash hash, const char* name) {
  global_schema_table().register_name(hash, name);
}

// Convenience: lookup name for hash in the global table.
[[nodiscard]] inline const char* schema_name(SchemaHash hash) {
  return global_schema_table().lookup(hash);
}

// Convenience: short name (stripped prefix) from global table.
[[nodiscard]] inline const char* schema_short_name(SchemaHash hash) {
  return global_schema_table().short_name(hash);
}

} // namespace crucible

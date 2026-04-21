#pragma once

// SchemaTable: SchemaHash → op name string lookup.
//
// The Vessel registers (hash, name) pairs at startup for every ATen op
// it intercepts. The standalone library starts with an empty table.
// After registration completes, the table is read-only (no locking).
//
// Lifecycle (compile-time + runtime enforced):
//
//   Mutable (fresh/after clear) → Sealed (one-way, set by seal())
//
//   In Mutable phase: register_name / lookup / count / clear all work.
//   In Sealed phase:  lookup / count work; register_name aborts.
//
// BackgroundThread::start() seals the global SchemaTable automatically
// so the documented invariant — "all registrations complete before
// bg thread starts" — is now a contract-enforced structural rule.  The
// runtime guard catches FFI callers; the typed register_name(MutableView,
// ...) overload is available for code that wants to prove the Mutable
// state at compile time (see ScopedView.h discipline).
//
// Used by:
//   - TraceVisualizer for DOT graph labels
//   - Diagnostics / debug printing
//   - .crtrace export (string table section)

#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/safety/ScopedView.h>
#include <crucible/safety/Tagged.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <type_traits>

namespace crucible {

static constexpr uint32_t SCHEMA_TABLE_CAP = 512;

struct SchemaEntry {
  SchemaHash hash;
  const char* name = nullptr;  // owned: malloc'd copy, freed in clear()
};

CRUCIBLE_ASSERT_TRIVIALLY_RELOCATABLE(SchemaEntry);

// ── SchemaTable state tags ──────────────────────────────────────────
// Tagged with safety::ScopedView so the compiler can prove the table
// is in Mutable state before register_name is called.  Mutable is the
// default (freshly-constructed or cleared); Sealed is entered via
// seal() and is irreversible for the lifetime of that phase (clear()
// resets to Mutable for test isolation).
namespace schema_state {
    struct Mutable {};
    struct Sealed  {};
}

struct SchemaTable {
  SchemaEntry entries[SCHEMA_TABLE_CAP]{};
  uint32_t size = 0;

  // Seal gate. false = Mutable, true = Sealed. Release-store pairs
  // with the acquire-load in is_sealed(): once seal() commits, any
  // thread observing Sealed also observes all earlier register_name()
  // effects (the entries[] writes). This is what makes the lookup()
  // path on the bg thread race-free with registration on the fg thread.
  std::atomic<bool> sealed_{false};

  SchemaTable() = default;

  // Not copyable — owns malloc'd strings.
  SchemaTable(const SchemaTable&) = delete("owns malloc'd name strings");
  SchemaTable& operator=(const SchemaTable&) = delete("owns malloc'd name strings");
  SchemaTable(SchemaTable&&) = delete("owns malloc'd name strings");
  SchemaTable& operator=(SchemaTable&&) = delete("owns malloc'd name strings");

  ~SchemaTable() { clear(); }

  // ── Seal transition ────────────────────────────────────────────────
  //
  // Irreversible (within one Mutable→Sealed phase); clear() resets.
  // Release-store: any subsequent acquire-load observing Sealed also
  // observes every entry registered before the seal.
  void seal() noexcept { sealed_.store(true, std::memory_order_release); }

  [[nodiscard]] bool is_sealed() const noexcept {
    return sealed_.load(std::memory_order_acquire);
  }

  // ── Typed views (ScopedView discipline) ────────────────────────────
  using MutableView = crucible::safety::ScopedView<SchemaTable, schema_state::Mutable>;
  using SealedView  = crucible::safety::ScopedView<SchemaTable, schema_state::Sealed>;

  [[nodiscard]] MutableView mint_mutable_view() const noexcept
      pre (!is_sealed())
  {
    return crucible::safety::mint_view<schema_state::Mutable>(*this);
  }

  [[nodiscard]] SealedView mint_sealed_view() const noexcept
      pre (is_sealed())
  {
    return crucible::safety::mint_view<schema_state::Sealed>(*this);
  }

  // ADL-discovered predicates for mint_view<>.
  [[nodiscard]] friend constexpr bool view_ok(
      SchemaTable const& t, std::type_identity<schema_state::Mutable>) noexcept {
    return !t.is_sealed();
  }
  [[nodiscard]] friend constexpr bool view_ok(
      SchemaTable const& t, std::type_identity<schema_state::Sealed>) noexcept {
    return t.is_sealed();
  }

  // Register a schema_hash → name mapping.  Idempotent: re-registering
  // the same hash updates the name.  Silently no-ops beyond cap.
  //
  // The name parameter is Tagged<source::Sanitized> so that bytes from
  // disk / network / FFI cannot reach this entry without going through
  // an explicit retag<source::Sanitized>() — i.e., the caller has to
  // certify they validated the input.  Trusted sources (PyTorch's
  // Operator schema, in-source string literals) construct Sanitized
  // directly; the TraceLoader retags after its length-bounded validation.
  using SanitizedName = crucible::safety::Tagged<const char*,
                                                 crucible::safety::source::Sanitized>;

  // Typed overload — requires MutableView proof.  Zero runtime phase
  // check: the proof is carried by the view parameter.
  void register_name(MutableView const&, SchemaHash hash, SanitizedName name_tag) {
    const char* name = name_tag.value();
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

  // Legacy overload — mints the view locally, runtime-guarded through
  // mint_mutable_view()'s pre() contract.  Kept for distant call sites
  // (FFI, TraceLoader) that can't easily thread a view through.
  void register_name(SchemaHash hash, SanitizedName name_tag) {
    register_name(mint_mutable_view(), hash, name_tag);
  }

  // Lookup: returns nullptr for unknown hashes. O(log n) binary search.
  // Safe in either phase — reads only.
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

  // Reset to empty Mutable state.  Resets the seal gate so tests can
  // reuse the table across cases; also called from the destructor.
  void clear() {
    for (uint32_t i = 0; i < size; i++) {
      std::free(const_cast<char*>(entries[i].name));
      entries[i].name = nullptr;
    }
    size = 0;
    sealed_.store(false, std::memory_order_release);
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

// Tier 2 opt-in: nothing inside SchemaTable may be a ScopedView.
static_assert(crucible::safety::no_scoped_view_field_check<SchemaTable>());

// Global singleton — same pattern as global_ckernel_table().
//
// Thread safety is enforced by the Mutable→Sealed phase.
// BackgroundThread::start() calls seal() on this global before spawning
// the bg worker, so any register_name() attempted after that point is
// caught by the mint_mutable_view() contract rather than racing with
// the bg thread's lookups.
[[nodiscard]] inline SchemaTable& global_schema_table() {
  static SchemaTable table;
  return table;
}

// Convenience: register hash → name in the global table.  The free
// function takes Tagged<Sanitized> so the trust boundary is uniform
// with SchemaTable::register_name above.
inline void register_schema_name(SchemaHash hash,
                                 SchemaTable::SanitizedName name) {
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

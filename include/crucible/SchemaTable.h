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
// the typed register_name(MutableView, ...) surface makes callers prove
// the Mutable state at the mutation boundary (see ScopedView.h discipline).
//
// Used by:
//   - TraceVisualizer for DOT graph labels
//   - Diagnostics / debug printing
//   - .crtrace export (string table section)

#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/ScopedView.h>
#include <crucible/safety/Tagged.h>

#include <algorithm>
#include <atomic>
#include <bit>          // std::bit_cast — §III-clean const-strip for std::free
#include <cstdint>
#include <cstdio>       // std::fprintf — overflow diagnostic on cap exhaustion
#include <cstdlib>
#include <cstring>
#include <memory>       // std::construct_at — for clear()'s SizeCounter rewind
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
  // ── size counter wrapper (#1003 WRAP-SchemaTab-1) ──────────────────
  // The entries[] array is structurally append-only: register_name
  // pushes one entry per call (or updates the existing one in place
  // for idempotent re-registration), never erases or reorders.  size
  // is bounded above by SCHEMA_TABLE_CAP and rewinds to 0 only via
  // clear() (called from the destructor and for test isolation).
  // Overflow at SCHEMA_TABLE_CAP is a loud abort with a stderr
  // diagnostic — same discipline as CKernelTable (#1009 WRAP-SchemaTab-7
  // upgraded the prior silent-truncate behavior to match CKernel).
  // Hitting the cap means the cap is wrong; raise SCHEMA_TABLE_CAP or
  // audit Vessel schema registrations.
  //
  // BoundedMonotonic<uint32_t, SCHEMA_TABLE_CAP> pins both invariants
  // at the type level: monotonic forward progress (no accidental
  // backward write via raw assignment) AND the upper bound (no
  // accidental size > CAP via aliasing or memcpy).  The clear()
  // rewind is the only legitimate non-monotonic mutation and uses
  // std::construct_at to re-establish the invariant from a known
  // floor (0u) — same pattern as CKernelTable (WRAP-CKernel-2 #890)
  // and IterationDetector's boundaries_detected / signature_len /
  // ops_since_boundary.
  //
  // Zero-cost: regime-2 collapse — sizeof(SizeCounter) ==
  // sizeof(uint32_t) == 4 B; SchemaTable layout preserved.
  using SizeCounter = ::crucible::safety::BoundedMonotonic<
      uint32_t, SCHEMA_TABLE_CAP>;

  SchemaEntry entries[SCHEMA_TABLE_CAP]{};
  SizeCounter size{0u};

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
  void seal() noexcept {
    sealed_.store(true, std::memory_order_release);
    // CONTRACT-SchemaTable-Seal-POST: one-way Mutable → Sealed transition.
    // After seal(), is_sealed() must observe true on the same thread (the
    // release-store has happens-before with our acquire-load via the same
    // sealed_ atomic).  Mirrors CONTRACT-CKernel-Seal-POST (CKernel.h).
    // Catches a future refactor that drops the store or weakens the
    // memory order — bg-thread readers depend on the release semantics
    // for seeing all preceding entries[] writes.  Routes through
    // CRUCIBLE_POST per the GCC 16.1.1 consteval-bypass family.  Void
    // return: first arg `0` is the conventional sentinel.
    CRUCIBLE_POST(0, sealed_.load(std::memory_order_acquire));
  }

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
    for (uint32_t i = 0; i < size.get(); i++) {
      if (entries[i].hash == hash) {
        // §III-clean: bit_cast strips const for std::free (which takes
        // void*).  Pointer is malloc'd, so we own it; the const-qualifier
        // on `name` is the storage type's published immutability, not
        // the underlying allocation's mutability.
        std::free(std::bit_cast<char*>(entries[i].name));
        entries[i].name = strdup_(name);
        // CONTRACT-SchemaTable-RegisterName-POST (alias-update path):
        // entries[i].name is a freshly strdup'd copy of `name` — i.e.,
        // entries[i].name == name byte-for-byte (strcmp == 0) but at a
        // different address (we just freed the old allocation and
        // duplicated the input).  The post pins the lookup-equivalent
        // invariant: a subsequent lookup(hash) finds entries[i].name.
        // Mirrors CONTRACT-CKernel-RegisterOp-POST alias-update path.
        // The `name != nullptr` post check is structurally guaranteed
        // by the early return on line 177 (`if (!name) return`).
        CRUCIBLE_POST(0, lookup(hash) != nullptr);
        return;
      }
    }
    // Overflow on the FFI / Vessel registration boundary is a loud
    // abort, not a silent truncate.  A silent return would dispatch
    // every subsequent unregistered schema as OPAQUE (slow path) at
    // the build_trace boundary, with the bug surfacing far from the
    // missed registration.  Same discipline as CKernelTable
    // (CKernel.h:500-506) — hitting the cap means the cap is wrong;
    // bump SCHEMA_TABLE_CAP or audit the Vessel schema registrations
    // that exhausted it.  WRAP-SchemaTab-7 (#1009).
    if (size.get() >= SCHEMA_TABLE_CAP) [[unlikely]] {
      std::fprintf(stderr,
        "crucible: SchemaTable full (%u/%u entries); bump "
        "SCHEMA_TABLE_CAP or audit Vessel schema registrations\n",
        size.get(), SCHEMA_TABLE_CAP);
      std::abort();
    }
    // Append + bump.  Two-step instead of `entries[size++]` because
    // BoundedMonotonic's bump() is a separate mutation; the index
    // .get() and the bump() bracket the store.
    entries[size.get()] = {.hash = hash, .name = strdup_(name)};
    size.bump();   // monotonic +1; bound-checked at SCHEMA_TABLE_CAP
    std::ranges::sort(std::span<SchemaEntry>{entries, size.get()},
                      {}, &SchemaEntry::hash);
    // CONTRACT-SchemaTable-RegisterName-POST (insert path):
    //   (1) lookup(hash) != nullptr — the freshly-registered schema is
    //       findable.  This is the load-bearing contract: a registration
    //       that doesn't make the schema findable would silently fall
    //       through to the OPAQUE fallback at every subsequent dispatch
    //       (slow path), defeating the whole registration phase.  After
    //       ranges::sort the entry is at its sorted position; lookup's
    //       binary search finds it.  Mirrors CKernel-RegisterOp insert
    //       path POST.
    //   (2) size.get() <= SCHEMA_TABLE_CAP — bound preserved by
    //       BoundedMonotonic but pinned because BoundedMonotonic's bump()
    //       pre is `current < Max`; post-bump current ≤ Max.
    // Routes through CRUCIBLE_POST per the GCC 16.1.1 consteval-bypass
    // family.  Catches a refactor that drops the sort (binary search
    // would fail to find the inserted entry) or that breaks the
    // bounded-monotonic counter contract.
    CRUCIBLE_POST(0, lookup(hash) != nullptr);
    CRUCIBLE_POST(0, size.get() <= SCHEMA_TABLE_CAP);
  }

  // Lookup: returns nullptr for unknown hashes. O(log n) binary search.
  // Safe in either phase — reads only.
  [[nodiscard]] const char* lookup(SchemaHash hash) const {
    uint32_t lo = 0, hi = size.get();
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

  [[nodiscard]] uint32_t count() const { return size.get(); }

  // Reset to empty Mutable state.  Resets the seal gate so tests can
  // reuse the table across cases; also called from the destructor.
  //
  // clear() is not a monotonic operation — it deliberately rewinds
  // size from N back to 0 after freeing every owned name string.
  // Re-construct the BoundedMonotonic in place so the bound +
  // monotonicity invariants are established afresh from the known
  // floor.  Same pattern as CKernelTable::clear() (WRAP-CKernel-2)
  // and IterationDetector's reset() rewinds.
  void clear() {
    for (uint32_t i = 0; i < size.get(); i++) {
      // §III-clean: bit_cast strips const for std::free; the malloc'd
      // strdup_ allocation is mutable storage we own.
      std::free(std::bit_cast<char*>(entries[i].name));
      entries[i].name = nullptr;
    }
    std::construct_at(&size, SizeCounter{0u});
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
// the bg worker, so callers must mint a MutableView before that point
// and thread it through every registration.
[[nodiscard]] inline SchemaTable& global_schema_table() {
  static SchemaTable table;
  return table;
}

// Convenience: register hash → name in the global table.  The free
// function takes Tagged<Sanitized> so the trust boundary is uniform
// with SchemaTable::register_name above.
inline void register_schema_name(SchemaTable::MutableView const& view,
                                 SchemaHash hash,
                                 SchemaTable::SanitizedName name) {
  global_schema_table().register_name(view, hash, name);
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

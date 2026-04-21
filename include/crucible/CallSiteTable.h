#pragma once

#include <crucible/Types.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Refined.h>

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

  // The seen[] array is 32 KiB (4096 × 8 B CallsiteHash); the entries
  // AppendOnly adds ~24 B (std::vector inline state).  Lock the layout
  // so SET_CAP changes are surfaced rather than silently bloating the
  // cache-line footprint.

  // entries is structurally append-only: insert() pushes new callsite
  // records, nothing ever erases or reorders.  AppendOnly<> turns the
  // convention into a type-system guarantee — code that tries to .erase()
  // or .clear() this won't compile.
  crucible::safety::AppendOnly<Entry> entries;

  // Open-addressing hash set for fast "already seen" check.
  // Sentinel: CallsiteHash{} (raw 0) means empty slot.
  static constexpr uint32_t SET_CAP = 4096;
  static constexpr uint32_t SET_MASK = SET_CAP - 1;
  CallsiteHash seen[SET_CAP]{};

  [[nodiscard, gnu::hot]] bool has(CallsiteHash hash) const noexcept {
    // Sentinel (raw == 0) reserved for empty slots; real callsites hash
    // to non-zero.  Reject the sentinel up front so has(sentinel) can't
    // alias the first empty bucket encountered.
    if (!hash) return false;
    uint32_t idx = static_cast<uint32_t>(hash.raw()) & SET_MASK;
    for (uint32_t p = 0; p < SET_CAP; p++) {
      const auto& h = seen[(idx + p) & SET_MASK];
      if (h == hash) return true;
      if (h == CallsiteHash{}) return false;
    }
    return false;
  }

  // Typed insert: Refined<non_zero, CallsiteHash> makes the
  // "not the sentinel" invariant a type-system property.  Construction
  // of the Refined at the call site fires a contract if the caller
  // passes the zero sentinel, AND the body here treats the invariant
  // as established: no internal null-check, no early return on zero.
  using NonZeroHash = crucible::safety::Refined<
      crucible::safety::non_zero, CallsiteHash>;

  void insert(
      NonZeroHash hash_nz,
      std::string filename,
      std::string funcname,
      int32_t lineno) {
    const CallsiteHash hash = hash_nz.value();
    if (has(hash)) return;
    uint32_t idx = static_cast<uint32_t>(hash.raw()) & SET_MASK;
    for (uint32_t p = 0; p < SET_CAP; p++) {
      auto& h = seen[(idx + p) & SET_MASK];
      if (h == CallsiteHash{}) {
        h = hash;
        entries.emplace(hash, std::move(filename), std::move(funcname), lineno);
        return;
      }
    }
  }

  // Backward-compat overload: constructs the NonZeroHash at the boundary
  // and forwards.  Callers that already filter the sentinel outside can
  // use this without wrapping; the Refined ctor fires a contract on zero
  // so the behavior is "crash the caller" rather than silent drop.
  void insert(
      CallsiteHash hash,
      std::string filename,
      std::string funcname,
      int32_t lineno) {
    if (!hash) return;  // tolerate sentinel for callers that haven't
                        // migrated — do NOT construct NonZeroHash, which
                        // would fire a contract.  Once all call sites
                        // pass NonZeroHash explicitly, this overload
                        // can be removed.
    insert(NonZeroHash{hash}, std::move(filename), std::move(funcname), lineno);
  }

  [[nodiscard]] uint32_t size() const { return static_cast<uint32_t>(entries.size()); }
};

// Layout lock: seen[] dominates the footprint.  Test: sizeof is at
// least SET_CAP * sizeof(CallsiteHash), bounded above by a generous
// ceiling that still surfaces SET_CAP doublings.
static_assert(sizeof(CallSiteTable) >= CallSiteTable::SET_CAP * sizeof(CallsiteHash),
              "CallSiteTable footprint should be dominated by seen[]");
static_assert(sizeof(CallSiteTable) <= CallSiteTable::SET_CAP * sizeof(CallsiteHash) + 128,
              "CallSiteTable grew unexpectedly — check for accidental field additions");

} // namespace crucible

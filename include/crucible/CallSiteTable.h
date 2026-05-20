#pragma once

// FIXY-U-096c production migration: NonNegative / Tagged / AppendOnly /
// Refined / non_zero / source::* tags reached through the fixy:: umbrella
// instead of safety::* directly.  CallSiteTable.h has zero production
// fan-in (only test/* + fuzz/* include sites) so the full Wrap.h umbrella
// is safe — no Arena.h cycle to dodge here.  fixy/Source.h installs
// `namespace source = ::crucible::safety::source;` under
// `crucible::fixy::tags::` (note the `tags::` segment — the top-level
// `crucible::fixy::source::` slot is reserved for the cross-org
// federation sub-tree per fixy-A4-013) so the canonical paths here are
// `fixy::tags::source::{Sanitized, External, FromInternal}`.
#include <crucible/Types.h>
#include <crucible/fixy/Source.h>
#include <crucible/fixy/Wrap.h>

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
  // Refinement type for `lineno` storage (#880 WRAP-CallSite-2).
  // Python source line numbers are non-negative: 0 is the canonical
  // "unset / unknown / <frozen importlib>" sentinel, ≥1 is a real
  // line per PEP 8.  Negative values come only from corrupted FFI or
  // bit-flipped data and are structurally invalid.
  //
  // `non_negative` (≥ 0), NOT `positive` (> 0): zero is admissible
  // because it is the NSDMI default and the unset sentinel.  Refined<
  // non_negative, int32_t> is regime-1 EBO collapse — sizeof(Lineno)
  // == sizeof(int32_t) == 4B; Entry's layout is preserved.
  //
  // The ctor's `pre(non_negative(v))` clause fires at construction
  // time inside insert() — a corrupted FFI lineno of -1 aborts under
  // semantic=enforce; in constexpr context (the neg-compile fixtures)
  // it is rejected as a non-constant expression per P1494R5.
  using Lineno = ::crucible::fixy::wrap::NonNegative<int32_t>;

  // Provenance newtype for the stored filename / funcname strings
  // (#881 WRAP-CallSite-3).  After insert() validates and stores, the
  // strings are post-validation: they came in via the External /
  // FromInternal overloads (or the legacy raw std::string overload)
  // and the table treats them uniformly as Sanitized at storage.
  // Consumers reading Entry::filename / funcname get a typed Tagged<>
  // they cannot accidentally pass back to a function expecting raw
  // External — the explicit-only Tagged ctor + lack of implicit
  // Tagged<T,A> → Tagged<T,B> conversion close both directions.
  //
  // Tagged<std::string, source::Sanitized> is regime-1 EBO collapse —
  // TrustLattice<Sanitized>::element_type is empty and `[[no_unique
  // _address]]`-collapsed in the Graded substrate, so sizeof(
  // SanitizedName) == sizeof(std::string) and Entry's layout +
  // CallSiteTable's footprint static_asserts (lines 155-158) are
  // preserved.
  using SanitizedName = ::crucible::fixy::wrap::Tagged<
      std::string, ::crucible::fixy::tags::source::Sanitized>;

  struct Entry {
    CallsiteHash hash;           // strong-typed callsite identity
    SanitizedName filename;      // Tagged<std::string, source::Sanitized>
    SanitizedName funcname;      // Tagged<std::string, source::Sanitized>
    Lineno lineno{int32_t{0}};   // Refined<non_negative, int32_t>; default 0
  };

  // The seen[] array is 32 KiB (4096 × 8 B CallsiteHash); the entries
  // AppendOnly adds ~24 B (std::vector inline state).  Lock the layout
  // so SET_CAP changes are surfaced rather than silently bloating the
  // cache-line footprint.

  // entries is structurally append-only: insert() pushes new callsite
  // records, nothing ever erases or reorders.  AppendOnly<> turns the
  // convention into a type-system guarantee — code that tries to .erase()
  // or .clear() this won't compile.
  ::crucible::fixy::wrap::AppendOnly<Entry> entries;

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
  using NonZeroHash = ::crucible::fixy::wrap::Refined<
      ::crucible::fixy::wrap::non_zero, CallsiteHash>;

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
        // Construct the typed wrappers HERE.  Lineno's ctor's pre
        // clause fires the contract on negative input.  SanitizedName
        // wraps std::string in source::Sanitized — storage carries
        // the post-validation provenance; downstream readers cannot
        // accidentally retag as External (#881 WRAP-CallSite-3).
        entries.emplace(hash,
                        SanitizedName{std::move(filename)},
                        SanitizedName{std::move(funcname)},
                        Lineno{lineno});
        return;
      }
    }
  }

  // ── Source-tagged variants ─────────────────────────────────────────
  //
  // Python frame metadata arriving via Vessel FFI is source::External
  // (not validated as a valid UTF-8 path, not sanitized for embedded
  // shell metacharacters, etc.).  Synthetic test drivers tag as
  // source::FromInternal so review greps distinguish the origins.
  //
  // These overloads unwrap the tag and forward to the untagged body —
  // the CallSiteTable stores the strings for diagnostic output only and
  // never consumes them as paths, shell arguments, or HTML, so the tag
  // acts as provenance documentation rather than a sanitization gate.
  using ExternalName = ::crucible::fixy::wrap::Tagged<
      std::string, ::crucible::fixy::tags::source::External>;
  using InternalName = ::crucible::fixy::wrap::Tagged<
      std::string, ::crucible::fixy::tags::source::FromInternal>;

  void insert(
      NonZeroHash hash_nz,
      ExternalName filename,
      ExternalName funcname,
      int32_t lineno) {
    insert(hash_nz,
           std::move(filename).into(),
           std::move(funcname).into(),
           lineno);
  }

  void insert(
      NonZeroHash hash_nz,
      InternalName filename,
      InternalName funcname,
      int32_t lineno) {
    insert(hash_nz,
           std::move(filename).into(),
           std::move(funcname).into(),
           lineno);
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

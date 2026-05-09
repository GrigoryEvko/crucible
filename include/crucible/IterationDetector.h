#pragma once

#include <cstdint>
#include <cstring>

#include <crucible/Platform.h>
#include <crucible/Saturate.h>
#include <crucible/Types.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/Post.h>
#include <crucible/safety/Refined.h>

#include <memory>

namespace crucible {

// Detects iteration boundaries in a continuous stream of op schema hashes.
//
// Algorithm: sequential matching with cached expected value.
//
// Maintains a K=5 signature of the first ops seen. In steady state, a single
// uint64_t comparison per op determines if we're at a boundary — no rolling
// fingerprint, no ring buffer, no multiply. The expected next hash value is
// cached directly in the struct, so the hot path is:
//
//   inc  [ops_since_boundary]          ; 1 cycle, no dependency
//   cmp  [expected_hash_], incoming    ; 1 load (L1d hot) + 1 cmp
//   jne  .done                         ; well-predicted: taken (mismatch)
//
// ~1ns on Raptor Lake / Zen 4. The entire hot working set (expected_hash_,
// signature[0], match_pos_, ops_since_boundary) fits in ONE 64-byte cache
// line that stays perpetually in L1d. Zero writes on the common mismatch path
// except the ops_since_boundary increment.
//
// Two-match requirement:
//   First match  -> candidate (locks signature, but not confirmed yet)
//   Second match -> confirmed iteration boundary (returns true)
// This handles warmup: the first "iteration" may contain lazy init,
// module construction, or one-time setup ops that differ from steady state.
struct IterationDetector {
  static constexpr uint32_t K = 5;

  // Structural upper bound on match_pos_'s STORED value (#927 WRAP-
  // IterDet-1).  Storage range is [0, K-1] — at the K-th match the
  // hot path goes through on_match_() which resets match_pos_ to 0
  // before any further write, so the value K is never STORED in the
  // field.  bounded_above<K-1> is the tightest correct invariant.
  static constexpr uint8_t MATCH_POS_MAX = static_cast<uint8_t>(K - 1);

  // Refinement type for match_pos_'s storage.  Wraps uint8_t with
  // bounded_above<MATCH_POS_MAX> at the type level so a value > K-1
  // is a contract violation at construction (constexpr context: hard
  // compile error per P1494R5; runtime under semantic=enforce: abort).
  // BoolLattice<bounded_above<...>> EBO-collapses; sizeof(MatchPos)
  // == sizeof(uint8_t) == 1B — cache layout (line 0 = 64B) preserved
  // by structural guarantee, not by hand.  See the static_assert at
  // the bottom of the struct for the layout invariant.
  using MatchPos = ::crucible::safety::Refined<
      ::crucible::safety::bounded_above<MATCH_POS_MAX>, uint8_t>;

  // Refinement+monotonicity type for signature_len's storage (#928
  // WRAP-IterDet-2).  signature_len's behavior:
  //   - starts at 0
  //   - bumped exactly K times during signature build (0→1→…→K)
  //   - never advances past K — the `if (signature_len < K)` guard at
  //     the top of check() routes the K-th-onward calls to phase 2,
  //     which never mutates signature_len.
  //   - reset to 0 only by reset() (non-monotonic re-construct).
  //
  // BoundedMonotonic<uint32_t, K> nests Monotonic<uint32_t> inside a
  // bound-checked façade: every advance() / bump() carries
  // pre(new_value <= K), AND inner Monotonic carries pre(new_value
  // >= current).  Combined, the type rejects "skip ahead" and
  // "rewind" at the call site.  A future regression that increments
  // beyond K (via bump() at signature_len == K) fires a contract
  // violation; constexpr witness fixtures pin the bound check.
  //
  // Zero-cost: Monotonic<uint32_t, less<>> is regime-2 (T==element
  // type collapse), so sizeof(BoundedMonotonic<uint32_t, K>) ==
  // sizeof(uint32_t) == 4B — line-0 layout (offset 56 + 4B pad)
  // preserved by structural guarantee.
  using SignatureLen =
      ::crucible::safety::BoundedMonotonic<uint32_t, K>;

  // Monotonic-with-reset wrapper for ops_since_boundary's storage
  // (#929 WRAP-IterDet-3).  Within a single iteration epoch the
  // counter is strictly monotonic — every check() does +1 — so
  // safety::Monotonic<uint32_t> is the natural type.  Across epoch
  // boundaries (reset() and on_match_()'s "ops_since_boundary = K"
  // sites) the counter rewinds; those rewinds use std::construct_at
  // to re-establish the invariant from a known floor, mirroring the
  // boundaries_detected pattern below.  Any future code path that
  // tries to assign a backward value via .advance() fires a contract
  // violation; .bump() is overflow-checked at UINT32_MAX.
  //
  // Zero-cost: Graded's regime-2 (T==element_type) collapse pins
  // sizeof(Monotonic<uint32_t>) == sizeof(uint32_t) == 4B; layout
  // (offset 52) preserved by structural guarantee.
  using OpsSinceBoundary = ::crucible::safety::Monotonic<uint32_t>;

  // ── Cache line 0: hot path data (touched every call) ─────────
  // Expected next hash VALUE (not pointer). One L1d load, zero pointer chase.
  // In steady state (match_pos_.value()==0), this equals signature[0].
  SchemaHash expected_hash_{};                        // offset 0,  8B

  // The K-element signature: first K schema hashes of the iteration.
  // Read-only after build. signature[0] is hot (restart check on mid-match
  // break). Rest only accessed during the rare K-op match sequence.
  SchemaHash signature[K]{};                          // offset 8,  40B

  // Position in sequential match (0..K-1). 0 = waiting for signature[0].
  // MatchPos = Refined<bounded_above<K-1>, uint8_t> — the type carries
  // the invariant.  EBO-collapsed to 1B, saving 3 bytes for packing.
  MatchPos match_pos_{uint8_t{0}};                   // offset 48, 1B

  // True after first full K-match (candidate). Second match returns true.
  bool confirmed = false;                             // offset 49, 1B

  uint8_t pad0_[2]{};                                 // offset 50, 2B

  // Monotonic-with-reset counter (#929 WRAP-IterDet-3).  Strictly
  // increases via .bump() within an epoch; explicit rewinds
  // (reset() and on_match_()) use std::construct_at to reinstall the
  // invariant from a known floor.
  OpsSinceBoundary ops_since_boundary{0u};            // offset 52, 4B

  // Number of hashes collected during signature build (0..K).
  // After build completes, stays at K permanently — a structural
  // invariant pinned by SignatureLen (#928 WRAP-IterDet-2).  bump()
  // at signature_len.get()==K fires a contract violation; reset()
  // re-constructs in place to rewind for the next epoch.
  SignatureLen signature_len{0u};                     // offset 56, 4B

  uint8_t pad1_[4]{};                                 // offset 60, 4B
  // ── End cache line 0 (64 bytes) ──────────────────────────────

  // ── Cache line 1: cold data (touched only at boundaries) ─────
  // boundaries_detected is structurally monotonic: it only increments
  // on a confirmed iteration boundary.  Wrapped in Monotonic<> so the
  // invariant is enforced by the type, not by convention.
  crucible::safety::Monotonic<uint32_t> boundaries_detected {0};  // offset 64, 4B
  uint32_t last_completed_len = 0;                    // offset 68, 4B
  uint8_t pad2_[56]{};                                // offset 72, pad to 128B
  // ── End cache line 1 (64 bytes) ──────────────────────────────

  // Hot path: called once per drained op on the background thread.
  //
  // Steady-state fast path (no match, match_pos_.value()==0): ~1ns.
  //   - 1 increment (ops_since_boundary, parallel with everything)
  //   - 1 comparison (schema_hash vs expected_hash_, one L1d load)
  //   - 1 branch (well-predicted: mismatch)
  //   - 0 writes beyond the increment (expected_hash_ unchanged)
  //
  // Mid-match path (match_pos_.value()>0, advancing through signature): ~1.5ns.
  //   - 1 comparison (match) + 1 write (match_pos_, expected_hash_)
  //
  // Boundary path (match_pos_ reaches K-1+1 = K): ~50ns (memcpy + reset, rare).
  [[nodiscard, gnu::hot]] CRUCIBLE_INLINE bool check(SchemaHash schema_hash) noexcept {
    ops_since_boundary.bump();  // monotonic +1; pre at UINT32_MAX

    // Phase 1: building signature from first K ops.
    // Entered exactly K times total, then never again — invariant
    // pinned by SignatureLen's bump() bound (a K-th bump fires a
    // contract violation, so re-entering build_signature_ when
    // signature_len.get()==K would be structurally rejected).
    if (signature_len.get() < K) [[unlikely]] {
      return build_signature_(schema_hash);
    }

    // Phase 2: sequential matching.
    // Compare incoming hash against the expected next value.
    if (schema_hash != expected_hash_) [[likely]] {
      // Mismatch. Only do work if we were mid-match (match_pos_.value() > 0).
      // When match_pos_.value()==0, expected_hash_ is already signature[0] and
      // match_pos_ is already MatchPos{0} — zero writes needed.
      if (match_pos_.value() != 0) [[unlikely]] {
        // Mid-match broke. Reset to start.
        // Also check: does this hash start a NEW match?
        // (handles overlapping patterns at boundary transitions)
        if (schema_hash == signature[0]) [[unlikely]] {
          match_pos_ = MatchPos{uint8_t{1}};
          expected_hash_ = signature[1];
        } else {
          match_pos_ = MatchPos{uint8_t{0}};
          expected_hash_ = signature[0];
        }
      }
      return false;
    }

    // Match — advance to next position in signature.
    const auto next = static_cast<uint8_t>(match_pos_.value() + 1);
    if (next >= K) [[unlikely]] {
      return on_match_();
    }
    // next < K here, so next ≤ K-1 = MATCH_POS_MAX — the construction
    // contract holds by control flow.  Hot-path TUs compile this to
    // [[assume(next <= MATCH_POS_MAX)]], propagating the bound forward.
    match_pos_ = MatchPos{next};
    expected_hash_ = signature[next];
    return false;
  }

  void reset() {
    expected_hash_ = SchemaHash{};
    for (auto& h : signature) h = SchemaHash{};
    match_pos_ = MatchPos{uint8_t{0}};
    confirmed = false;
    // reset() is not a monotonic operation — it deliberately rewinds
    // the (Bounded)Monotonic counters on test/teardown.  Re-construct
    // each so the bound + monotonicity invariants are established
    // afresh from value 0.
    std::construct_at(&ops_since_boundary, OpsSinceBoundary{0u});
    std::construct_at(&signature_len, SignatureLen{0u});
    std::construct_at(&boundaries_detected,
                      crucible::safety::Monotonic<uint32_t>{0});
    last_completed_len = 0;
    // CONTRACT-IterDet-Reset-POST: state-machine reset invariant —
    // after reset(), every observable field is back to its
    // default-constructed value, restoring the Building-state-from-zero
    // initial condition.  This is the structural witness that #930
    // (WRAP-IterDet-4 reset() ScopedView state transition) codifies
    // at the type level: include/crucible/IterationDetectorState.h
    // ships the iter_det_state::{Building,Steady} tags + view_ok
    // overloads, so a caller can mint
    // `safety::ScopedView<IterationDetector, iter_det_state::Building>`
    // immediately after reset() and pass that proof downstream.  The
    // POSTs below back the typestate's value-level invariants; the
    // typestate is the compile-time witness that callers can carry.
    //   (1) match_pos_ raw == 0      — sequential matcher rewound to head
    //   (2) signature_len.get() == 0 — Building-state phase 1 (signature
    //                                   collection restarts from scratch)
    //   (3) ops_since_boundary.get() == 0 — counter at origin
    //   (4) boundaries_detected.get() == 0 — boundary-count history wiped
    //   (5) confirmed == false       — second-match witness reset
    //   (6) last_completed_len == 0  — no boundary completion recorded
    // Routes through CRUCIBLE_POST because every predicate references a
    // class member through `this->` — same GCC 16.1.1 consteval-bypass
    // family as CONTRACT-100..108-POST + 116..127-POST + Tx-*-POST +
    // Arena/AddBranch/MakeRegion factory POSTs.  Under NDEBUG these
    // collapse to `[[assume]]`, so the next check() call's hot path
    // can speculate that signature_len.bump() pre is satisfied
    // (current < K trivially when current == 0).  Void return: first
    // CRUCIBLE_POST arg is the conventional sentinel `0`.
    CRUCIBLE_POST(0, match_pos_.value() == 0u);
    CRUCIBLE_POST(0, signature_len.get() == 0u);
    CRUCIBLE_POST(0, ops_since_boundary.get() == 0u);
    CRUCIBLE_POST(0, boundaries_detected.get() == 0u);
    CRUCIBLE_POST(0, !confirmed);
    CRUCIBLE_POST(0, last_completed_len == 0u);
  }

 private:
  // Signature build: collect first K hashes. Called exactly K times.
  // The check() guard `signature_len.get() < K` admits this only
  // when bump() is in-bounds (current <= K-1), so the contract on
  // SignatureLen::bump (pre: current < K) holds by control flow.
  [[nodiscard]] bool build_signature_(SchemaHash schema_hash) {
    signature[signature_len.get()] = schema_hash;
    signature_len.bump();

    if (signature_len.get() == K) [[unlikely]] {
      // Signature complete. Prime the sequential matcher.
      expected_hash_ = signature[0];
      match_pos_ = MatchPos{uint8_t{0}};
    }
    return false;
  }

  // Boundary handler. Separated from hot path to keep check() tiny.
  // Called when K consecutive hashes matched the signature.
  [[nodiscard]] bool on_match_() {
    // Reset sequential matcher for next iteration.
    match_pos_ = MatchPos{uint8_t{0}};
    expected_hash_ = signature[0];

    if (!confirmed) [[unlikely]] {
      // First match — candidate, not yet confirmed.  Re-anchor the
      // counter at K (the K matched ops form this iteration's
      // signature, so they're K ops into the next epoch).  This is a
      // deliberate rewind from an arbitrary >= K value, so we use
      // construct_at to bypass Monotonic's monotonicity contract —
      // same pattern as reset() above.
      confirmed = true;
      std::construct_at(&ops_since_boundary, OpsSinceBoundary{K});
      return false;
    }

    // Second+ match — confirmed iteration boundary.
    last_completed_len =
        crucible::sat::sub_sat(ops_since_boundary.get(), K);
    std::construct_at(&ops_since_boundary, OpsSinceBoundary{K});
    boundaries_detected.bump();   // monotonicity-checked +1
    return true;
  }
};

static_assert(sizeof(IterationDetector) == 128,
              "IterationDetector must be exactly 2 cache lines");

} // namespace crucible

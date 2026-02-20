#pragma once

#include <cstdint>
#include <cstring>
#include <numeric>

#include <crucible/Platform.h>
#include <crucible/Types.h>

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

  // ── Cache line 0: hot path data (touched every call) ─────────
  // Expected next hash VALUE (not pointer). One L1d load, zero pointer chase.
  // In steady state (match_pos_==0), this equals signature[0].
  SchemaHash expected_hash_{};                        // offset 0,  8B

  // The K-element signature: first K schema hashes of the iteration.
  // Read-only after build. signature[0] is hot (restart check on mid-match
  // break). Rest only accessed during the rare K-op match sequence.
  SchemaHash signature[K]{};                          // offset 8,  40B

  // Position in sequential match (0..K-1). 0 = waiting for signature[0].
  // Using uint8_t since K=5 fits trivially, saving 3 bytes for packing.
  uint8_t match_pos_ = 0;                            // offset 48, 1B

  // True after first full K-match (candidate). Second match returns true.
  bool confirmed = false;                             // offset 49, 1B

  uint8_t pad0_[2]{};                                 // offset 50, 2B

  uint32_t ops_since_boundary = 0;                    // offset 52, 4B

  // Number of hashes collected during signature build (0..K).
  // After build completes, stays at K permanently.
  uint32_t signature_len = 0;                         // offset 56, 4B

  uint8_t pad1_[4]{};                                 // offset 60, 4B
  // ── End cache line 0 (64 bytes) ──────────────────────────────

  // ── Cache line 1: cold data (touched only at boundaries) ─────
  uint32_t boundaries_detected = 0;                   // offset 64, 4B
  uint32_t last_completed_len = 0;                    // offset 68, 4B
  uint8_t pad2_[56]{};                                // offset 72, pad to 128B
  // ── End cache line 1 (64 bytes) ──────────────────────────────

  // Hot path: called once per drained op on the background thread.
  //
  // Steady-state fast path (no match, match_pos_==0): ~1ns.
  //   - 1 increment (ops_since_boundary, parallel with everything)
  //   - 1 comparison (schema_hash vs expected_hash_, one L1d load)
  //   - 1 branch (well-predicted: mismatch)
  //   - 0 writes beyond the increment (expected_hash_ unchanged)
  //
  // Mid-match path (match_pos_>0, advancing through signature): ~1.5ns.
  //   - 1 comparison (match) + 1 write (match_pos_, expected_hash_)
  //
  // Boundary path (match_pos_ reaches K): ~50ns (memcpy + reset, rare).
  [[nodiscard]] CRUCIBLE_INLINE bool check(SchemaHash schema_hash) {
    ops_since_boundary++;

    // Phase 1: building signature from first K ops.
    // Entered exactly K times total, then never again.
    if (signature_len < K) [[unlikely]] {
      return build_signature_(schema_hash);
    }

    // Phase 2: sequential matching.
    // Compare incoming hash against the expected next value.
    if (schema_hash != expected_hash_) [[likely]] {
      // Mismatch. Only do work if we were mid-match (match_pos_ > 0).
      // When match_pos_==0, expected_hash_ is already signature[0] and
      // match_pos_ is already 0 — zero writes needed.
      if (match_pos_ != 0) [[unlikely]] {
        // Mid-match broke. Reset to start.
        // Also check: does this hash start a NEW match?
        // (handles overlapping patterns at boundary transitions)
        if (schema_hash == signature[0]) [[unlikely]] {
          match_pos_ = 1;
          expected_hash_ = signature[1];
        } else {
          match_pos_ = 0;
          expected_hash_ = signature[0];
        }
      }
      return false;
    }

    // Match — advance to next position in signature.
    uint8_t next = match_pos_ + 1;
    if (next >= K) [[unlikely]] {
      return on_match_();
    }
    match_pos_ = next;
    expected_hash_ = signature[next];
    return false;
  }

  void reset() {
    expected_hash_ = SchemaHash{};
    std::memset(signature, 0, sizeof(signature));
    match_pos_ = 0;
    confirmed = false;
    ops_since_boundary = 0;
    signature_len = 0;
    boundaries_detected = 0;
    last_completed_len = 0;
  }

 private:
  // Signature build: collect first K hashes. Called exactly K times.
  [[nodiscard]] bool build_signature_(SchemaHash schema_hash) {
    signature[signature_len] = schema_hash;
    signature_len++;

    if (signature_len == K) [[unlikely]] {
      // Signature complete. Prime the sequential matcher.
      expected_hash_ = signature[0];
      match_pos_ = 0;
    }
    return false;
  }

  // Boundary handler. Separated from hot path to keep check() tiny.
  // Called when K consecutive hashes matched the signature.
  [[nodiscard]] bool on_match_() {
    // Reset sequential matcher for next iteration.
    match_pos_ = 0;
    expected_hash_ = signature[0];

    if (!confirmed) [[unlikely]] {
      // First match — candidate, not yet confirmed.
      confirmed = true;
      ops_since_boundary = K;
      return false;
    }

    // Second+ match — confirmed iteration boundary.
    last_completed_len = std::sub_sat(ops_since_boundary, K);
    ops_since_boundary = K;
    boundaries_detected++;
    return true;
  }
};

static_assert(sizeof(IterationDetector) == 128,
              "IterationDetector must be exactly 2 cache lines");

} // namespace crucible

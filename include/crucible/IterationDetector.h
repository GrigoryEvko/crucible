#pragma once

#include <cstdint>

namespace crucible {

// Detects iteration boundaries in a continuous stream of op schema hashes.
//
// Maintains a signature of the first K ops seen. When the same K-op sequence
// appears again, an iteration boundary is detected. K=5 is sufficient to
// avoid false positives — the probability of 5 consecutive ops matching
// by chance is negligible for any real model.
//
// Two-match requirement:
//   First match  → candidate (locks signature, but not confirmed yet)
//   Second match → confirmed iteration boundary (returns true)
// This handles warmup: the first "iteration" may contain lazy init,
// module construction, or one-time setup ops that differ from steady state.
struct IterationDetector {
  static constexpr uint32_t K = 5;

  // First K schema hashes of the current trace.
  uint64_t signature[K]{};
  uint32_t signature_len = 0;
  bool signature_locked = false;
  bool confirmed = false; // true after first match (candidate)

  // Circular buffer of the most recent K schema hashes.
  uint64_t recent[K]{};
  uint32_t recent_pos = 0;

  // How many ops since the last boundary (= current iteration length).
  uint32_t ops_since_boundary = 0;

  // Number of boundaries detected so far.
  uint32_t boundaries_detected = 0;

  // Check if this op triggers an iteration boundary.
  // Returns true exactly at the boundary — the FIRST op of the new iteration.
  [[nodiscard]] bool check(uint64_t schema_hash) {
    recent[recent_pos % K] = schema_hash;
    recent_pos++;
    ops_since_boundary++;

    if (!signature_locked) {
      // Still building the signature from the first K ops.
      if (signature_len < K) {
        signature[signature_len++] = schema_hash;
        return false;
      }
    }

    // Need at least K ops to compare.
    if (recent_pos < K) {
      return false;
    }

    // Check if the last K ops match the signature.
    bool match = true;
    for (uint32_t i = 0; i < K; i++) {
      if (recent[(recent_pos - K + i) % K] != signature[i]) {
        match = false;
        break;
      }
    }

    if (match) {
      if (!confirmed) {
        // First match — candidate, not yet confirmed.
        // Lock the signature and reset, but don't report a boundary.
        confirmed = true;
        signature_locked = true;
        recent_pos = 0;
        ops_since_boundary = K;
        return false;
      }
      // Second+ match — confirmed iteration boundary.
      recent_pos = 0;
      ops_since_boundary = K;
      boundaries_detected++;
      return true;
    }
    return false;
  }

  void reset() {
    signature_len = 0;
    signature_locked = false;
    confirmed = false;
    recent_pos = 0;
    ops_since_boundary = 0;
    boundaries_detected = 0;
  }
};

} // namespace crucible

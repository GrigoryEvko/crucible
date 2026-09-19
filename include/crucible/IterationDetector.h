#pragma once

#include <cstdint>
#include <cstring>

#include <crucible/Platform.h>
#include <crucible/Saturate.h>
#include <crucible/Types.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/_Post.h>

#include <memory>

namespace crucible {

// Finds iteration boundaries in a continuous stream of schema hashes by
// matching against a signature taken from the first K hashes seen.
//
// A boundary is reported on the second match, not the first. The first
// iteration a program runs often contains lazy initialization and one-time
// setup that never recurs, so a single match is only a candidate.
//
// The field order and the padding put everything the matching step reads into
// the first line and everything else into the second.
struct IterationDetector {
    static constexpr uint32_t K = 5;

    // The stored value never reaches K. On the K-th match the handler rewinds
    // the position to zero before anything else writes the field, so K-1 is
    // the tightest bound the storage can carry.
    static constexpr uint8_t MATCH_POS_MAX = static_cast<uint8_t>(K - 1);

    using MatchPos = ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::bounded_above<MATCH_POS_MAX>, uint8_t>;

    // The length rises to K during signature collection and stops there: the
    // guard at the head of check() sends every later call to the matching
    // step, which does not touch it. Only reset() rewinds it.
    using SignatureLen = ::crucible::fixy::wrap::BoundedMonotonic<uint32_t, K>;

    // Monotonic within one iteration and rewound at each boundary. The rewind
    // sites construct a fresh counter in place rather than assigning, because
    // assigning backwards is what the type forbids.
    using OpsSinceBoundary = ::crucible::fixy::wrap::Monotonic<uint32_t>;

    // While no match is in progress this holds signature[0].
    SchemaHash expected_hash_{};

    // Read-only once collection finishes.
    SchemaHash signature[K]{};

    // Zero means the matcher is waiting for signature[0].
    MatchPos match_pos_{uint8_t{0}};

    // Set by the first full match, which is only a candidate.
    bool confirmed = false;

    uint8_t pad0_[2]{};

    OpsSinceBoundary ops_since_boundary{0u};

    SignatureLen signature_len{0u};

    uint8_t pad1_[4]{};

    // Read only at a boundary, so it sits in the second line.
    crucible::fixy::wrap::Monotonic<uint32_t> boundaries_detected{0};
    uint32_t last_completed_len = 0;
    uint8_t pad2_[56]{};

    [[nodiscard, gnu::hot]] CRUCIBLE_INLINE bool check(SchemaHash schema_hash) noexcept {
        ops_since_boundary.bump();

        // This branch is taken exactly K times over the object's life. A
        // K-plus-first entry would violate the length counter's own bound.
        if (signature_len.get() < K) [[unlikely]] {
            return build_signature_(schema_hash);
        }

        if (schema_hash != expected_hash_) [[likely]] {
            // With no match in progress both fields already hold what a
            // restart would write, so the common case writes nothing.
            if (match_pos_.value() != 0) [[unlikely]] {
                // A broken match can itself be the start of the next one,
                // which is what happens where two iterations abut.
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

        const auto next = static_cast<uint8_t>(match_pos_.value() + 1);
        if (next >= K) [[unlikely]] {
            return on_match_();
        }
        // The branch above leaves next below K, so the bound the storage type
        // demands holds by control flow.
        match_pos_ = MatchPos{next};
        expected_hash_ = signature[next];
        return false;
    }

    void reset() {
        expected_hash_ = SchemaHash{};
        for (auto& h : signature)
            h = SchemaHash{};
        match_pos_ = MatchPos{uint8_t{0}};
        confirmed = false;
        // Each counter runs backwards here, which is exactly what its type
        // refuses on assignment. Constructing a fresh one in place installs
        // the invariant again from zero.
        std::construct_at(&ops_since_boundary, OpsSinceBoundary{0u});
        std::construct_at(&signature_len, SignatureLen{0u});
        std::construct_at(&boundaries_detected, crucible::fixy::wrap::Monotonic<uint32_t>{0});
        last_completed_len = 0;
        CRUCIBLE_POST(0, match_pos_.value() == 0u);
        CRUCIBLE_POST(0, signature_len.get() == 0u);
        CRUCIBLE_POST(0, ops_since_boundary.get() == 0u);
        CRUCIBLE_POST(0, boundaries_detected.get() == 0u);
        CRUCIBLE_POST(0, !confirmed);
        CRUCIBLE_POST(0, last_completed_len == 0u);
    }

private:
    // The caller's guard admits this only while the length is below K, so the
    // bump below is in bounds by control flow.
    [[nodiscard]] bool build_signature_(SchemaHash schema_hash) {
        signature[signature_len.get()] = schema_hash;
        signature_len.bump();

        if (signature_len.get() == K) [[unlikely]] {
            expected_hash_ = signature[0];
            match_pos_ = MatchPos{uint8_t{0}};
        }
        return false;
    }

    [[nodiscard]] bool on_match_() {
        match_pos_ = MatchPos{uint8_t{0}};
        expected_hash_ = signature[0];

        if (!confirmed) [[unlikely]] {
            confirmed = true;
            // The K ops that just matched belong to the next iteration, so
            // the counter restarts at K rather than at zero. This is a rewind
            // from an arbitrary larger value, hence the in-place construction.
            std::construct_at(&ops_since_boundary, OpsSinceBoundary{K});
            return false;
        }

        last_completed_len = crucible::sat::sub_sat(ops_since_boundary.get(), K);
        std::construct_at(&ops_since_boundary, OpsSinceBoundary{K});
        boundaries_detected.bump();
        return true;
    }
};

static_assert(sizeof(IterationDetector) == 128, "IterationDetector must be exactly 2 cache lines");

}  // namespace crucible

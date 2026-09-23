#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <crucible/Expr.h>
#include <crucible/Platform.h>
#include <crucible/Types.h>
#include <crucible/fixy/Wrap.h>
#include <crucible/safety/_Post.h>

namespace crucible {

// Finds iteration boundaries in a continuous stream of operations.
//
// Each operation is one key, a mix of its schema hash and its shape hash.
// That is the pair the replay guard compares, so a period of keys is a
// period the guard accepts.
//
// The first K keys form the signature. A signature match only proposes a
// period. It does not prove one. An iteration can contain the signature more
// than once. An inference forward records the same ten ops for each Linear
// layer, so the signature matches at every layer.
//
// A period P is accepted only when the P ops before a signature match equal
// the P ops before those (a square). A square can also come from repeated
// layers inside one iteration. Such a period breaks when the stream stops
// repeating at it, and a period that is refuted is kept, with its body, so
// the search skips it. Otherwise the same layer square is found again after
// each restart.
//
// One break does not refute a period. The recorded stream has gaps: the
// foreground records nothing while it aligns or replays, and a gap breaks
// the true period once, after which it holds again. A period is refuted on
// its second break, or on a break followed by a divergence of the region
// that replayed it.
//
// A boundary is reported only at an accepted period, and each report proves
// that the iteration just completed equals the iteration before it.
//
// Assumptions:
//   - The detector runs on the background thread, never on the foreground
//     dispatch path. It keeps a history of keys, so check() is not O(1) in
//     memory. The history holds at most two iterations once a period is
//     accepted. Before that it holds every op since the last restart, which
//     is also what the caller's trace buffer holds.
//   - A period is at least K ops. A stream with a shorter true period gets
//     the smallest multiple of it that is K or more.
struct IterationDetector {
    static constexpr uint32_t K = 5;

    // Each period that breaks costs one entry. A body of N identical layers
    // can produce N/2 layer squares, and each one breaks.
    static constexpr uint32_t REFUTED_CAPACITY = 32;

    // The matcher's partial-match length. After a full match it falls back to
    // the signature's longest proper border, which is at most K-1, so the
    // stored value never reaches K.
    static constexpr uint8_t MATCH_POS_MAX = static_cast<uint8_t>(K - 1);

    using MatchPos = ::crucible::fixy::wrap::Refined<::crucible::fixy::wrap::bounded_above<MATCH_POS_MAX>, uint8_t>;

    // The length rises to K during signature collection and stops there. Only
    // reset() and restart_after_divergence() rewind it.
    using SignatureLen = ::crucible::fixy::wrap::BoundedMonotonic<uint32_t, K>;

    // Monotonic within one iteration and rewound at each boundary. The rewind
    // sites construct a fresh counter in place rather than assigning, because
    // assigning backwards is what the type forbids.
    using OpsSinceBoundary = ::crucible::fixy::wrap::Monotonic<uint32_t>;

    // A period is refuted after this many breaks.
    static constexpr uint32_t BREAKS_TO_REFUTE = 2;

    // One period that the stream failed to hold. The body fingerprint is a
    // sum over the body's keys, so it is the same for every rotation of the
    // body. A restart can land at any offset of the iteration.
    struct RefutedPeriod {
        uint32_t period = 0;
        uint64_t body_sum = 0;
        uint32_t breaks = 0;
        // Broke in the history the detector holds, since the last restart.
        bool broke_since_restart = false;
    };

    uint64_t signature[K]{};

    // failure_[i] is the length of the longest proper border of
    // signature[0..i]. It lets the matcher find overlapping matches.
    uint8_t failure_[K]{};

    MatchPos match_pos_{uint8_t{0}};

    // True while a period is accepted.
    bool confirmed = false;

    // The phase window of the accepted period occurs once per period. Only
    // then does a divergence refute the period: with a repeated phase window
    // the foreground can align at the wrong copy, and the divergence says
    // nothing about the period.
    bool phase_is_unique = false;

    SignatureLen signature_len{0u};
    OpsSinceBoundary ops_since_boundary{0u};
    crucible::fixy::wrap::Monotonic<uint32_t> boundaries_detected{0};
    uint32_t last_completed_len = 0;

    // Zero while no period is accepted.
    uint32_t period_ = 0;

    // The body fingerprint of the accepted period.
    uint64_t period_body_sum_ = 0;

    // How many accepted periods have reported a boundary since the last
    // restart, and whether the current one has.
    uint32_t periods_reported_ = 0;
    bool current_period_reported_ = false;

    // Absolute index of the next op that check() receives.
    uint64_t pos_ = 0;

    // Absolute index where the next iteration starts, while a period is
    // accepted.
    uint64_t next_boundary_ = 0;

    // Absolute index of keys_[0].
    uint64_t base_ = 0;

    // The history. prefix_poly_[i] and prefix_sum_[i] fold keys_[0..i), so
    // each vector has one more element than keys_.
    std::vector<uint64_t> keys_;
    std::vector<uint64_t> prefix_poly_{0};
    std::vector<uint64_t> prefix_sum_{0};

    // powers_[n] is POLY_BASE to the power n. It only grows.
    std::vector<uint64_t> powers_{1};

    // Absolute start index of each signature match in the history, ascending.
    std::vector<uint64_t> match_starts_;

    // A ring of periods that broke at least once. It survives
    // restart_after_divergence(), and only reset() clears it.
    RefutedPeriod refuted_[REFUTED_CAPACITY]{};
    uint32_t refuted_count_ = 0;
    uint32_t refuted_next_ = 0;

    // Reports whether the op just received completes an iteration. When it
    // does, last_completed_len holds the iteration length, and the K ops just
    // received are the first K ops of the next iteration.
    //
    // Complexity: amortized O(1) per op while a period is accepted. While the
    // detector searches, each signature match tests every earlier match in
    // the second half of the history, which is O(M) for M matches. A search
    // is O(M^2) in total.
    [[nodiscard]] bool check(SchemaHash schema_hash, ShapeHash shape_hash = ShapeHash{}) {
        ops_since_boundary.bump();
        const uint64_t key = key_of_(schema_hash, shape_hash);
        append_(key);
        const uint64_t op_index = pos_;
        ++pos_;

        // This branch is taken exactly K times between restarts. A K-plus-first
        // entry would violate the length counter's own bound.
        if (signature_len.get() < K) [[unlikely]] {
            return build_signature_(key, op_index);
        }

        const bool matched = advance_match_(key);
        if (matched) match_starts_.push_back(op_index + 1 - K);

        if (period_ != 0) {
            if (op_index + 1 == next_boundary_ + K) return verify_boundary_();
            return false;
        }
        if (!matched) return false;
        return search_(op_index + 1 - K);
    }

    // Clears everything, the refuted periods included.
    void reset() {
        restart_();
        refuted_count_ = 0;
        refuted_next_ = 0;
        for (auto& entry : refuted_)
            entry = RefutedPeriod{};
        CRUCIBLE_POST(0, refuted_count_ == 0u);
    }

    // Starts again from an empty history after the foreground diverged from
    // a published region.
    //
    // The accepted period is refuted first when the region that diverged can
    // only have been cut at that period. The foreground never says which
    // region it replayed, so that holds only when this period is the one
    // period that reported a boundary since the last restart. A period
    // accepted but not yet reported published no region. A period accepted
    // after an earlier one was reported cannot be told apart from it, and the
    // earlier one is usually the region that diverged: the foreground aligns
    // to the first region published. Refuting the wrong one would force a
    // period of two iterations onto a stream that has one.
    //
    // A period that broke since the last restart and reported a boundary
    // before it broke published a region, and the divergence is its second
    // strike. That is the layer square a replay diverged from.
    void restart_after_divergence() {
        const bool diverged_at_this_period = period_ != 0 && current_period_reported_ && periods_reported_ == 1;
        if (diverged_at_this_period && phase_is_unique) break_(period_, period_body_sum_, BREAKS_TO_REFUTE);
        for (uint32_t i = 0; i < refuted_count_; ++i) {
            if (refuted_[i].broke_since_restart) refuted_[i].breaks = BREAKS_TO_REFUTE;
            refuted_[i].broke_since_restart = false;
        }
        restart_();
    }

    [[nodiscard]] bool is_refuted(uint32_t period, uint64_t body_sum) const noexcept {
        for (uint32_t i = 0; i < refuted_count_; ++i) {
            if (refuted_[i].period == period && refuted_[i].body_sum == body_sum)
                return refuted_[i].breaks >= BREAKS_TO_REFUTE;
        }
        return false;
    }

private:
    static constexpr uint64_t POLY_BASE = 0x9E3779B97F4A7C15ULL;
    static constexpr uint64_t SUM_SALT = 0xD6E8FEB86659FD93ULL;

    [[nodiscard]] static constexpr uint64_t key_of_(SchemaHash schema_hash, ShapeHash shape_hash) noexcept {
        return detail::fmix64(schema_hash.raw() ^ detail::fmix64(shape_hash.raw() + POLY_BASE));
    }

    void append_(uint64_t key) {
        keys_.push_back(key);
        prefix_poly_.push_back(prefix_poly_.back() * POLY_BASE + key);
        prefix_sum_.push_back(prefix_sum_.back() + detail::fmix64(key ^ SUM_SALT));
        if (powers_.size() <= keys_.size()) powers_.push_back(powers_.back() * POLY_BASE);
    }

    void restart_() {
        for (auto& h : signature)
            h = 0;
        for (auto& f : failure_)
            f = 0;
        match_pos_ = MatchPos{uint8_t{0}};
        confirmed = false;
        phase_is_unique = false;
        // Each counter runs backwards here, which is exactly what its type
        // refuses on assignment. Constructing a fresh one in place installs
        // the invariant again from zero.
        std::construct_at(&ops_since_boundary, OpsSinceBoundary{0u});
        std::construct_at(&signature_len, SignatureLen{0u});
        std::construct_at(&boundaries_detected, crucible::fixy::wrap::Monotonic<uint32_t>{0});
        last_completed_len = 0;
        period_ = 0;
        period_body_sum_ = 0;
        periods_reported_ = 0;
        current_period_reported_ = false;
        pos_ = 0;
        next_boundary_ = 0;
        base_ = 0;
        keys_.clear();
        prefix_poly_.assign(1, 0);
        prefix_sum_.assign(1, 0);
        match_starts_.clear();
        CRUCIBLE_POST(0, match_pos_.value() == 0u);
        CRUCIBLE_POST(0, signature_len.get() == 0u);
        CRUCIBLE_POST(0, ops_since_boundary.get() == 0u);
        CRUCIBLE_POST(0, boundaries_detected.get() == 0u);
        CRUCIBLE_POST(0, !confirmed);
        CRUCIBLE_POST(0, last_completed_len == 0u);
    }

    // Records breaks against a period. The ring evicts its oldest entry when
    // it is full.
    void break_(uint32_t period, uint64_t body_sum, uint32_t breaks) {
        for (uint32_t i = 0; i < refuted_count_; ++i) {
            if (refuted_[i].period == period && refuted_[i].body_sum == body_sum) {
                refuted_[i].breaks = std::min(BREAKS_TO_REFUTE, refuted_[i].breaks + breaks);
                return;
            }
        }
        refuted_[refuted_next_] = RefutedPeriod{
            .period = period, .body_sum = body_sum, .breaks = std::min(BREAKS_TO_REFUTE, breaks),
            .broke_since_restart = false};
        refuted_next_ = (refuted_next_ + 1) % REFUTED_CAPACITY;
        if (refuted_count_ < REFUTED_CAPACITY) ++refuted_count_;
    }

    // The caller's guard admits this only while the length is below K, so the
    // bump below is in bounds by control flow.
    [[nodiscard]] bool build_signature_(uint64_t key, uint64_t op_index) {
        signature[signature_len.get()] = key;
        signature_len.bump();
        if (signature_len.get() < K) return false;

        // The standard border table of the signature.
        failure_[0] = 0;
        uint8_t border = 0;
        for (uint32_t i = 1; i < K; ++i) {
            while (border > 0 && signature[i] != signature[border])
                border = failure_[border - 1];
            if (signature[i] == signature[border]) ++border;
            failure_[i] = border;
        }
        // The signature itself is the first match.
        match_pos_ = MatchPos{failure_[K - 1]};
        match_starts_.push_back(op_index + 1 - K);
        return false;
    }

    // One step of the border-table matcher. Returns true when the key just
    // received completes a match.
    [[nodiscard]] bool advance_match_(uint64_t key) {
        uint8_t matched = match_pos_.value();
        while (matched > 0 && key != signature[matched])
            matched = failure_[matched - 1];
        if (key == signature[matched]) ++matched;
        if (matched == K) {
            // The border is at most K-1, which keeps the stored value inside
            // the bound the storage type demands.
            match_pos_ = MatchPos{failure_[K - 1]};
            return true;
        }
        match_pos_ = MatchPos{matched};
        return false;
    }

    [[nodiscard]] uint64_t window_poly_(uint64_t begin, uint64_t end) const noexcept {
        const uint64_t lo = begin - base_;
        const uint64_t hi = end - base_;
        return prefix_poly_[hi] - prefix_poly_[lo] * powers_[hi - lo];
    }

    [[nodiscard]] uint64_t window_sum_(uint64_t begin, uint64_t end) const noexcept {
        return prefix_sum_[end - base_] - prefix_sum_[begin - base_];
    }

    // True when keys [left, left+length) equal keys [right, right+length).
    // The fingerprints reject almost every unequal pair in O(1). The key
    // compare makes the answer exact, and it runs O(length) only on a pair
    // that is almost surely equal.
    [[nodiscard]] bool windows_equal_(uint64_t left, uint64_t right, uint64_t length) const {
        if (window_poly_(left, left + length) != window_poly_(right, right + length)) return false;
        const auto* data = keys_.data();
        return std::equal(data + (left - base_), data + (left - base_ + length), data + (right - base_));
    }

    // Is [start - 2P, start) two copies of one body of length P?
    [[nodiscard]] bool is_square_(uint64_t start, uint64_t period) const {
        return windows_equal_(start - 2 * period, start - period, period);
    }

    [[nodiscard]] bool search_(uint64_t start) {
        // The newest match is the one at start itself. The others give
        // candidate periods in increasing order.
        for (size_t idx = match_starts_.size() - 1; idx-- > 0;) {
            const uint64_t period = start - match_starts_[idx];
            if (period < K) continue;
            if (start - base_ < 2 * period) break;
            if (!is_square_(start, period)) continue;
            const uint64_t body_sum = window_sum_(start - period, start);
            if (is_refuted(static_cast<uint32_t>(period), body_sum)) continue;
            return accept_(start, static_cast<uint32_t>(period), body_sum);
        }
        return false;
    }

    // Accepts a period whose square ends at start, and picks the phase at
    // which iterations begin.
    //
    // The foreground aligns a published region by its first K ops. A phase
    // whose K-op window occurs once per period aligns at one place only. A
    // window that repeats inside the period can align at the wrong copy, so
    // the phase is the window that occurs the fewest times, and the earliest
    // such window on a tie.
    //
    // Complexity: O(P log P), once per accepted period.
    [[nodiscard]] bool accept_(uint64_t start, uint32_t period, uint64_t body_sum) {
        // The window at start - P + phase lies wholly inside the history,
        // because the newest op is start + K - 1.
        std::vector<std::pair<uint64_t, uint32_t>> windows(period);
        for (uint32_t phase = 0; phase < period; ++phase) {
            const uint64_t begin = start - period + phase;
            windows[phase] = {window_poly_(begin, begin + K), phase};
        }
        std::sort(windows.begin(), windows.end());

        uint32_t best_phase = 0;
        uint32_t best_count = UINT32_MAX;
        for (size_t lo = 0; lo < windows.size();) {
            size_t hi = lo;
            while (hi < windows.size() && windows[hi].first == windows[lo].first)
                ++hi;
            const auto count = static_cast<uint32_t>(hi - lo);
            // The pairs sort by phase inside one fingerprint, so windows[lo]
            // holds the earliest phase of this group.
            const uint32_t phase = windows[lo].second;
            if (count < best_count || (count == best_count && phase < best_phase)) {
                best_count = count;
                best_phase = phase;
            }
            lo = hi;
        }

        period_ = period;
        period_body_sum_ = body_sum;
        current_period_reported_ = false;
        phase_is_unique = best_count == 1;
        confirmed = true;
        next_boundary_ = start + best_phase;
        if (best_phase == 0) return fire_();
        return false;
    }

    // Runs when the op at next_boundary_ + K - 1 arrives.
    [[nodiscard]] bool verify_boundary_() {
        const uint64_t boundary = next_boundary_;
        const bool holds = is_square_(boundary, period_) && windows_equal_(boundary - period_, boundary, K);
        if (holds) return fire_();
        break_(period_, period_body_sum_, 1);
        if (current_period_reported_) mark_broke_since_restart_(period_, period_body_sum_);
        period_ = 0;
        confirmed = false;
        phase_is_unique = false;
        return false;
    }

    void mark_broke_since_restart_(uint32_t period, uint64_t body_sum) {
        for (uint32_t i = 0; i < refuted_count_; ++i) {
            if (refuted_[i].period == period && refuted_[i].body_sum == body_sum) refuted_[i].broke_since_restart = true;
        }
    }

    [[nodiscard]] bool fire_() {
        if (!current_period_reported_) {
            current_period_reported_ = true;
            ++periods_reported_;
        }
        last_completed_len = period_;
        // The K ops just received belong to the next iteration, so the
        // counter restarts at K. This is a rewind from an arbitrary larger
        // value, hence the in-place construction.
        std::construct_at(&ops_since_boundary, OpsSinceBoundary{K});
        boundaries_detected.bump();
        const uint64_t boundary = next_boundary_;
        next_boundary_ = boundary + period_;
        trim_(boundary - period_);
        return true;
    }

    // Drops the history before new_base. The fingerprints of the kept
    // entries stay valid, because a window fingerprint only differences two
    // prefixes. Amortized O(1) per op: the erase moves the kept entries, at
    // most 2P of them, once per iteration of P ops.
    void trim_(uint64_t new_base) {
        if (new_base <= base_) return;
        const auto drop = static_cast<std::ptrdiff_t>(new_base - base_);
        keys_.erase(keys_.begin(), keys_.begin() + drop);
        prefix_poly_.erase(prefix_poly_.begin(), prefix_poly_.begin() + drop);
        prefix_sum_.erase(prefix_sum_.begin(), prefix_sum_.begin() + drop);
        const auto first_kept = std::lower_bound(match_starts_.begin(), match_starts_.end(), new_base);
        match_starts_.erase(match_starts_.begin(), first_kept);
        base_ = new_base;
    }
};

}  // namespace crucible

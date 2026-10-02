#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>

#include <crucible/Expr.h>
#include <crucible/Types.h>
#include <fixy/Mutation.h>
#include <fixy/Refined.h>
#include <foundation/AlignedBuffer.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Post.h>

namespace crucible {

// Finds iteration boundaries in a continuous stream of operations.
//
// Each operation is one key, a mix of its schema hash and its shape hash.
// That is the pair the replay guard compares, so a period of keys is a
// period the guard accepts.
//
// The window of an op is the K keys that end at it.  A table maps the
// fingerprint of each window to the newest op where an equal window starts.
// Each window also keeps a link to the window before it with the same
// fingerprint.  A period can thus start at any op of the stream, and a
// prefix of ops that never occur again does not stop the search.
//
// An earlier equal window only proposes a period.  It does not prove one.
// An iteration can contain a window more than once.  An inference forward
// records the same ten ops for each Linear layer, so a window of one layer
// occurs at every layer.
//
// A period P is accepted only when the P ops before a window equal the P ops
// before those (a square).  The search tests the earlier equal windows of the
// newest window, the nearest first.  A square can also come from repeated
// layers inside one iteration.  Such a period breaks when the stream stops
// repeating at it, and a period that is refuted is kept, with its body, so
// the search skips it.  Otherwise the same layer square is found again after
// each restart.
//
// One break does not refute a period.  The recorded stream has gaps: the
// foreground records nothing while it aligns or replays, and a gap breaks
// the true period once, after which it holds again.  A period is refuted on
// its second break, or on a break followed by a divergence of the region
// that replayed it.
//
// A boundary is reported only at an accepted period, and each report proves
// that the iteration just completed equals the iteration before it.
//
// Assumptions:
//   - The detector runs on the background thread, never on the foreground
//     dispatch path.  It keeps a history of keys in a ring that grows from
//     HISTORY_MIN_CAPACITY to HISTORY_MAX_CAPACITY slots, and a window table
//     of two slots for each slot of the ring.  At the largest capacity the
//     two take 32 MiB.
//   - A period is at least K ops.  A stream with a shorter true period gets
//     the smallest multiple of it that is K or more.
//   - A period is at most MAX_PERIOD ops, because a square of P ops needs
//     2P + K + 1 slots of history.  When the history fills the largest ring,
//     the detector forgets its oldest op for each new op.  A stream whose
//     true period is longer than MAX_PERIOD then gets no boundary, and the
//     memory of the detector stays at the bound.
//   - The search tests at most MAX_CANDIDATES earlier windows for each op.
//     A stream from a small set of keys can repeat a window many times, and
//     the bound keeps the cost of one op constant.
struct IterationDetector {
    static constexpr uint32_t K = 5;

    // Each period that breaks costs one entry.  A body of N identical layers
    // can produce N/2 layer squares, and each one breaks.
    static constexpr uint32_t REFUTED_CAPACITY = 32;

    // The ring of the history grows by doubling between these capacities.
    // A small model keeps a small ring, because a confirmed period trims
    // the history to two iterations.
    static constexpr uint32_t HISTORY_MIN_CAPACITY = uint32_t{1} << 12;
    static constexpr uint32_t HISTORY_MAX_CAPACITY = uint32_t{1} << 19;

    // A square of P ops ending at window start s needs the keys of
    // [s - 2P, s + K) and the prefix before s - 2P.
    static constexpr uint32_t MAX_PERIOD = (HISTORY_MAX_CAPACITY - K - 1) / 2;

    static constexpr uint32_t MAX_CANDIDATES = 256;

    // The one door into Period is ::fixy::mint_refined<kPeriodBound>(length).
    static constexpr auto kPeriodBound = ::fixy::in_range<K, MAX_PERIOD>;
    using Period = ::fixy::Refined<kPeriodBound, uint32_t>;

    // The number of keys in the window of the newest op.  It rises to K
    // after a restart and stops there.  Only reset() and
    // restart_after_divergence() rewind it.
    using SignatureLen = ::fixy::BoundedMonotonic<uint32_t, K>;

    // Monotonic within one iteration and rewound at each boundary.  The
    // rewind sites call reset_under_quiescence, because an assignment
    // backwards is what the type forbids.
    using OpsSinceBoundary = ::fixy::Monotonic<uint32_t>;

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

    // True while a period is accepted.
    bool confirmed = false;

    // The phase window of the accepted period occurs once per period. Only
    // then does a divergence refute the period: with a repeated phase window
    // the foreground can align at the wrong copy, and the divergence says
    // nothing about the period.
    bool phase_is_unique = false;

    SignatureLen signature_len = ::fixy::mint_bounded_monotonic<uint32_t, K>(0u);
    OpsSinceBoundary ops_since_boundary = ::fixy::mint_monotonic<uint32_t>(0u);
    ::fixy::Monotonic<uint32_t> boundaries_detected = ::fixy::mint_monotonic<uint32_t>(0u);
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

    // Absolute index of the oldest op of the history.  The ring also holds
    // the op before it, whose prefix fingerprints start the history.
    uint64_t base_ = 0;

    // A ring of periods that broke at least once. It survives
    // restart_after_divergence(), and only reset() clears it.
    RefutedPeriod refuted_[REFUTED_CAPACITY]{};
    uint32_t refuted_count_ = 0;
    uint32_t refuted_next_ = 0;

    // Reports whether the op just received completes an iteration. When it
    // does, last_completed_len holds the iteration length, and the K ops just
    // received are the first K ops of the next iteration.
    //
    // Complexity: amortized O(1) per op.  While the detector searches, each
    // op inserts its window into the table and tests at most MAX_CANDIDATES
    // earlier windows, and each test costs O(1) unless the fingerprints of a
    // square agree.  While a period holds, the detector does not update the
    // table, and a break builds it again in O(history).  An accepted period
    // costs O(P) one time.
    [[nodiscard]] bool check(SchemaHash schema_hash, ShapeHash shape_hash = ShapeHash{}) {
        ops_since_boundary.bump();
        const uint64_t key = key_of_(schema_hash, shape_hash);
        append_(key);
        const uint64_t op_index = pos_;
        ++pos_;

        // The bump is in bounds, because the guard admits it only while the
        // length is below K.
        if (signature_len.get() < K) signature_len.bump();
        if (signature_len.get() < K) return false;

        if (period_ != 0) {
            if (op_index + 1 == next_boundary_ + K) return verify_boundary_();
            return false;
        }
        const uint64_t start = op_index + 1 - K;
        return search_(start, link_window_(start));
    }

    // The oldest op that a future report of a boundary can put in its
    // iteration, as an absolute index of check().
    //
    // While the detector searches, a square of P ops that a later op
    // completes ends at a window start s after the newest window start, and
    // its history starts at s - 2P, at or after base_.  Its iteration then
    // starts at s - P, at or after (s + base_) / 2.  While a period holds,
    // the next iteration starts at next_boundary_ - period_.  The period can
    // still break and give the search back the same history, so the result
    // is the smaller of the two.  A change to the rule that accepts a period
    // must change this function too.
    [[nodiscard]] uint64_t oldest_claimable() const noexcept {
        const uint64_t newest_start = pos_ > K ? pos_ - K : 0;
        const uint64_t search_floor = newest_start > base_ ? base_ + (newest_start - base_) / 2 : base_;
        if (period_ == 0) return search_floor;
        const uint64_t next_iteration = next_boundary_ - period_;
        return next_iteration < search_floor ? next_iteration : search_floor;
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

    // The slots of the ring of the history, zero before the first check().
    [[nodiscard]] uint64_t history_capacity() const noexcept { return history_mask_ == 0 ? 0 : history_mask_ + 1; }

private:
    static constexpr uint64_t POLY_BASE = 0x9E3779B97F4A7C15ULL;
    static constexpr uint64_t SUM_SALT = 0xD6E8FEB86659FD93ULL;

    // POLY_BASE to the power exponent, modulo 2^64.  Complexity: O(log
    // exponent).
    [[nodiscard]] static constexpr uint64_t power_of_base_(uint64_t exponent) noexcept {
        uint64_t result = 1;
        uint64_t factor = POLY_BASE;
        while (exponent != 0) {
            if ((exponent & 1) != 0) result *= factor;
            factor *= factor;
            exponent >>= 1;
        }
        return result;
    }

    [[nodiscard]] static constexpr uint64_t key_of_(SchemaHash schema_hash, ShapeHash shape_hash) noexcept {
        return detail::fmix64(schema_hash.raw() ^ detail::fmix64(shape_hash.raw() + POLY_BASE));
    }

    // The ring holds the ops from held_from_() up to pos_.
    [[nodiscard]] uint64_t held_from_() const noexcept { return base_ == 0 ? 0 : base_ - 1; }

    // Makes room in the ring for the op at pos_.  The ring grows while it is
    // below its largest capacity.  At the largest capacity, the oldest ops
    // leave the history.
    void make_room_() {
        if (history_mask_ == 0) {
            grow_history_(HISTORY_MIN_CAPACITY);
            return;
        }
        const uint64_t capacity = history_mask_ + 1;
        if (pos_ + 1 - held_from_() <= capacity) return;
        if (capacity < HISTORY_MAX_CAPACITY) {
            grow_history_(capacity * 2);
            return;
        }
        // The ring also holds the op before base_, so the first eviction
        // moves base_ from zero to two.
        forget_before_(pos_ + 2 - capacity);
    }

    // Drops the history before new_base.  While the detector searches, it
    // also removes from the table each window that starts there and is the
    // newest of its fingerprint, so the table holds only windows of the
    // history.  Complexity: O(1) for each dropped op.
    void forget_before_(uint64_t new_base) {
        if (period_ == 0) {
            for (uint64_t start = base_; start < new_base; ++start) {
                if (start + K <= pos_) table_remove_newest_(window_fingerprint_(start), start);
            }
        }
        base_ = new_base;
    }

    // Moves the held ops into a ring of new_capacity slots, and builds the
    // window table again for it.  Complexity: O(new_capacity).
    void grow_history_(uint64_t new_capacity) {
        auto keys = ::foundation::AlignedBuffer<uint64_t>::allocate_value_initialized(new_capacity);
        auto prefix_polys = ::foundation::AlignedBuffer<uint64_t>::allocate_value_initialized(new_capacity);
        auto prefix_sums = ::foundation::AlignedBuffer<uint64_t>::allocate_value_initialized(new_capacity);
        auto earlier_links = ::foundation::AlignedBuffer<uint64_t>::allocate_value_initialized(new_capacity);
        const uint64_t new_mask = new_capacity - 1;
        for (uint64_t position = held_from_(); position < pos_; ++position) {
            keys[position & new_mask] = keys_[position & history_mask_];
            prefix_polys[position & new_mask] = prefix_polys_[position & history_mask_];
            prefix_sums[position & new_mask] = prefix_sums_[position & history_mask_];
            earlier_links[position & new_mask] = earlier_links_[position & history_mask_];
        }
        keys_ = std::move(keys);
        prefix_polys_ = std::move(prefix_polys);
        prefix_sums_ = std::move(prefix_sums);
        earlier_links_ = std::move(earlier_links);
        history_mask_ = new_mask;
        // The windows that start before the window of the op at pos_ are
        // complete.  While a period holds, the detector does not update the
        // table, and a break builds it for the ring of that time.
        if (period_ == 0) rebuild_table_(new_capacity * 2, pos_ + 1 > K ? pos_ + 1 - K : 0);
    }

    void append_(uint64_t key) {
        make_room_();
        const uint64_t slot = pos_ & history_mask_;
        const uint64_t poly_before = prefix_poly_before_(pos_);
        const uint64_t sum_before = prefix_sum_before_(pos_);
        keys_[slot] = key;
        prefix_polys_[slot] = poly_before * POLY_BASE + key;
        prefix_sums_[slot] = sum_before + detail::fmix64(key ^ SUM_SALT);
        earlier_links_[slot] = 0;
    }

    // The fingerprint of the keys before index, for an index from base_ up
    // to pos_.
    [[nodiscard]] uint64_t prefix_poly_before_(uint64_t index) const noexcept {
        return index == 0 ? 0 : prefix_polys_[(index - 1) & history_mask_];
    }

    [[nodiscard]] uint64_t prefix_sum_before_(uint64_t index) const noexcept {
        return index == 0 ? 0 : prefix_sums_[(index - 1) & history_mask_];
    }

    [[nodiscard]] uint64_t window_fingerprint_(uint64_t start) const noexcept {
        return prefix_poly_before_(start + K) - prefix_poly_before_(start) * power_of_base_(K);
    }

    [[nodiscard]] uint64_t window_sum_(uint64_t begin, uint64_t end) const noexcept {
        return prefix_sum_before_(end) - prefix_sum_before_(begin);
    }

    // Records the window that starts at start as the newest window with its
    // fingerprint.  Returns the link to the newest earlier window with the
    // same fingerprint: its start plus one, or zero when the history holds
    // none.
    [[nodiscard]] uint64_t link_window_(uint64_t start) {
        const uint64_t earlier_link = table_exchange_(window_fingerprint_(start), start);
        earlier_links_[start & history_mask_] = earlier_link;
        return earlier_link;
    }

    // The table holds one slot for each fingerprint of a window of the
    // history: the start of the newest such window plus one, or zero for an
    // empty slot, and the fingerprint.  The windows of the history are at
    // most the slots of the ring, and the table has two slots for each slot
    // of the ring, so its load stays at most one half.  Linear probing.
    [[nodiscard]] uint64_t home_slot_(uint64_t fingerprint) const noexcept {
        return detail::fmix64(fingerprint) & table_mask_;
    }

    // Records start as the newest window of its fingerprint, and returns the
    // slot value that it replaces: the earlier start plus one, or zero.
    [[nodiscard]] uint64_t table_exchange_(uint64_t fingerprint, uint64_t start) {
        uint64_t index = home_slot_(fingerprint);
        while (table_[index].start_plus_one != 0) {
            if (table_[index].fingerprint == fingerprint) {
                const uint64_t earlier = table_[index].start_plus_one;
                table_[index].start_plus_one = start + 1;
                return earlier;
            }
            index = (index + 1) & table_mask_;
        }
        table_[index] = TableSlot{.start_plus_one = start + 1, .fingerprint = fingerprint};
        ++table_used_;
        return 0;
    }

    // Removes the slot of the fingerprint when start is its newest window.
    // The slots after it in the probe run move back, so each probe run stays
    // unbroken (Knuth, The Art of Computer Programming, volume 3, 6.4,
    // Algorithm R).
    void table_remove_newest_(uint64_t fingerprint, uint64_t start) {
        uint64_t hole = home_slot_(fingerprint);
        while (table_[hole].start_plus_one != 0 && table_[hole].fingerprint != fingerprint)
            hole = (hole + 1) & table_mask_;
        if (table_[hole].start_plus_one != start + 1) return;
        uint64_t next = hole;
        while (true) {
            next = (next + 1) & table_mask_;
            if (table_[next].start_plus_one == 0) break;
            // The slot at next can move to the hole when its home is not
            // in the cyclic range (hole, next].
            const uint64_t home = home_slot_(table_[next].fingerprint);
            const bool home_after_hole = ((home - hole - 1) & table_mask_) < ((next - hole) & table_mask_);
            if (home_after_hole) continue;
            table_[hole] = table_[next];
            hole = next;
        }
        table_[hole] = TableSlot{};
        --table_used_;
    }

    // Builds a table of size slots from the windows that start in
    // [base_, end_start), in order, and links each window to the newest
    // earlier window of its fingerprint.  Complexity: O(size).
    void rebuild_table_(uint64_t size, uint64_t end_start) {
        table_ = ::foundation::AlignedBuffer<TableSlot, sizeof(TableSlot)>::allocate_value_initialized(size);
        table_mask_ = size - 1;
        table_used_ = 0;
        for (uint64_t start = base_; start < end_start; ++start)
            earlier_links_[start & history_mask_] = table_exchange_(window_fingerprint_(start), start);
    }

    void restart_() {
        confirmed = false;
        phase_is_unique = false;
        // Each counter runs backwards here, which is exactly what its type
        // refuses on assignment.  One thread owns the detector, so each
        // rewind is under quiescence.
        ops_since_boundary.reset_under_quiescence(0u);
        signature_len.reset_under_quiescence(0u);
        boundaries_detected.reset_under_quiescence(0u);
        last_completed_len = 0;
        period_ = 0;
        period_body_sum_ = 0;
        periods_reported_ = 0;
        current_period_reported_ = false;
        pos_ = 0;
        next_boundary_ = 0;
        base_ = 0;
        // The ring keeps its slots, and the empty history makes each one
        // dead.  The table keeps its size, and each slot becomes empty.
        if (table_mask_ != 0) rebuild_table_(table_mask_ + 1, 0);
        CRUCIBLE_POST(0, signature_len.get() == 0u);
        CRUCIBLE_POST(0, ops_since_boundary.get() == 0u);
        CRUCIBLE_POST(0, boundaries_detected.get() == 0u);
        CRUCIBLE_POST(0, !confirmed);
        CRUCIBLE_POST(0, last_completed_len == 0u);
        CRUCIBLE_POST(0, table_used_ == 0u);
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
        refuted_[refuted_next_] = RefutedPeriod{.period = period,
                                                .body_sum = body_sum,
                                                .breaks = std::min(BREAKS_TO_REFUTE, breaks),
                                                .broke_since_restart = false};
        refuted_next_ = (refuted_next_ + 1) % REFUTED_CAPACITY;
        if (refuted_count_ < REFUTED_CAPACITY) ++refuted_count_;
    }

    // True when keys [left, left+length) equal keys [right, right+length).
    // The fingerprints reject almost every unequal pair in O(1). The key
    // compare makes the answer exact, and it runs O(length) only on a pair
    // that is almost surely equal.
    [[nodiscard]] bool windows_equal_(uint64_t left, uint64_t right, uint64_t length) const noexcept {
        const uint64_t power = power_of_base_(length);
        const uint64_t left_poly = prefix_poly_before_(left + length) - prefix_poly_before_(left) * power;
        const uint64_t right_poly = prefix_poly_before_(right + length) - prefix_poly_before_(right) * power;
        if (left_poly != right_poly) return false;
        for (uint64_t offset = 0; offset < length; ++offset) {
            if (keys_[(left + offset) & history_mask_] != keys_[(right + offset) & history_mask_]) return false;
        }
        return true;
    }

    // Is [start - 2P, start) two copies of one body of length P?
    [[nodiscard]] bool is_square_(uint64_t start, uint64_t period) const noexcept {
        return windows_equal_(start - 2 * period, start - period, period);
    }

    // Tests the earlier windows equal to the window at start, the nearest
    // first, so the candidate periods come in increasing order.
    [[nodiscard]] bool search_(uint64_t start, uint64_t earlier_link) {
        uint64_t link = earlier_link;
        for (uint32_t tested = 0; link != 0 && tested < MAX_CANDIDATES; ++tested) {
            const uint64_t earlier = link - 1;
            if (earlier < base_) break;
            const uint64_t period = start - earlier;
            if (start - base_ < 2 * period) break;
            if (period >= K && is_square_(start, period)) {
                const uint64_t body_sum = window_sum_(start - period, start);
                const auto length = static_cast<uint32_t>(period);
                if (!is_refuted(length, body_sum))
                    return accept_(start, ::fixy::mint_refined<kPeriodBound>(length), body_sum);
            }
            link = earlier_links_[earlier & history_mask_];
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
    // The windows of the body start in [start - P, start), and the newest op
    // is start + K - 1, so each one is complete.  The stream repeats with
    // period P over the history of the square, so each run of P window starts
    // holds the same windows.  A window whose link stays in the body is a
    // later copy of a window at an earlier phase.  For the first copy, the
    // links back over P window starts count its copies.
    //
    // Complexity: O(P), once per accepted period.
    [[nodiscard]] bool accept_(uint64_t start, Period period, uint64_t body_sum) {
        const uint64_t length = period.value();
        const uint64_t body_begin = start - length;
        uint32_t best_phase = 0;
        uint64_t best_count = ~uint64_t{0};
        for (uint32_t phase = 0; phase < length; ++phase) {
            const uint64_t window_start = body_begin + phase;
            uint64_t link = earlier_links_[window_start & history_mask_];
            if (link != 0 && link - 1 >= body_begin) continue;
            uint64_t copies = 1;
            while (link != 0 && link - 1 + length > window_start) {
                ++copies;
                link = earlier_links_[(link - 1) & history_mask_];
            }
            if (copies < best_count) {
                best_count = copies;
                best_phase = phase;
            }
        }

        period_ = period.value();
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
        // The search starts again, and the detector did not update the table
        // while the period held.  Each complete window of the history goes
        // into it again.
        rebuild_table_((history_mask_ + 1) * 2, pos_ + 1 - K);
        return false;
    }

    void mark_broke_since_restart_(uint32_t period, uint64_t body_sum) {
        for (uint32_t i = 0; i < refuted_count_; ++i) {
            if (refuted_[i].period == period && refuted_[i].body_sum == body_sum)
                refuted_[i].broke_since_restart = true;
        }
    }

    [[nodiscard]] bool fire_() {
        if (!current_period_reported_) {
            current_period_reported_ = true;
            ++periods_reported_;
        }
        last_completed_len = period_;
        // The K ops just received belong to the next iteration, so the
        // counter restarts at K.  This is a rewind from an arbitrary larger
        // value, under the quiescence of the one thread that owns the
        // detector.
        ops_since_boundary.reset_under_quiescence(K);
        boundaries_detected.bump();
        const uint64_t boundary = next_boundary_;
        next_boundary_ = boundary + period_;
        trim_(boundary - period_);
        return true;
    }

    // Drops the history before new_base.  The ring keeps the slots, and the
    // fingerprints of the kept ops stay valid, because a window fingerprint
    // only differences two prefixes.  Amortized O(1) per op: a period of P
    // ops drops P ops at each boundary.
    void trim_(uint64_t new_base) {
        if (new_base <= base_) return;
        forget_before_(new_base);
    }

    // The ring of the history: the key of the op at index p, the prefix
    // fingerprints that end with it, and the link of the window that starts
    // at it, each in slot p & history_mask_.
    ::foundation::AlignedBuffer<uint64_t> keys_;
    ::foundation::AlignedBuffer<uint64_t> prefix_polys_;
    ::foundation::AlignedBuffer<uint64_t> prefix_sums_;
    ::foundation::AlignedBuffer<uint64_t> earlier_links_;
    uint64_t history_mask_ = 0;

    // The window table: open addressing over window fingerprints, at a load
    // of at most one half.  The two words of a slot share one cache line, so
    // a probe costs one miss.  table_used_ counts the full slots.
    struct TableSlot {
        uint64_t start_plus_one = 0;
        uint64_t fingerprint = 0;
    };
    ::foundation::AlignedBuffer<TableSlot, sizeof(TableSlot)> table_;
    uint64_t table_mask_ = 0;
    uint64_t table_used_ = 0;
};

}  // namespace crucible

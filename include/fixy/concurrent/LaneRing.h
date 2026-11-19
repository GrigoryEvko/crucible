#pragma once

// A ring of many producers and one consumer: one SPSC lane for each producer,
// which the consumer reads in one fixed order.  The order of the merged stream
// is thus a function of the items of each lane, and not of the schedule.
//
// THE ORDER
//   Each lane is a ring of fixy/concurrent/SpscRing.h with one producer.  The
//   consumer takes one item of each lane in turn, in the order of the lane
//   indexes, and then starts again at the first lane of the turn.  While the
//   lane of the turn is empty and open, a pop gives no item, and the next pop
//   reads the same lane again.  A lane leaves the turn only after its producer
//   ended it and the consumer took its last item.  Thus:
//     - the items of one lane come in the order of their pushes, and
//     - after an item of lane k comes the next item of the next lane after k
//       that is still in the turn.
//   Two runs with the same items in each lane give the same merged stream, bit
//   for bit.  The schedule decides only when a pop gives no item, as it does
//   for one SPSC ring, so a pop takes no tag of Nondet.  In the net of a
//   program the ring is a fixed join of its lanes, and not a merge agent.
//
// THE STALL
//   The consumer waits for the lane of its turn while other lanes hold items.
//   So the slowest producer sets the pace of the stream, and a lane whose
//   producer neither pushes nor ends it stops the stream.  Each lane of the
//   type takes part in the turn from the start, so a lane that no producer
//   uses must be ended.
//
// THE END OF A LANE
//   The producer of a lane ends it after its last push (end_lane): a release
//   store of the end word of the lane, on a cache line of its own.  The
//   consumer reads that word with acquire order only after a pop that found
//   the lane empty, and then it pops the lane one more time.  That pop finds
//   each item that the producer pushed before its end.  A lane ends one time
//   in a stream.  When each lane ended and the consumer took each item, the
//   stream ended, and try_receive gives NoItem::Closed (fixy/concurrent/
//   ChannelEnd.h).  drain empties the lanes and opens each lane again for a
//   new stream.
//
// THE OWNER
//   A ring with an Owner lets Owner read the end of a lane and the end of the
//   stream, so the producer door of a channel refuses a lane that ended
//   (fixy/concurrent/PermissionedChannel.h).
//
// THE COSTS
//   A push is the push of one SPSC ring.  A pop is the pop of one SPSC ring
//   and a read of the turn, which only the consumer writes, on lines of their
//   own.  The end of a lane costs the consumer a walk of the turn, linear in
//   the lanes, one time for each lane.

#include <fixy/concurrent/ChannelEnd.h>
#include <fixy/concurrent/RingValue.h>
#include <fixy/concurrent/SpscRing.h>

#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Index.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

// The claims the carriers in this header make that no lattice grades.
// foundation/diag/RowHash.h folds each identity, so every carrier here
// takes a cache slot of its own rather than the zero a bare payload has.
namespace fixy::row_discipline {
struct lane_ring;
}  // namespace fixy::row_discipline

namespace fixy::concurrent {

// The count of the lanes of a ring of lanes.  It is an enum, so a count of
// lanes and a capacity cannot swap in a list of template arguments.
enum class LaneCount : std::size_t {
};

// A lane of a ring of Lanes lanes.
template <LaneCount Lanes>
using LaneIndex = ::foundation::core::Index<static_cast<std::size_t>(Lanes)>;

// The state of the end word of a lane (THE END OF A LANE).
enum class LaneEnd : std::uint8_t {
    open = 0,
    ended = 1,
};

namespace detail {

// One lane: its SPSC ring and its end word.  The producer of the lane writes
// the end word, and the consumer reads it after a pop that found the ring
// empty.  The class is outside the ring, so the array of the ring reads the
// default constructor of a complete class (foundation/core/Array.h).
template <RingValue T, std::size_t Capacity>
struct LaneOf {
    SpscRing<T, Capacity> ring{};
    ::foundation::core::CacheLine<::foundation::core::Atomic<LaneEnd>> end{LaneEnd::open};
};

}  // namespace detail

template <RingValue T, std::size_t LaneCapacity, LaneCount Lanes, class Owner = void>
class LaneRing : public ::foundation::Pinned<LaneRing<T, LaneCapacity, Lanes, Owner>> {
public:
    static constexpr std::size_t lane_count = static_cast<std::size_t>(Lanes);
    static constexpr std::size_t lane_capacity = LaneCapacity;

    static_assert(lane_count > 0, "fixy::concurrent::LaneRing: a ring of lanes has at least one lane.");
    static_assert(lane_count <= std::size_t{0xFFFFFFFF}, "fixy::concurrent::LaneRing: the turn keeps a lane in a word "
                                                         "of 32 bits.");
    static_assert(LaneCapacity <= ~std::size_t{0} / lane_count, "fixy::concurrent::LaneRing: the lanes hold more "
                                                                "items than a size_t counts.");

    using value_type = T;
    using lane_index = LaneIndex<Lanes>;
    using owner_type = Owner;
    static constexpr std::size_t channel_capacity = LaneCapacity * lane_count;
    using row_discipline = ::fixy::row_discipline::lane_ring;
    using row_payload = T;

    // Each lane is open and in the turn, and the turn starts at lane 0.
    LaneRing() noexcept { open_each_lane_(); }

    // The push of the one producer of LANE.  It gives false when the lane is
    // full.  A push from two threads into one lane corrupts the lane, and the
    // checked publish of the SPSC ring ends the process at the next publish.
    [[nodiscard, gnu::hot]] bool try_push(lane_index lane, T const& item) noexcept {
        return lanes_.at(lane).ring.try_push(item);
    }

    // The producer of LANE ends it after its last push (THE END OF A LANE).
    // A second end of one lane in a stream ends the process.
    void end_lane(lane_index lane) noexcept {
        ::foundation::core::Atomic<LaneEnd>& end = *lanes_.at(lane).end;
        CRUCIBLE_FATAL_INVARIANT(end.load_acquire() == LaneEnd::open);
        end.store_release(LaneEnd::ended);
    }

    // The next item of the merged stream (THE ORDER), NoItem::Empty while the
    // lane of the turn is empty and open, or NoItem::Closed after the last
    // item of the last lane.  Only the one consumer calls it.
    [[nodiscard, gnu::hot]] ::foundation::core::Result<T, NoItem> try_receive() noexcept {
        if (live_count_ != 0) [[likely]] {
            ::foundation::core::Option<T> item = lane_of_turn_().ring.try_pop();
            if (item.is_some()) [[likely]] {
                step_turn_();
                return static_cast<::foundation::core::Option<T>&&>(item).ok_or(NoItem::Empty);
            }
        }
        return receive_after_empty_();
    }

    // The next item of the merged stream, or no value when try_receive gives
    // no item.  It does not tell a wait from the end of the stream.
    [[nodiscard, gnu::hot]] ::foundation::core::Option<T> try_pop() noexcept { return try_receive().ok(); }

    // Gives each item that the lanes hold to drop, and then opens each lane
    // again for a new stream, with the turn at lane 0.  The items come in the
    // order of THE ORDER, with each empty lane taken as ended.  That order is
    // a function of the items of the lanes, so the drop can hold state.  The
    // caller holds the ring alone: no push and no pop runs at the same time.
    // Complexity: linear in the items, and quadratic in the lanes at most.
    template <typename Drop>
        requires std::is_nothrow_invocable_v<Drop&, T>
    void drain(Drop&& drop) noexcept {
        while (live_count_ != 0) {
            ::foundation::core::Option<T> item = lane_of_turn_().ring.try_pop();
            if (item.is_some()) {
                step_turn_();
                drop(static_cast<::foundation::core::Option<T>&&>(item).expect("the test above found an item"));
            } else {
                leave_turn_();
            }
        }
        open_each_lane_();
    }

    // Snapshots.  Each one reads the indexes of the producers, and the
    // schedule decides when the producers move them, so each one takes the
    // bare tag of Nondet.  Complexity: linear in the lanes.
    [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        for (detail::LaneOf<T, LaneCapacity> const& lane : lanes_) {
            if (!lane.ring.empty_approx(nondet)) return false;
        }
        return true;
    }

    [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        std::size_t count = 0;
        for (detail::LaneOf<T, LaneCapacity> const& lane : lanes_) {
            count += lane.ring.size_approx(nondet);
        }
        return count;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return channel_capacity; }

private:
    friend Owner;

    // The end word of LANE, with acquire order (THE OWNER).  The producer door
    // of a channel reads it after it takes the claim of the lane, so the read
    // comes after the end of the last producer of the lane.
    [[nodiscard]] LaneEnd end_of_lane_(lane_index lane) const noexcept { return lanes_.at(lane).end->load_acquire(); }

    // True when each lane ended, with acquire order (THE OWNER).  Complexity:
    // linear in the lanes.
    [[nodiscard]] bool is_each_lane_ended_() const noexcept {
        for (detail::LaneOf<T, LaneCapacity> const& lane : lanes_) {
            if (lane.end->load_acquire() == LaneEnd::open) return false;
        }
        return true;
    }

    // The lane at a position of the turn.  The turn holds only lanes below
    // lane_count, and each position below live_count_ holds one, so the Index
    // comes through the door of the base layer with no check.
    [[nodiscard, gnu::always_inline]] detail::LaneOf<T, LaneCapacity>& lane_of_turn_() noexcept {
        std::uint32_t const lane = live_.at(position_(turn_));
        return lanes_.at(lane_index{::foundation::core::detail::IndexDoor::key(), lane});
    }

    [[nodiscard, gnu::always_inline]] static lane_index position_(std::uint32_t position) noexcept {
        return lane_index{::foundation::core::detail::IndexDoor::key(), position};
    }

    // The turn moves to the next lane of the turn, and from the last lane to
    // the first.
    [[gnu::always_inline]] void step_turn_() noexcept {
        std::uint32_t const next = turn_ + 1;
        turn_ = next == live_count_ ? 0 : next;
    }

    // The lane of the turn ended and holds no item: it leaves the turn.  The
    // lanes after it keep their order, and the turn then names the next lane.
    // Complexity: linear in the lanes.
    CRUCIBLE_COLD void leave_turn_() noexcept {
        for (std::uint32_t position = turn_ + 1; position < live_count_; ++position) {
            live_.at(position_(position - 1)) = live_.at(position_(position));
        }
        --live_count_;
        if (turn_ == live_count_) turn_ = 0;
    }

    // The receive after a pop that found the lane of the turn empty (THE END
    // OF A LANE).  An open lane gives NoItem::Empty.  An ended lane gets one
    // more pop after the acquire read of its end, and an empty ended lane
    // leaves the turn.  Then the next lane of the turn gets its pop.
    [[nodiscard]] CRUCIBLE_COLD ::foundation::core::Result<T, NoItem> receive_after_empty_() noexcept {
        while (live_count_ != 0) {
            detail::LaneOf<T, LaneCapacity>& lane = lane_of_turn_();
            if (lane.end->load_acquire() == LaneEnd::open) return ::foundation::core::err(NoItem::Empty);
            ::foundation::core::Option<T> item = lane.ring.try_pop();
            if (item.is_none()) {
                leave_turn_();
                if (live_count_ == 0) break;
                item = lane_of_turn_().ring.try_pop();
            }
            if (item.is_some()) {
                step_turn_();
                return static_cast<::foundation::core::Option<T>&&>(item).ok_or(NoItem::Empty);
            }
        }
        return ::foundation::core::err(NoItem::Closed);
    }

    // Each lane is open and in the turn in the order of its index, and the
    // turn is at lane 0.
    void open_each_lane_() noexcept {
        for (lane_index const lane : ::foundation::core::IndexRange<lane_count>{}) {
            lanes_.at(lane).end->store_release(LaneEnd::open);
            live_.at(lane) = static_cast<std::uint32_t>(lane.raw());
        }
        live_count_ = static_cast<std::uint32_t>(lane_count);
        turn_ = 0;
    }

    // The lanes.  Each one holds the lines of its SPSC ring and the line of
    // its end word, so no two lanes share a line.
    ::foundation::core::FixedArray<detail::LaneOf<T, LaneCapacity>, lane_count> lanes_{};

    // The consumer is the one reader and the one writer of the turn: the lanes
    // of the turn in the order of their index, their count, and the position
    // of the next pop.  They start on a line of their own, so a push never
    // reads them.
    alignas(64) std::uint32_t turn_ = 0;
    std::uint32_t live_count_ = 0;
    ::foundation::core::FixedArray<std::uint32_t, lane_count> live_{};
};

}  // namespace fixy::concurrent

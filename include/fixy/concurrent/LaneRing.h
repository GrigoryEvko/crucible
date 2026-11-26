#pragma once

// A ring of many producers and one consumer: one SPSC lane for each producer,
// which the consumer reads in one fixed order.  The order of the merged stream
// is thus a function of the items of each lane, and not of the schedule.
//
// Order
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
// Stall
//   The consumer waits for the lane of its turn while other lanes hold items.
//   So the slowest producer sets the pace of the stream, and a lane whose
//   producer neither pushes nor ends it stops the stream.  Each lane of the
//   type takes part in the turn from the start, so a lane that no producer
//   uses must be ended.
//
// End of a lane
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
// Room
//   A pop takes one item of a lane and then one item of each other lane, so
//   a pop that published the tail of its lane at each item would move the
//   line of that tail between the two cores at each item of a full lane.  So
//   the consumer pops with the deferred tail of fixy/concurrent/SpscRing.h:
//   it publishes the tail of a lane at each tail_interval pops of that lane,
//   and it publishes the tail of each lane before each answer of
//   NoItem::Empty, and when a lane leaves the turn.  A consumer waits or
//   parks only after an Empty answer.  So a producer that waits for room in
//   its lane while the consumer waits for an item sees each cell that the
//   consumer freed, and no wait of the two holds the other.  The publish
//   decides only when a producer sees room, so the order does not change.
//
//   A consumer that wakes parked producers takes try_receive_freeing and
//   is_next_ready, which call it with the index of each lane after they publish
//   the tail of that lane.  No other step of the two frees a cell that a
//   producer can see.  A lane that leaves the turn ended, so no producer waits
//   for its room, and its publish calls nothing.
//
// Owner
//   Owner, such as the lanes channel of fixy/concurrent/PermissionedChannel.h,
//   mints the proofs of the ring (the writers).  Each thread can read the end
//   of a lane and the end of the stream, so the producer door of a channel
//   refuses a lane that ended.
//
// Writers
//   Each part of the ring has its writer in a type, so two threads can write
//   the ring at the same time, and the ring states synchronized{}
//   (foundation/Transfer.h).  The producer of a lane writes the producer side
//   of the SPSC ring of its lane: its head, its own copy of the head, its
//   cache of the tail and the cells that it fills.  Each push takes the
//   LaneProof of that lane, which only Owner mints, one time for each holder
//   of the lane, such as under the claim of the lane of a channel.  The SPSC
//   ring of the lane then takes the proof of its one producer (the proven form
//   of fixy/concurrent/SpscRing.h).  The end of a lane takes the same proof,
//   and the end word is atomic.  The consumer writes the consumer side of each
//   SPSC ring, the turn, the state of the tails and the lanes of the turn.
//   Each pop takes the ConsumerProof, which only Owner mints for its one
//   consumer.
//
// Costs
//   A push is the push of one SPSC ring.  A pop is the deferred pop of one
//   SPSC ring and a read of the turn, which only the consumer writes, on
//   lines of their own.  The first Empty answer after an item costs a walk
//   of the turn.  The end of a lane costs the consumer a walk of the turn,
//   linear in the lanes, one time for each lane.  A snapshot reads the
//   published tails, so between two Empty answers it counts as items at most
//   tail_interval - 1 deferred pops of each lane.

#include <fixy/concurrent/ChannelEnd.h>
#include <fixy/concurrent/RingValue.h>
#include <fixy/concurrent/SpscRing.h>

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/NoObject.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/Transfer.h>
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

// The state of the end word of a lane (the end of a lane).
enum class LaneEnd : std::uint8_t {
    open = 0,
    ended = 1,
};

// Whether a lane of a ring can hold deferred pops (the room).  It takes a
// word of 32 bits, so the line of the turn holds no padding.
enum class LaneTails : std::uint32_t {
    published = 0,
    deferred = 1,
};

namespace detail {

// One lane: its SPSC ring and its end word.  The producer of the lane writes
// the end word, and the consumer reads it after a pop that found the ring
// empty.  The class is outside the ring, so the array of the ring reads the
// default constructor of a complete class (foundation/core/Array.h).
template <RingValue T, std::size_t Capacity, class RingOwner>
struct LaneOf {
    using ring_type = SpscRing<T, Capacity, RingOwner>;
    ring_type ring{};
    ::foundation::core::CacheLine<::foundation::core::Atomic<LaneEnd>> end{LaneEnd::open};
};

}  // namespace detail

// The ring of lanes (the definition below).  LaneRingFrameKeyDoor names it as
// a friend, so this declaration stands before the door.
template <RingValue T, std::size_t LaneCapacity, LaneCount Lanes, class Owner>
class LaneRing;

}  // namespace fixy::concurrent

namespace foundation::core {

// The door of a ring of lanes to the key of the frame doors (the doors of the
// owners above foundation/core/Frame.h).  Its one member is private, and
// LaneRing of fixy/concurrent/LaneRing.h is its one friend.  The invariant:
// the one consumer of the ring reaches each lane of the run of the FixedArray
// of its lanes, and a producer reaches only the lane of its proof.  A producer
// writes only the SPSC ring and the end word of its lane, which are atomic.  A
// home owner row of utils/rule-table.txt keeps this definition in this header.
class LaneRingFrameKeyDoor final : ::foundation::NoObject<LaneRingFrameKeyDoor> {
    template <::fixy::concurrent::RingValue T, std::size_t LaneCapacity, ::fixy::concurrent::LaneCount Lanes,
              class Owner>
    friend class ::fixy::concurrent::LaneRing;

    // The key of the frame doors, for the run of the lanes of a ring.
    [[nodiscard]] CRUCIBLE_INLINE static constexpr detail::FrameKey key_() noexcept { return detail::FrameDoor::key(); }
};

}  // namespace foundation::core

namespace fixy::concurrent {

// A drop of the drain of a ring of lanes: it takes each item and does not
// throw.
template <class Drop, class T>
concept LaneDrainDrop = std::is_nothrow_invocable_v<Drop&, T>;

// A callable that the consumer gives for the lanes whose tail it publishes
// (the room): it takes the index of the lane and does not throw.
template <class Freed, class Index>
concept LaneFreedCall = std::is_nothrow_invocable_v<Freed&, Index>;

template <RingValue T, std::size_t LaneCapacity, LaneCount Lanes, class Owner>
class[[= ::foundation::transfer::synchronized{}]] LaneRing
    : public ::foundation::Pinned<LaneRing<T, LaneCapacity, Lanes, Owner>> {
    using lane_type = detail::LaneOf<T, LaneCapacity, LaneRing>;

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

    // The proof of the one producer of one lane (the writers).  Only Owner
    // mints one.  It moves with the holder of the lane, and it does not copy.
    class[[= ::foundation::linear::authority{}]][[= ::foundation::transfer::not_shareable{}]] LaneProof final {
        lane_index lane_{::foundation::core::index<0>()};

        // The seal refuses each byte route, such as std::bit_cast, so no bytes
        // become a proof.
        [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

        explicit constexpr LaneProof(lane_index lane) noexcept : lane_{lane} {}
        friend Owner;

    public:
        LaneProof(LaneProof const&) = delete("a lane has one producer, and a copy of its proof is a second");
        LaneProof& operator=(LaneProof const&) = delete("a lane has one producer, and a copy of its proof is a second");
        constexpr LaneProof(LaneProof&&) noexcept = default;
        LaneProof& operator=(LaneProof&&) = delete("the proof of a lane names one lane for its life");
        ~LaneProof() = default;

        // The lane of the proof.
        [[nodiscard]] constexpr lane_index lane() const noexcept { return lane_; }
    };

    // The proof of the one consumer (the writers).  Only Owner mints one, for
    // the call of its consumer, and it does not copy or move.
    class[[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]] ConsumerProof final {
        // The seal refuses each byte route, such as std::bit_cast, so no bytes
        // become a proof.
        [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

        constexpr ConsumerProof() noexcept {}
        friend Owner;

    public:
        ConsumerProof(ConsumerProof const&) = delete("a proof of the consumer lives for the call of its owner");
        ConsumerProof(ConsumerProof&&) = delete("a proof of the consumer lives for the call of its owner");
        ConsumerProof&
        operator=(ConsumerProof const&) = delete("a proof of the consumer lives for the call of its owner");
        ConsumerProof& operator=(ConsumerProof&&) = delete("a proof of the consumer lives for the call of its owner");
        ~ConsumerProof() = default;
    };
    using consumer_proof = ConsumerProof;
    static constexpr std::size_t channel_capacity = LaneCapacity * lane_count;
    using row_discipline = ::fixy::row_discipline::lane_ring;
    using row_payload = T;

    // The deferred pops of a lane between two publishes of its tail (the
    // room): a quarter of the lane, and at most 64 cells, eight lines of
    // cells of eight bytes.  A larger interval saves little, because the
    // lines of the cells then move more often than the line of the tail.
    static constexpr std::size_t tail_interval = LaneCapacity / 4 == 0 ? 1
                                               : LaneCapacity / 4 > 64 ? 64
                                                                       : LaneCapacity / 4;

    // Each lane is open and in the turn, and the turn starts at lane 0.
    LaneRing() noexcept { open_each_lane_(); }

    // The push of the one producer of the lane of the proof.  It gives false
    // when the lane is full.  The proof names the one producer, so the SPSC
    // ring of the lane takes the proven push.  The producers of two lanes call
    // it at the same time: each writes its own lane, and the SPSC ring of a
    // lane takes its one producer and the one consumer at once.
    [[nodiscard, gnu::hot]] bool try_push(LaneProof const& proof, T const& item) noexcept {
        return lane_of_(proof.lane()).ring.try_push_proven(prove_lane_producer_(), item);
    }

    // The producer of the lane of the proof ends it after its last push (the end
    // of a lane).  A second end of one lane in a stream ends the process.  The
    // end of a lane is an Atomic, which the consumer reads at the same time.
    void end_lane(LaneProof const& proof) noexcept {
        ::foundation::core::Atomic<LaneEnd>& end = *lane_of_(proof.lane()).end;
        CRUCIBLE_FATAL_INVARIANT(end.load_acquire() == LaneEnd::open);
        end.store_release(LaneEnd::ended);
    }

    // The next item of the merged stream (the order), NoItem::Empty while the
    // lane of the turn is empty and open, or NoItem::Closed after the last
    // item of the last lane.  The proof names the one consumer.
    [[nodiscard, gnu::hot]] ::foundation::core::Result<T, NoItem> try_receive(ConsumerProof const& proof) noexcept {
        return try_receive_freeing(proof, NoFreedCall{});
    }

    // As try_receive, and it calls freed with the index of each lane after it
    // published the tail of that lane (the room).  A door of a channel that
    // parks its producers wakes the producer of the lane there.  The call can
    // come before the result, and it reaches no state of the ring, so it can
    // make a system call.  The receive borrows the callable, so it holds no
    // linear value, and the door has no deleted twin, as the drain below.  The
    // index of the lane is a local of the door, so a callable that takes it
    // by reference stores no borrow outside the call (LentArmStoresNoBorrow
    // of foundation/core/Choice.h).
    template <class Freed>
        requires LaneFreedCall<Freed, lane_index> && ::foundation::linear::HoldsNoLinearValue<Freed>
              && ::foundation::core::LentArmStoresNoBorrow<Freed, lane_index>
    [[nodiscard, gnu::hot]] ::foundation::core::Result<T, NoItem> try_receive_freeing(ConsumerProof const& /*proof*/,
                                                                                      Freed&& freed) noexcept {
        if (!turn_.is_empty()) [[likely]] {
            lane_index const lane = lane_index_of_turn_();
            ::foundation::core::Option<T> item = pop_lane_of_turn_();
            if (item.is_some()) [[likely]] {
                step_turn_();
                call_if_published_(lane, freed);
                return static_cast<::foundation::core::Option<T>&&>(item).ok_or(NoItem::Empty);
            }
        }
        return receive_after_empty_(freed);
    }

    // True when the next receive gives an item or the end of the stream, and
    // false when it gives NoItem::Empty.  It takes no item.  Before a false
    // answer it publishes each tail and calls freed, as a receive does, so the
    // caller can park after it (the room).  A lane that ended and holds no
    // item leaves the turn here.  The proof names the one consumer.  freed
    // stores no borrow of the index of a lane outside the call, as in
    // try_receive_freeing.
    template <class Freed>
        requires LaneFreedCall<Freed, lane_index> && ::foundation::linear::HoldsNoLinearValue<Freed>
              && ::foundation::core::LentArmStoresNoBorrow<Freed, lane_index>
    [[nodiscard]] bool is_next_ready(ConsumerProof const& /*proof*/, Freed&& freed) noexcept {
        while (!turn_.is_empty()) {
            lane_type& lane = lane_of_turn_();
            if (lane.ring.has_item_proven(prove_lane_consumer_())) return true;
            if (lane.end->load_acquire() == LaneEnd::open) {
                if (tails_ == LaneTails::deferred) publish_each_tail_(freed);
                return false;
            }
            // The read of the end comes before this read, so it finds each
            // item that the producer pushed before its end.
            if (lane.ring.has_item_proven(prove_lane_consumer_())) return true;
            leave_turn_();
        }
        return true;
    }

    // The next item of the merged stream, or no value when try_receive gives
    // no item.  It does not tell a wait from the end of the stream.
    [[nodiscard, gnu::hot]] ::foundation::core::Option<T> try_pop(ConsumerProof const& proof) noexcept {
        return try_receive(proof).ok();
    }

    // Gives each item that the lanes hold to drop, and then opens each lane
    // again for a new stream, with the turn at lane 0.  The items come in the
    // order of the order above, with each empty lane taken as ended.  That order is
    // a function of the items of the lanes, so the drop can hold state.  Each
    // lane publishes its tail when it leaves the turn, so the drain ends with
    // no deferred pop.  The proof names the one consumer, and the caller holds
    // the ring alone: no push runs at the same time.  The drain borrows its
    // drop and calls it for each item, so the drop holds no linear value
    // (HoldsNoLinearValue of foundation/Linear.h).  Each item is a temporary
    // of the door, so a drop that takes it by reference stores no borrow
    // outside the call (LentArmStoresNoBorrow of foundation/core/Choice.h).
    // The drain has no deleted twin: the ring states synchronized{}, and the
    // door check of foundation/Transfer.h finds no specialization of a deleted
    // member template for its probe body, so it would refuse the ring.  A drop
    // that the gates refuse meets the failed constraint, which names the gate.
    // Complexity: linear in the items, and quadratic in the lanes at most.
    template <typename Drop>
        requires LaneDrainDrop<Drop, T> && ::foundation::linear::HoldsNoLinearValue<Drop>
              && ::foundation::core::LentArmStoresNoBorrow<Drop, T>
    void drain(ConsumerProof const& /*proof*/, Drop&& drop) noexcept {
        while (!turn_.is_empty()) {
            ::foundation::core::Option<T> item = pop_lane_of_turn_();
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
    // bare tag of Nondet.  Each one counts the deferred pops of a lane as
    // items (the costs).  Complexity: linear in the lanes.
    [[nodiscard]] bool empty_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        for (lane_type const& lane : lanes_) {
            if (!lane.ring.empty_approx(nondet)) return false;
        }
        return true;
    }

    [[nodiscard]] std::size_t size_approx(::foundation::effects::cap::Nondet nondet) const noexcept {
        std::size_t count = 0;
        for (lane_type const& lane : lanes_) {
            count += lane.ring.size_approx(nondet);
        }
        return count;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return channel_capacity; }

    // The end word of LANE, with acquire order (the owner).  The producer door
    // of a channel reads it after it takes the claim of the lane, so the read
    // comes after the end of the last producer of the lane.
    [[nodiscard]] LaneEnd end_of_lane(lane_index lane) const noexcept { return lanes_.at(lane).end->load_acquire(); }

    // True when each lane ended, with acquire order (the owner).  Complexity:
    // linear in the lanes.
    [[nodiscard]] bool is_each_lane_ended() const noexcept {
        for (lane_type const& lane : lanes_) {
            if (lane.end->load_acquire() == LaneEnd::open) return false;
        }
        return true;
    }

private:
    // The callable of a receive that wakes no producer.
    struct NoFreedCall final {
        constexpr void operator()(lane_index /*lane*/) const noexcept {}
    };

    // The lane of the turn.  The type of the turn and the type of each lane of
    // the turn prove them below lane_count, so the reads have no check.
    [[nodiscard, gnu::always_inline]] lane_type& lane_of_turn_() noexcept { return lane_at_(turn_.at()); }

    // The index of the lane of the turn.
    [[nodiscard, gnu::always_inline]] lane_index lane_index_of_turn_() const noexcept {
        return live_.at(turn_.at()).lane;
    }

    // Calls freed with the lane when the deferred pop before the call, which
    // took an item of the lane, published its tail (the room).
    template <class Freed>
    [[gnu::always_inline]] void call_if_published_(lane_index lane, Freed& freed) noexcept {
        if (!lane_of_(lane).ring.has_deferred_pops_proven(prove_lane_consumer_())) freed(lane);
    }

    // The lane at a position of the turn.  Only the one consumer calls it.
    [[nodiscard, gnu::always_inline]] lane_type& lane_at_(lane_index position) noexcept {
        return lane_of_(live_.at(position).lane);
    }

    // A lane of the ring.  The one consumer calls it for each lane, and a
    // producer for the lane of its proof.  The producers write only the SPSC
    // ring and the end of their lanes, which are atomic.  The ring reaches its
    // own lane through the keyed run of a frame, because a FixedArray gives no
    // other reference that writes an element.  The read of the run writes no
    // state of the array, so two threads can call it at the same time.
    [[nodiscard, gnu::always_inline]] lane_type& lane_of_(lane_index lane) noexcept {
        return frame_run_(::foundation::core::LaneRingFrameKeyDoor::key_(), lanes_).at(lane);
    }

    // The Index of a position of the turn.  Each caller compares the position
    // with the live count of the turn first, which is at most lane_count, so
    // the optimizer removes the check.
    [[nodiscard, gnu::always_inline]] static lane_index position_(std::size_t position) noexcept {
        return lane_index::check(position).expect("a position of the turn of a lane ring lies below its lanes");
    }

    // The proof of the one producer of the SPSC ring of a lane.  The ring
    // names this ring as its owner, so only this ring makes it, and only a push
    // that takes the proof of its lane calls it (the writers).
    [[nodiscard, gnu::always_inline]] static constexpr typename lane_type::ring_type::producer_proof
    prove_lane_producer_() noexcept {
        return typename lane_type::ring_type::producer_proof{};
    }

    // The proof of the one consumer of the SPSC ring of each lane.  Only this
    // ring makes it, and only the consumer side calls it: a door that takes
    // the proof of the consumer, and the cold paths of such a door (the
    // writers).
    [[nodiscard, gnu::always_inline]] static constexpr typename lane_type::ring_type::consumer_proof
    prove_lane_consumer_() noexcept {
        return typename lane_type::ring_type::consumer_proof{};
    }

    // The deferred pop of the lane of the turn (the room).
    [[nodiscard, gnu::always_inline]] ::foundation::core::Option<T> pop_lane_of_turn_() noexcept {
        return lane_of_turn_().ring.template try_pop_deferred_proven<tail_interval>(prove_lane_consumer_());
    }

    // The turn moves to the next lane of the turn, and from the last lane to
    // the first.  The type of the turn keeps its position below the count of
    // the turn, so the step has no check.  A pop of an item comes before each
    // step, so the step also records that a lane can hold deferred pops (the
    // room).
    [[gnu::always_inline]] void step_turn_() noexcept {
        turn_.step();
        tails_ = LaneTails::deferred;
    }

    // Publishes the tail of each lane of the turn that holds deferred pops, and
    // then calls freed with the lane (the room).  A lane out of the turn
    // published its tail when it left.  Complexity: linear in the lanes of the
    // turn.
    template <class Freed>
    CRUCIBLE_COLD void publish_each_tail_(Freed& freed) noexcept {
        for (std::size_t position = 0; position < turn_.live(); ++position) {
            lane_index const lane = live_.at(position_(position)).lane;
            if (!lane_of_(lane).ring.has_deferred_pops_proven(prove_lane_consumer_())) continue;
            lane_of_(lane).ring.publish_tail_proven(prove_lane_consumer_());
            freed(lane);
        }
        tails_ = LaneTails::published;
    }

    // The lane of the turn ended and holds no item: it publishes its tail and
    // leaves the turn.  The lanes after it keep their order, and the turn then
    // names the next lane.  Complexity: linear in the lanes.
    CRUCIBLE_COLD void leave_turn_() noexcept {
        lane_of_turn_().ring.publish_tail_proven(prove_lane_consumer_());
        for (std::size_t position = turn_.at().raw() + 1; position < turn_.live(); ++position) {
            TurnLane const next = live_.at(position_(position));
            live_.set(position_(position - 1), next);
        }
        turn_.drop();
    }

    // The receive after a pop that found the lane of the turn empty (the end
    // of a lane).  An open lane gives NoItem::Empty, after the publish of the
    // tail of each lane (the room).  An ended lane gets one more pop after the
    // acquire read of its end, and an empty ended lane leaves the turn.  Then
    // the next lane of the turn gets its pop.  The pops and the publishes call
    // freed as a receive does (the room).
    template <class Freed>
    [[nodiscard]] CRUCIBLE_COLD ::foundation::core::Result<T, NoItem> receive_after_empty_(Freed& freed) noexcept {
        while (!turn_.is_empty()) {
            lane_type& lane = lane_of_turn_();
            if (lane.end->load_acquire() == LaneEnd::open) {
                if (tails_ == LaneTails::deferred) publish_each_tail_(freed);
                return ::foundation::core::err(NoItem::Empty);
            }
            ::foundation::core::Option<T> item = pop_lane_of_turn_();
            if (item.is_none()) {
                leave_turn_();
                if (turn_.is_empty()) break;
                item = pop_lane_of_turn_();
            }
            if (item.is_some()) {
                lane_index const popped = lane_index_of_turn_();
                step_turn_();
                call_if_published_(popped, freed);
                return static_cast<::foundation::core::Option<T>&&>(item).ok_or(NoItem::Empty);
            }
        }
        return ::foundation::core::err(NoItem::Closed);
    }

    // Each lane is open and in the turn in the order of its index, and the
    // turn is at lane 0.  A new ring and a drained ring hold no deferred pop.
    void open_each_lane_() noexcept {
        for (lane_index const lane : ::foundation::core::IndexRange<lane_count>{}) {
            lane_of_(lane).end->store_release(LaneEnd::open);
            live_.set(lane, TurnLane{.lane = lane});
        }
        turn_ = ::foundation::core::CyclicIndex<lane_count>{};
        tails_ = LaneTails::published;
    }

    // A lane of the turn.  The type of its index proves it a lane of the ring.
    struct TurnLane final {
        lane_index lane{::foundation::core::index<0>()};
    };

    // The lanes.  Each one holds the lines of its SPSC ring and the line of
    // its end word, so no two lanes share a line.  The proof of a lane admits
    // each write of the producer side of its SPSC ring and of its end word, and
    // the proof of the consumer admits each write of the consumer side
    // (the writers).
    ::foundation::core::FixedArray<lane_type, lane_count> lanes_{};

    // The consumer is the one reader and the one writer of the turn: the lanes
    // of the turn in the order of their index, their count with the position
    // of the next pop, and whether a lane can hold deferred pops (the room).
    // The proof of the consumer admits each write of them.  They start on a
    // line of their own, so a push never reads them.
    alignas(64)::foundation::core::CyclicIndex<lane_count> turn_{};
    LaneTails tails_ = LaneTails::published;
    ::foundation::core::FixedArray<TurnLane, lane_count> live_{};
};

}  // namespace fixy::concurrent

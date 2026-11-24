// The parked form of a wire of Box objects (fixy/concurrent/ParkedChannel.h
// over fixy/concurrent/PermissionedBoxChannel.h).  A push takes the Box by
// move, and a refused push gives the same Box back in a PushRefused, with the
// reason: Full for a full ring and its deadline, ConsumerGone after the close
// of the consumer.  The count of the live parcels shows that no object leaks
// and none ends two times.  The waits and their notifies are those of the
// wire of values (test_parked_channel), and this test shows the Box form of
// each push door.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/ParkedChannel.h>
#include <fixy/concurrent/PermissionedBoxChannel.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/os/ThreadTasks.h>
#include <fixy/os/Time.h>
#include <foundation/Linear.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include "../foundation/philox_stream.h"

#include <cstdint>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
namespace cc = ::fixy::concurrent;
namespace perm = ::foundation::permissions;

using ParkAtOnce = cc::SpinThenPark<0>;

struct ParcelTag {};

constexpr std::uint64_t kParcels = 2000;
// The consumer waits for a parked producer before each parcel of this
// stride.
constexpr std::uint64_t kParkStride = 64;
// The turns of the pause after a thread sees a waiter in a count.
constexpr std::uint64_t kSettleTurns = 20000;
// The bound of one timed push.
constexpr std::uint64_t kTimedStepNanos = 1000000;

// The count of the live parcels.  Each object must end one time, and no
// object may leak.
fixy::Atomic<std::int64_t> live_parcels{0};

class Parcel {
public:
    explicit Parcel(std::uint64_t label) noexcept : label_{label} {
        static_cast<void>(live_parcels.fetch_add_acq_rel(1));
    }
    Parcel(Parcel const&) = delete("a parcel ends one time, so it has one owner");
    Parcel& operator=(Parcel const&) = delete("a parcel ends one time, so it has one owner");
    ~Parcel() { static_cast<void>(live_parcels.fetch_sub_acq_rel(1)); }

    [[nodiscard]] std::uint64_t label() const noexcept { return label_; }

private:
    std::uint64_t label_ = 0;
};

using ParcelBox = fixy::Box<Parcel>;
using Refused = cc::PushRefused<ParcelBox>;
using BoxPush = fixy::Result<fixy::Unit, Refused>;

[[nodiscard]] ParcelBox make_parcel(std::uint64_t label) noexcept {
    return fixy::mint_box<Parcel>(eff::testing::test().alloc, label);
}

[[nodiscard]] eff::cap::Nondet nondet_of(::fixy::BgLoadCtx const& ctx) noexcept {
    return eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
}

// The pause after a thread sees a waiter in a count, so the waiter goes from
// its last test of the condition to its park.
void settle() noexcept {
    for (std::uint64_t turn = 0; turn < kSettleTurns; ++turn) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// A delay of the consumer: short for most parcels and long for some, so the
// producer waits for room.
void delay(::crucible::test::PhiloxStream& delays) noexcept {
    std::uint64_t const draw = delays.below(100);
    std::uint64_t const turns = draw < 90 ? delays.below(200) : 2000 + delays.below(10000);
    for (std::uint64_t turn = 0; turn < turns; ++turn) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// The refusal of a push, or no value when the ring took the Box.
[[nodiscard]] fixy::Option<Refused> refusal_of(BoxPush&& pushed) noexcept { return std::move(pushed).err(); }

// The label of the parcel of a Box.  The Box can be a temporary, which lives
// until the end of the full expression of the call.
[[nodiscard]] std::uint64_t label_of(ParcelBox const& box) noexcept { return box->label(); }

// True when the ring took the Box.  A refusal ends its Box here.
[[nodiscard]] bool is_pushed(BoxPush&& pushed) noexcept {
    return std::move(pushed).match([](fixy::Unit) noexcept { return true; },
                                   [](Refused refused) noexcept {
                                       ::foundation::linear::drop(std::move(refused));
                                       return false;
                                   });
}

// The Box of a refusal with the reason why, or the end of the process with
// the reason of the caller when the ring took the Box or the reason differs.
[[nodiscard]] ParcelBox box_of_refusal(BoxPush&& pushed, cc::PushRefusal why) noexcept {
    Refused refused = refusal_of(std::move(pushed)).expect("the push gives its Box back");
    CRUCIBLE_FATAL_INVARIANT(refused.reason() == why);
    return std::move(refused).take_item();
}

// A full ring of 2 cells refuses the third Box with Full and gives the same
// Box back.  After one pop, the push of that Box goes in.  The consumer gets
// each parcel in order, and no parcel stays alive after the frame.
void expect_a_full_ring_gives_the_box_back(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, ParcelTag>>();
    using Parked = cc::ParkedChannel<cc::box_channel_t<Parcel, 2, decltype(root)>>;
    static_assert(Parked::moves_its_items);
    {
        Parked channel{};
        auto [producer_perm, consumer_perm] =
            perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
        cc::with_ends(
            channel, std::move(producer_perm), std::move(consumer_perm), [&ctx](auto producer, auto consumer) noexcept {
                CRUCIBLE_FATAL_INVARIANT(is_pushed(producer.try_push(ctx, make_parcel(0))));
                CRUCIBLE_FATAL_INVARIANT(is_pushed(producer.try_push(ctx, make_parcel(1))));
                ParcelBox kept = box_of_refusal(producer.try_push(ctx, make_parcel(2)), cc::PushRefusal::Full);
                CRUCIBLE_FATAL_INVARIANT(kept->label() == 2);
                CRUCIBLE_FATAL_INVARIANT(label_of(consumer.try_pop(ctx).expect("the ring is full")) == 0);
                CRUCIBLE_FATAL_INVARIANT(is_pushed(producer.try_push(ctx, std::move(kept))));
                std::move(producer).close(ctx);
                CRUCIBLE_FATAL_INVARIANT(label_of(consumer.try_pop(ctx).expect("the ring holds two parcels")) == 1);
                CRUCIBLE_FATAL_INVARIANT(label_of(consumer.try_pop(ctx).expect("the ring holds one parcel")) == 2);
                std::move(consumer).close(ctx);
            });
    }
    CRUCIBLE_FATAL_INVARIANT(live_parcels.load_acquire() == 0);
}

// The timed push gives the Box back with Full at its deadline.
void expect_the_timed_push_gives_the_box_back(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, ParcelTag>>();
    using Parked = cc::ParkedChannel<cc::box_channel_t<Parcel, 2, decltype(root)>>;
    {
        Parked channel{};
        auto [producer_perm, consumer_perm] =
            perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
        cc::with_ends(
            channel, std::move(producer_perm), std::move(consumer_perm), [&ctx](auto producer, auto consumer) noexcept {
                auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(ctx);
                CRUCIBLE_FATAL_INVARIANT(is_pushed(producer.try_push(ctx, make_parcel(0))));
                CRUCIBLE_FATAL_INVARIANT(is_pushed(producer.try_push(ctx, make_parcel(1))));
                ::fixy::futex::Deadline const soon =
                    ::fixy::futex::Deadline::after(clock, nondet_of(ctx), kTimedStepNanos).expect("the clock reads");
                ParcelBox kept = box_of_refusal(
                    producer.template push_waiting_until<ParkAtOnce>(ctx, make_parcel(2), soon), cc::PushRefusal::Full);
                CRUCIBLE_FATAL_INVARIANT(kept->label() == 2);
                CRUCIBLE_FATAL_INVARIANT(label_of(consumer.try_pop(ctx).expect("the ring is full")) == 0);
                ::fixy::futex::Deadline const later =
                    ::fixy::futex::Deadline::after(clock, nondet_of(ctx), kTimedStepNanos).expect("the clock reads");
                CRUCIBLE_FATAL_INVARIANT(
                    is_pushed(producer.template push_waiting_until<ParkAtOnce>(ctx, std::move(kept), later)));
                std::move(producer).close(ctx);
                std::move(consumer).close(ctx);
            });
    }
    CRUCIBLE_FATAL_INVARIANT(live_parcels.load_acquire() == 0);
}

// A producer that parks on a full ring with push_waiting ends with
// ConsumerGone when the consumer closes, and it gets its Box back.  The
// consumer closes only after it sees the producer park.
void expect_a_consumer_close_gives_the_box_back(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, ParcelTag>>();
    using Parked = cc::ParkedChannel<cc::box_channel_t<Parcel, 2, decltype(root)>>;
    {
        Parked channel{};
        auto [producer_perm, consumer_perm] =
            perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
        cc::with_ends(
            channel, std::move(producer_perm), std::move(consumer_perm),
            [&ctx, &channel](auto producer, auto consumer) noexcept {
                auto fill_then_wait = [handle = std::move(producer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                    CRUCIBLE_FATAL_INVARIANT(is_pushed(handle.try_push(bg, make_parcel(0))));
                    CRUCIBLE_FATAL_INVARIANT(is_pushed(handle.try_push(bg, make_parcel(1))));
                    ParcelBox kept = box_of_refusal(handle.template push_waiting<ParkAtOnce>(bg, make_parcel(2)),
                                                    cc::PushRefusal::ConsumerGone);
                    std::move(handle).close(bg);
                    return kept->label();
                };
                auto values =
                    ::fixy::thread::run_beside(ctx, ::fixy::thread::ThreadOptions{}, std::move(fill_then_wait),
                                               [&ctx, &channel, owned = std::move(consumer)]() mutable noexcept {
                                                   eff::cap::Nondet const nondet = nondet_of(ctx);
                                                   while (!channel.has_parked_producer(nondet)) {
                                                       CRUCIBLE_SPIN_PAUSE;
                                                   }
                                                   settle();
                                                   std::move(owned).close(ctx);
                                               })
                        .expect("the thread door starts a producer");
                CRUCIBLE_FATAL_INVARIANT(values.at(::fixy::index<0>()) == 2);
            });
    }
    CRUCIBLE_FATAL_INVARIANT(live_parcels.load_acquire() == 0);
}

// A wire of 4 cells with a slow consumer: the producer pushes each Box with
// push_waiting, and before each stride parcel the consumer waits until the
// producer parks, so its receive must wake it.  Each parcel comes in order,
// then the end, and no parcel stays alive.
void expect_each_box_through_a_full_wire(::fixy::BgLoadCtx const& ctx) {
    auto root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, ParcelTag>>();
    using Parked = cc::ParkedChannel<cc::box_channel_t<Parcel, 4, decltype(root)>>;
    {
        Parked channel{};
        auto [producer_perm, consumer_perm] =
            perm::mint_permission_split<Parked::producer_tag, Parked::consumer_tag>(std::move(root));
        cc::with_ends(
            channel, std::move(producer_perm), std::move(consumer_perm),
            [&ctx, &channel](auto producer, auto consumer) noexcept {
                auto push_each = [handle = std::move(producer)](::fixy::BgLoadCtx const& bg) mutable noexcept {
                    for (std::uint64_t label = 0; label < kParcels; ++label) {
                        CRUCIBLE_FATAL_INVARIANT(
                            is_pushed(handle.template push_waiting<ParkAtOnce>(bg, make_parcel(label))));
                    }
                    std::move(handle).close(bg);
                };
                auto values =
                    ::fixy::thread::run_beside(
                        ctx, ::fixy::thread::ThreadOptions{}, std::move(push_each),
                        [&ctx, &channel, owned = std::move(consumer)]() mutable noexcept {
                            ::crucible::test::PhiloxStream delays{61};
                            eff::cap::Nondet const nondet = nondet_of(ctx);
                            std::uint64_t count = 0;
                            bool is_ordered = true;
                            for (;;) {
                                if (count % kParkStride == 0 && count + Parked::channel_capacity < kParcels) {
                                    while (!channel.has_parked_producer(nondet)) {
                                        CRUCIBLE_SPIN_PAUSE;
                                    }
                                    settle();
                                }
                                fixy::Result<ParcelBox, cc::NoItem> received =
                                    owned.template receive_waiting<ParkAtOnce>(ctx);
                                if (received.is_err()) break;
                                is_ordered =
                                    is_ordered
                                    && label_of(std::move(received).expect("the test above found a parcel")) == count;
                                ++count;
                                delay(delays);
                            }
                            std::move(owned).close(ctx);
                            return is_ordered && count == kParcels;
                        })
                        .expect("the thread door starts a producer");
                CRUCIBLE_FATAL_INVARIANT(values.at(::fixy::index<1>()));
            });
    }
    CRUCIBLE_FATAL_INVARIANT(live_parcels.load_acquire() == 0);
}

}  // namespace

int main() {
    ::fixy::BgLoadCtx const ctx{eff::testing::bg()};
    expect_a_full_ring_gives_the_box_back(ctx);
    expect_the_timed_push_gives_the_box_back(ctx);
    expect_a_consumer_close_gives_the_box_back(ctx);
    expect_each_box_through_a_full_wire(ctx);
    return 0;
}

// A pipeline over parked channels (fixy/concurrent/ParkedChannel.h).  Each
// stage names the context of its stage (fixy/concurrent/StageShape.h) and
// gives it to each door of its parked handles, so no stage holds a context of
// its own.  The channels hold 2 items and each wait parks at once, so each
// stage parks on a full output and on an empty input many times.  Each item
// comes out in order, and no boxed item leaks.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/HandleTraits.h>
#include <fixy/concurrent/ParkedChannel.h>
#include <fixy/concurrent/PermissionedBoxChannel.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/Stage.h>
#include <fixy/os/Futex.h>
#include <fixy/os/Time.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include "../foundation/philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace cc = ::fixy::concurrent;
namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;

using ParkAtOnce = cc::SpinThenPark<0>;

struct StartTag {};
struct ParcelTag {};
struct SquareTag {};

constexpr std::uint64_t kParcels = 2000;
// The bound of one timed wait of the last stage.
constexpr std::uint64_t kWaitStepNanos = 200000;

struct Go {};

// The count of the live parcels.  Each parcel ends one time, and none leaks.
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

// The label of the parcel of a Box.  The Box can be a temporary, which lives
// until the end of the full expression of the call.
[[nodiscard]] std::uint64_t label_of(ParcelBox const& box) noexcept {
    return ::foundation::core::with_view(box, [](Parcel const& parcel) noexcept { return parcel.label(); });
}

// What the last stage found: the count of the items, and whether each item
// came in its order.
struct Summary {
    std::uint64_t items = 0;
    bool is_ordered = true;
};

// The sink takes the summary of the last stage.  Brand is the brand of the
// cell that the sink writes.
template <class Brand>
struct SummarySink {
    fixy::Ref<fixy::Atomic<std::uint64_t>, Brand> items;

    [[nodiscard]] bool try_push(Summary const& summary) noexcept {
        items->store_release(summary.is_ordered ? summary.items : 0);
        return true;
    }
};

void pause_for(std::uint64_t turns) noexcept {
    for (std::uint64_t turn = 0; turn < turns; ++turn) {
        CRUCIBLE_SPIN_PAUSE;
    }
}

// Waits for the start signal, pushes each parcel in order, and closes its two
// handles.
template <class Input, class Output>
void push_parcels(Input&& in, Output&& out, ::fixy::BgLoadCtx const& ctx) noexcept {
    CRUCIBLE_FATAL_INVARIANT(in.template receive_waiting<ParkAtOnce>(ctx).is_ok());
    for (std::uint64_t label = 0; label < kParcels; ++label) {
        out.template push_waiting<ParkAtOnce>(ctx, fixy::mint_box<Parcel>(ctx.cap().alloc, label))
            .match([](fixy::Unit) noexcept {},
                   [](cc::PushRefused<ParcelBox>) noexcept {
                       fixy::fatal("the squaring stage closed its input before the end of the stream");
                   });
    }
    std::move(out).close(ctx);
    std::move(in).close(ctx);
}

// Squares the label of each parcel, with a delay of a varied length, so the
// two channels of the stage are full and empty in turn.
template <class Input, class Output>
void square_labels(Input&& in, Output&& out, ::fixy::BgLoadCtx const& ctx) noexcept {
    ::crucible::test::PhiloxStream delays{73};
    for (;;) {
        fixy::Result<ParcelBox, cc::NoItem> received = in.template receive_waiting<ParkAtOnce>(ctx);
        if (received.is_err()) break;
        std::uint64_t const label = label_of(std::move(received).expect("the test above found a parcel"));
        CRUCIBLE_FATAL_INVARIANT(out.template push_waiting<ParkAtOnce>(ctx, label * label).is_ok());
        pause_for(delays.below(100) < 95 ? delays.below(50) : 2000 + delays.below(4000));
    }
    std::move(out).close(ctx);
    std::move(in).close(ctx);
}

// Takes each square with a poll and a timed wait that takes no item.
template <class Input, class Sink>
void collect_squares(Input&& in, Sink&& out, ::fixy::BgLoadCtx const& ctx) noexcept {
    auto const clock = ::fixy::time::mint_clock_reader<::fixy::ClockSource_v::Monotonic>(ctx);
    eff::cap::Nondet const nondet = eff::extract_bare_from_ctx<eff::Effect::Nondet>(ctx);
    ::crucible::test::PhiloxStream delays{79};
    Summary summary{};
    for (;;) {
        fixy::Result<std::uint64_t, cc::NoItem> received = in.try_receive(ctx);
        if (received.is_err()) {
            if (received.err().value_or(cc::NoItem::Empty) == cc::NoItem::Closed) break;
            ::fixy::futex::Deadline const deadline =
                ::fixy::futex::Deadline::after(clock, nondet, kWaitStepNanos).expect("the clock gives a deadline");
            static_cast<void>(in.template wait_for_item_until<ParkAtOnce>(ctx, deadline));
            continue;
        }
        std::uint64_t const square = std::move(received).expect("the test above found a square");
        summary.is_ordered = summary.is_ordered && square == summary.items * summary.items;
        ++summary.items;
        pause_for(delays.below(100) < 90 ? delays.below(50) : delays.below(3000));
    }
    CRUCIBLE_FATAL_INVARIANT(out.try_push(summary));
    std::move(in).close(ctx);
}

// Builds the three channels and the three stages, and runs the pipeline.
void run_parked_pipeline(::fixy::BgLoadCtx const& ctx, fixy::Atomic<std::uint64_t>& items) noexcept {
    auto start_root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, StartTag>>();
    auto parcel_root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, ParcelTag>>();
    auto square_root = perm::mint_permission_root<cc::channel_tag::Whole<cc::ChannelForm::wire, SquareTag>>();
    using Start = cc::ParkedChannel<cc::channel_t<Go, 1, decltype(start_root)>>;
    using Parcels = cc::ParkedChannel<cc::box_channel_t<Parcel, 2, decltype(parcel_root)>>;
    using Squares = cc::ParkedChannel<cc::channel_t<std::uint64_t, 2, decltype(square_root)>>;

    Start start{};
    Parcels parcels{};
    Squares squares{};
    auto [start_out, start_in] =
        perm::mint_permission_split<Start::producer_tag, Start::consumer_tag>(std::move(start_root));
    auto [parcel_out, parcel_in] =
        perm::mint_permission_split<Parcels::producer_tag, Parcels::consumer_tag>(std::move(parcel_root));
    auto [square_out, square_in] =
        perm::mint_permission_split<Squares::producer_tag, Squares::consumer_tag>(std::move(square_root));

    auto const run_stages = [&ctx, &items](auto go, auto start_end, auto to_squares, auto from_source, auto to_sink,
                                           auto from_squares) noexcept {
        // A parked handle is a pole of the context form and of no plain form.
        static_assert(cc::is_ctx_producer_handle_v<decltype(to_squares)>
                      && !cc::is_producer_handle_v<decltype(to_squares)>);
        static_assert(cc::is_ctx_consumer_handle_v<decltype(from_source)>
                      && !cc::is_consumer_handle_v<decltype(from_source)>);
        static_assert(std::is_same_v<cc::stage_output_handle_value_t<decltype(to_squares)>, ParcelBox>);
        static_assert(std::is_same_v<cc::stage_input_handle_value_t<decltype(from_squares)>, std::uint64_t>);
        // The two ends of one channel name it, and report one identity.
        CRUCIBLE_FATAL_INVARIANT(to_squares.channel_identity() == from_source.channel_identity());
        CRUCIBLE_FATAL_INVARIANT(to_squares.channel_identity().is_bound());

        go.try_push(ctx, Go{}).match(
            [](fixy::Unit) noexcept {},
            [](cc::PushRefusal) noexcept { fixy::fatal("the start channel holds the start signal of one run"); });
        std::move(go).close(ctx);

        using StartIn = decltype(start_end);
        using ParcelOut = decltype(to_squares);
        using ParcelIn = decltype(from_source);
        using SquareOut = decltype(to_sink);
        using SquareIn = decltype(from_squares);
        // The body names the context of its stage, so another context refuses
        // the stage.
        static_assert(cc::CtxFitsStage<&push_parcels<StartIn, ParcelOut>, ::fixy::BgLoadCtx>);
        static_assert(!cc::CtxFitsStage<&push_parcels<StartIn, ParcelOut>, ::fixy::BgDrainCtx>);
        auto source =
            cc::mint_stage<&push_parcels<StartIn, ParcelOut>>(ctx, std::move(start_end), std::move(to_squares));
        auto squarer =
            cc::mint_stage<&square_labels<ParcelIn, SquareOut>>(ctx, std::move(from_source), std::move(to_sink));
        auto sink = SummarySink{fixy::to(items)};
        auto collector =
            cc::mint_stage<&collect_squares<SquareIn, decltype(sink)>>(ctx, std::move(from_squares), std::move(sink));
        cc::mint_pipeline(ctx, std::move(source), std::move(squarer), std::move(collector)).run(ctx);
    };

    cc::with_ends(
        start, std::move(start_out), std::move(start_in),
        [&, parcel_out = std::move(parcel_out), parcel_in = std::move(parcel_in), square_out = std::move(square_out),
         square_in = std::move(square_in)](auto go, auto start_end) mutable noexcept {
            cc::with_ends(parcels, std::move(parcel_out), std::move(parcel_in),
                          [&, go = std::move(go), start_end = std::move(start_end), square_out = std::move(square_out),
                           square_in = std::move(square_in)](auto to_squares, auto from_source) mutable noexcept {
                              cc::with_ends(squares, std::move(square_out), std::move(square_in),
                                            [&, go = std::move(go), start_end = std::move(start_end),
                                             to_squares = std::move(to_squares), from_source = std::move(from_source)](
                                                auto to_sink, auto from_squares) mutable noexcept {
                                                run_stages(std::move(go), std::move(start_end), std::move(to_squares),
                                                           std::move(from_source), std::move(to_sink),
                                                           std::move(from_squares));
                                            });
                          });
        });
}

}  // namespace

int main() {
    ::fixy::BgLoadCtx const ctx{eff::testing::bg()};
    fixy::Atomic<std::uint64_t> items{0};
    run_parked_pipeline(ctx, items);
    CRUCIBLE_FATAL_INVARIANT(items.load_acquire() == kParcels);
    CRUCIBLE_FATAL_INVARIANT(live_parcels.load_acquire() == 0);
    return 0;
}

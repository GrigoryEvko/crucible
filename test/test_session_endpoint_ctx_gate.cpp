// Each endpoint session mint states its context gate in its own clause, so a
// caller can ask whether a context fits before it calls, and a refusal names
// the mint.  The gate is the one mint_permissioned_session enforces: the
// context must admit the protocol with no permission in hand.
//
// This test asks that question of every endpoint mint whose protocol carries
// the channel payload.  The payload is a computation with a background row, so
// a background context fits and the foreground context does not.  A plain
// payload fits the foreground context, which shows that the gate refuses the
// row and not the mint.
//
// main() mints a session over a background-row channel with a background
// context and moves one value through it.

#include <crucible/concurrent/_PermissionedCalendarGrid.h>
#include <crucible/concurrent/_PermissionedChaseLevDeque.h>
#include <crucible/concurrent/_PermissionedMpmcChannel.h>
#include <crucible/concurrent/_PermissionedShardedCalendarGrid.h>
#include <crucible/concurrent/_PermissionedShardedGrid.h>
#include <crucible/concurrent/_PermissionedSnapshot.h>
#include <crucible/concurrent/_PermissionedSpscChannel.h>
#include <crucible/effects/_Computation.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/_AsyncPipelineSession.h>
#include <crucible/sessions/_CalendarGridSession.h>
#include <crucible/sessions/_ChaseLevDequeSession.h>
#include <crucible/sessions/_MpmcChannelSession.h>
#include <crucible/sessions/_ShardedCalendarGridSession.h>
#include <crucible/sessions/_ShardedGridSession.h>
#include <crucible/sessions/_SnapshotSession.h>
#include <crucible/sessions/_SpscSession.h>
#include <crucible/sessions/_SwmrSession.h>

#include <cstdint>
#include <cstdio>
#include <utility>

namespace endpoint_ctx_gate {

namespace eff = ::crucible::effects;
namespace conc = ::crucible::concurrent;
namespace proto = ::crucible::safety::proto;

using Fg = eff::HotFgCtx;
using Bg = eff::BgDrainCtx;

using BgInt = eff::Computation<eff::Row<eff::Effect::Bg>, int>;

struct SpscTag {};
struct MpmcTag {};
struct SnapTag {};
struct SwmrWriterTag {};
struct SwmrReaderTag {};
struct DequeTag {};
struct GridTag {};
struct CalendarTag {};
struct ShardedCalendarTag {};

struct BgIntKey {
    static std::uint64_t key(BgInt const&) noexcept { return 0; }
};

using Spsc = conc::PermissionedSpscChannel<BgInt, 64, SpscTag>;
using Mpmc = conc::PermissionedMpmcChannel<BgInt, 64, MpmcTag>;
using Snap = conc::PermissionedSnapshot<BgInt, SnapTag>;
using Swmr = proto::swmr_session::SwmrSession<BgInt, SwmrWriterTag, SwmrReaderTag>;
using Deque = conc::PermissionedChaseLevDeque<BgInt, 64, DequeTag>;
using Grid = conc::PermissionedShardedGrid<BgInt, 2, 2, 16, GridTag>;
using Calendar = conc::PermissionedCalendarGrid<BgInt, 2, 8, 16, BgIntKey, 1000000ULL, CalendarTag>;
using ShardedCalendar = conc::PermissionedShardedCalendarGrid<BgInt, 2, 8, 16, BgIntKey, 1000000ULL, ShardedCalendarTag>;

using PlainSpsc = conc::PermissionedSpscChannel<int, 64, SpscTag>;

namespace spsc = proto::spsc_session;
namespace mpmc = proto::mpmc_channel_session;
namespace snapshot = proto::snapshot_session;
namespace swmr = proto::swmr_session;
namespace chaselev = proto::chaselev_session;
namespace sharded = proto::sharded_grid_session;
namespace calendar = proto::calendar_grid_session;
namespace sharded_calendar = proto::sharded_calendar_grid_session;

// ── One question per mint: does a context of type Ctx fit it? ──
//
// Each question is a concept, so a refused call is a substitution failure and
// the answer is false.  At namespace scope a refused call would be an error.

template <class Ctx, class C>
concept fits_spsc_producer = requires(Ctx const& ctx, typename C::ProducerHandle& h) {
    spsc::mint_producer_session<C>(ctx, h);
};
template <class Ctx, class C>
concept fits_spsc_consumer = requires(Ctx const& ctx, typename C::ConsumerHandle& h) {
    spsc::mint_consumer_session<C>(ctx, h);
};
template <class Ctx, class C>
concept fits_mpmc_producer = requires(Ctx const& ctx, typename C::ProducerHandle& h) {
    mpmc::mint_mpmc_producer_session<C>(ctx, h);
};
template <class Ctx, class C>
concept fits_mpmc_consumer = requires(Ctx const& ctx, typename C::ConsumerHandle& h) {
    mpmc::mint_mpmc_consumer_session<C>(ctx, h);
};
template <class Ctx, class C>
concept fits_snapshot_writer = requires(Ctx const& ctx, typename C::WriterHandle& h) {
    snapshot::mint_snapshot_writer_session<C>(ctx, h);
};
template <class Ctx, class C>
concept fits_snapshot_reader = requires(Ctx const& ctx, typename C::ReaderHandle& h) {
    snapshot::mint_snapshot_reader_session<C>(ctx, h);
};
template <class Ctx, class C>
concept fits_swmr_writer = requires(Ctx const& ctx, typename C::WriterHandle& h) {
    swmr::mint_writer_session<C>(ctx, h);
};
template <class Ctx, class C>
concept fits_swmr_reader = requires(Ctx const& ctx, typename C::ReaderHandle& h) {
    swmr::mint_reader_session<C>(ctx, h);
};
template <class Ctx, class C>
concept fits_swmr_runtime_writer = requires(Ctx const& ctx, typename C::WriterHandle& h) {
    swmr::mint_writer_runtime_session<C>(ctx, h);
};
template <class Ctx, class C>
concept fits_swmr_runtime_reader = requires(Ctx const& ctx, typename C::ReaderHandle& h) {
    swmr::mint_reader_runtime_session<C>(ctx, h);
};
template <class Ctx, class C>
concept fits_deque_owner = requires(Ctx const& ctx, typename C::OwnerHandle& h) {
    chaselev::mint_owner_session<C>(ctx, h);
};
template <class Ctx, class C>
concept fits_deque_thief = requires(Ctx const& ctx, typename C::ThiefHandle& h) {
    chaselev::mint_thief_session<C>(ctx, h);
};
template <class Ctx, class C>
concept fits_grid_producer = requires(Ctx const& ctx, typename C::template ProducerHandle<0>& h) {
    sharded::mint_producer_session<C, 0>(ctx, h);
};
template <class Ctx, class C>
concept fits_grid_consumer = requires(Ctx const& ctx, typename C::template ConsumerHandle<0>& h) {
    sharded::mint_consumer_session<C, 0>(ctx, h);
};
template <class Ctx, class C>
concept fits_calendar_producer = requires(Ctx const& ctx, typename C::template ProducerHandle<0>& h) {
    calendar::mint_producer_session<C, 0>(ctx, h);
};
template <class Ctx, class C>
concept fits_calendar_consumer = requires(Ctx const& ctx, typename C::ConsumerHandle& h) {
    calendar::mint_consumer_session<C>(ctx, h);
};
template <class Ctx, class C>
concept fits_sharded_calendar_producer = requires(Ctx const& ctx, typename C::template ProducerHandle<0>& h) {
    sharded_calendar::mint_producer_session<C, 0>(ctx, h);
};
template <class Ctx, class C>
concept fits_sharded_calendar_consumer = requires(Ctx const& ctx, typename C::template ConsumerHandle<0>& h) {
    sharded_calendar::mint_consumer_session<C, 0>(ctx, h);
};

// ── The background context fits every endpoint of a background-row channel ──

static_assert(fits_spsc_producer<Bg, Spsc> && fits_spsc_consumer<Bg, Spsc>);
static_assert(fits_mpmc_producer<Bg, Mpmc> && fits_mpmc_consumer<Bg, Mpmc>);
static_assert(fits_snapshot_writer<Bg, Snap> && fits_snapshot_reader<Bg, Snap>);
static_assert(fits_swmr_writer<Bg, Swmr> && fits_swmr_reader<Bg, Swmr>);
static_assert(fits_swmr_runtime_writer<Bg, Swmr> && fits_swmr_runtime_reader<Bg, Swmr>);
static_assert(fits_deque_owner<Bg, Deque> && fits_deque_thief<Bg, Deque>);
static_assert(fits_grid_producer<Bg, Grid> && fits_grid_consumer<Bg, Grid>);
static_assert(fits_calendar_producer<Bg, Calendar> && fits_calendar_consumer<Bg, Calendar>);
static_assert(fits_sharded_calendar_producer<Bg, ShardedCalendar>
              && fits_sharded_calendar_consumer<Bg, ShardedCalendar>);

// ── The foreground context fits none of them ──

static_assert(!fits_spsc_producer<Fg, Spsc> && !fits_spsc_consumer<Fg, Spsc>);
static_assert(!fits_mpmc_producer<Fg, Mpmc> && !fits_mpmc_consumer<Fg, Mpmc>);
static_assert(!fits_snapshot_writer<Fg, Snap> && !fits_snapshot_reader<Fg, Snap>);
static_assert(!fits_swmr_writer<Fg, Swmr> && !fits_swmr_reader<Fg, Swmr>);
static_assert(!fits_swmr_runtime_writer<Fg, Swmr> && !fits_swmr_runtime_reader<Fg, Swmr>);
static_assert(!fits_deque_owner<Fg, Deque> && !fits_deque_thief<Fg, Deque>);
static_assert(!fits_grid_producer<Fg, Grid> && !fits_grid_consumer<Fg, Grid>);
static_assert(!fits_calendar_producer<Fg, Calendar> && !fits_calendar_consumer<Fg, Calendar>);
static_assert(!fits_sharded_calendar_producer<Fg, ShardedCalendar>
              && !fits_sharded_calendar_consumer<Fg, ShardedCalendar>);

// ── A plain payload fits the foreground context ──

static_assert(fits_spsc_producer<Fg, PlainSpsc> && fits_spsc_consumer<Fg, PlainSpsc>);

// ── A value that is not a context fits nothing ──

static_assert(!fits_spsc_producer<int, Spsc> && !fits_spsc_producer<int, PlainSpsc>);

// ── The async pipeline pair asks both endpoints of the one context ──

namespace aps = proto::async_pipeline_session;

struct SlotTag {};
struct SlotHandle {
    using slot_tag = SlotTag;
    static constexpr std::size_t slot_bytes = 256;
    static constexpr std::size_t stages = 2;
    static constexpr aps::MemoryScope scope = aps::MemoryScope::Cta;
    constexpr void arrive_expect_tx(std::size_t) noexcept {}
    [[nodiscard]] constexpr bool try_wait(std::uint32_t) noexcept { return true; }
};

static_assert(aps::CtxFitsAsyncPipelineProducer<256, SlotHandle, Fg>);
static_assert(aps::CtxFitsAsyncPipelineConsumer<256, SlotHandle, Fg>);
static_assert(!aps::CtxFitsAsyncPipelineProducer<256, SlotHandle, int>, "a value that is not a context fits nothing");
static_assert(!aps::CtxFitsAsyncPipelineConsumer<128, SlotHandle, Fg>, "the slot width still gates the session");

// Mints a session over a background-row channel from a background context,
// sends one value and receives it back.  Returns true when the value arrives.
[[nodiscard]] inline bool round_trip_under_background_context() noexcept {
    using ::crucible::safety::mint_permission_root;
    using ::crucible::safety::mint_permission_split;
    using proto::detach_reason::TestInstrumentation;

    const Bg bg{eff::testing::bg()};
    Spsc channel;
    auto whole = mint_permission_root<Spsc::whole_tag>();
    auto [producer_perm, consumer_perm] =
        mint_permission_split<Spsc::producer_tag, Spsc::consumer_tag>(std::move(whole));
    auto producer = channel.producer(std::move(producer_perm));
    auto consumer = channel.consumer(std::move(consumer_perm));

    auto producer_session = spsc::mint_producer_session<Spsc>(bg, producer);
    auto consumer_session = spsc::mint_consumer_session<Spsc>(bg, consumer);

    auto sent = std::move(producer_session).send(eff::Computation<eff::Row<>, int>::lift<eff::Effect::Bg>(41),
                                                 spsc::blocking_push);
    auto [received, rest] = std::move(consumer_session).recv(spsc::blocking_pop);
    const bool arrived = received.graded().peek() == 41;
    std::move(sent).detach(TestInstrumentation{});
    std::move(rest).detach(TestInstrumentation{});
    return arrived;
}

}  // namespace endpoint_ctx_gate

int main() {
    if (!endpoint_ctx_gate::round_trip_under_background_context()) {
        std::fprintf(stderr, "test_session_endpoint_ctx_gate: the value did not arrive through the gated session\n");
        return 1;
    }
    std::fprintf(stderr, "test_session_endpoint_ctx_gate: the gate probes held and the round trip passed\n");
    return 0;
}

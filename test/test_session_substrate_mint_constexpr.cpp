// Every linear-permission mint factory must be nodiscard, constexpr and
// noexcept.  Each one below is driven with a freshly minted root
// permission, or a split pack where the tag tree needs one, and the
// noexcept half is pinned directly.
//
// The constexpr half is witnessed only indirectly, by each substrate
// factory in the chain being constexpr itself.  Dropping constexpr from
// a link would surface at a constant-expression caller rather than here.
//
// The pool-mediated factories are deliberately absent.  Lending from a
// shared permission pool performs an atomic compare-and-exchange, which
// is not eligible for constant evaluation, and an atomic refcount
// misrepresents runtime cost in the same way a heap allocation does.

#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>
#include <crucible/concurrent/_PermissionedCalendarGrid.h>
#include <crucible/concurrent/_PermissionedChaseLevDeque.h>
#include <crucible/concurrent/_PermissionedShardedCalendarGrid.h>
#include <crucible/concurrent/_PermissionedShardedGrid.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/sessions/_CalendarGridSession.h>
#include <crucible/sessions/_ChaseLevDequeSession.h>
#include <crucible/sessions/_ShardedCalendarGridSession.h>
#include <crucible/sessions/_ShardedGridSession.h>
#include <crucible/sessions/_SwmrSession.h>

#include <fixy/Ctx.h>

#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

namespace safety = ::crucible::safety;
namespace concur = ::crucible::concurrent;

struct ChaseLevTag {};
struct MetaLogTag {};
struct SwmrTag {};
struct CalendarTag {};
struct ShardedGridTag {};
struct ShardedCalendarTag {};

struct WriterTag {};
struct ReaderTag {};

struct DeadlineKey {
    static std::uint64_t key(int const& v) noexcept { return static_cast<std::uint64_t>(v); }
};

using Deque = concur::PermissionedChaseLevDeque<int, 64, ChaseLevTag>;
using MetaLog = ::crucible::PermissionedMetaLog<MetaLogTag>;
using Swmr = safety::proto::swmr_session::SwmrSession<int, WriterTag, ReaderTag>;
using Calendar =
    concur::PermissionedCalendarGrid<int, /*M=*/2, /*Buckets=*/8, /*BucketCap=*/4, DeadlineKey, 1ULL, CalendarTag>;
using ShardedGrid = concur::PermissionedShardedGrid<int, 2, 2, 64, ShardedGridTag>;
using ShardedCal = concur::PermissionedShardedCalendarGrid<int, /*Shards=*/2, /*Buckets=*/8, /*BucketCap=*/4,
                                                           DeadlineKey, 1ULL, ShardedCalendarTag>;

namespace cs = ::crucible::safety::proto::chaselev_session;
namespace ms = ::crucible::metalog_session;
namespace ws = ::crucible::safety::proto::swmr_session;
namespace cgs = ::crucible::safety::proto::calendar_grid_session;
namespace sgs = ::crucible::safety::proto::sharded_grid_session;
namespace scs = ::crucible::safety::proto::sharded_calendar_grid_session;

void exercise_chaselev_owner() {
    Deque deque;
    auto perm = safety::mint_permission_root<Deque::owner_tag>();
    auto owner = cs::mint_chaselev_owner<Deque>(deque, std::move(perm));
    static_assert(noexcept(cs::mint_chaselev_owner<Deque>(std::declval<Deque&>(),
                                                          std::declval<safety::Permission<Deque::owner_tag>&&>())));
    (void)owner;
}

// The permissioned MetaLog makes its handles through member accessors, so
// its mints are the two session mints, which take a handle by move and
// give it back at End.
void exercise_metalog_pair() {
    namespace fp = ::foundation::permissions;
    using FgCtx = ::fixy::HotFgCtx;
    const FgCtx ctx = ::foundation::effects::testing::foreground();
    ::crucible::MetaLog raw_log;
    MetaLog log{raw_log};
    auto whole = fp::mint_permission_root<MetaLog::whole_tag>();
    auto [pp, cp] = fp::mint_permission_split<MetaLog::producer_tag, MetaLog::consumer_tag>(std::move(whole));
    auto producer = ms::mint_metalog_producer_session<MetaLog>(ctx, log.producer(std::move(pp)));
    auto consumer = ms::mint_metalog_consumer_session<MetaLog>(ctx, log.consumer(std::move(cp)));
    static_assert(noexcept(ms::mint_metalog_producer_session<MetaLog>(
        std::declval<FgCtx const&>(), std::declval<MetaLog::ProducerHandle&&>())));
    static_assert(noexcept(ms::mint_metalog_consumer_session<MetaLog>(
        std::declval<FgCtx const&>(), std::declval<MetaLog::ConsumerHandle&&>())));
    (void)std::move(producer).select<1>(::fixy::session::no_label).close();
    (void)std::move(consumer).select<1>(::fixy::session::no_label).close();
}

void exercise_swmr_writer() {
    Swmr swmr;
    auto perm = safety::mint_permission_root<WriterTag>();
    auto writer = ws::mint_swmr_writer<Swmr>(swmr, std::move(perm));
    static_assert(
        noexcept(ws::mint_swmr_writer<Swmr>(std::declval<Swmr&>(), std::declval<safety::Permission<WriterTag>&&>())));
    (void)writer;
}

void exercise_calendar_pair() {
    Calendar grid;
    auto whole = safety::mint_permission_root<concur::calendar_tag::Whole<CalendarTag>>();
    auto perms =
        safety::mint_grid_permissions<concur::calendar_tag::Whole<CalendarTag>, /*M=*/2, /*N=*/1>(std::move(whole));
    auto producer = cgs::mint_calendar_grid_producer<Calendar, 0>(grid, std::move(std::get<0>(perms.producers)));
    auto consumer = cgs::mint_calendar_grid_consumer<Calendar>(grid, std::move(std::get<0>(perms.consumers)));
    static_assert(noexcept(cgs::mint_calendar_grid_producer<Calendar, 0>(
        std::declval<Calendar&>(),
        std::declval<safety::Permission<concur::calendar_tag::Producer<CalendarTag, 0>>&&>())));
    static_assert(noexcept(cgs::mint_calendar_grid_consumer<Calendar>(
        std::declval<Calendar&>(), std::declval<safety::Permission<concur::calendar_tag::Consumer<CalendarTag>>&&>())));
    (void)producer;
    (void)consumer;
}

void exercise_sharded_grid_pair() {
    ShardedGrid grid;
    auto whole = safety::mint_permission_root<concur::grid_tag::Whole<ShardedGridTag>>();
    auto perms = safety::mint_grid_permissions<concur::grid_tag::Whole<ShardedGridTag>, 2, 2>(std::move(whole));
    auto producer = sgs::mint_sharded_grid_producer<ShardedGrid, 0>(grid, std::move(std::get<0>(perms.producers)));
    auto consumer = sgs::mint_sharded_grid_consumer<ShardedGrid, 0>(grid, std::move(std::get<0>(perms.consumers)));
    static_assert(noexcept(sgs::mint_sharded_grid_producer<ShardedGrid, 0>(
        std::declval<ShardedGrid&>(),
        std::declval<safety::Permission<concur::grid_tag::Producer<ShardedGridTag, 0>>&&>())));
    static_assert(noexcept(sgs::mint_sharded_grid_consumer<ShardedGrid, 0>(
        std::declval<ShardedGrid&>(),
        std::declval<safety::Permission<concur::grid_tag::Consumer<ShardedGridTag, 0>>&&>())));
    (void)producer;
    (void)consumer;
}

void exercise_sharded_calendar_pair() {
    ShardedCal grid;
    auto whole = safety::mint_permission_root<concur::sharded_calendar_tag::Whole<ShardedCalendarTag>>();
    auto perms = safety::mint_grid_permissions<concur::sharded_calendar_tag::Whole<ShardedCalendarTag>,
                                               /*M=*/2, /*N=*/2>(std::move(whole));
    auto producer =
        scs::mint_sharded_calendar_grid_producer<ShardedCal, 0>(grid, std::move(std::get<0>(perms.producers)));
    auto consumer =
        scs::mint_sharded_calendar_grid_consumer<ShardedCal, 0>(grid, std::move(std::get<0>(perms.consumers)));
    static_assert(noexcept(scs::mint_sharded_calendar_grid_producer<ShardedCal, 0>(
        std::declval<ShardedCal&>(),
        std::declval<safety::Permission<typename ShardedCal::template shard_producer_tag<0>>&&>())));
    static_assert(noexcept(scs::mint_sharded_calendar_grid_consumer<ShardedCal, 0>(
        std::declval<ShardedCal&>(),
        std::declval<safety::Permission<typename ShardedCal::template shard_consumer_tag<0>>&&>())));
    (void)producer;
    (void)consumer;
}

}  // namespace

int main() {
    exercise_chaselev_owner();
    exercise_metalog_pair();
    exercise_swmr_writer();
    exercise_calendar_pair();
    exercise_sharded_grid_pair();
    exercise_sharded_calendar_pair();
    std::fprintf(stderr, "session substrate mints: 10 OK\n");
    return 0;
}

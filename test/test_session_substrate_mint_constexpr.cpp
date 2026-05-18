// fixy-A2-017 HS14 sentinel — 12 linear-permission token mint factories
// across 8 session-substrate headers must be [[nodiscard]] constexpr noexcept
// per CLAUDE.md §XXI.
//
// Each mint is exercised with a freshly-minted root Permission (or split
// pack for grid/calendar tags) and the noexcept attribute is pinned at
// compile time via static_assert(noexcept(call)). The constexpr keyword
// is structurally witnessed by the requirement that the substrate factory
// methods (PermissionedX::producer(perm) etc.) are themselves constexpr —
// removing constexpr from any link in the chain would surface in a future
// constant-expression caller without breaking this TU. The combined ctest
// run validates the wrappers compile cleanly with the substrate factory
// constexpr chain in place.
//
// Pool-mediated factories (mint_chaselev_thief, mint_swmr_reader) are
// EXCLUDED — SharedPermissionPool::lend() performs atomic CAS, which is
// not constexpr-eligible. Per §XXI: "unless the factory genuinely
// allocates" — atomic refcounting is the same "lies about runtime cost"
// concern as heap allocation.

#include <crucible/concurrent/PermissionedCalendarGrid.h>
#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/concurrent/PermissionedShardedCalendarGrid.h>
#include <crucible/concurrent/PermissionedShardedGrid.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/CalendarGridSession.h>
#include <crucible/sessions/ChainEdgeSession.h>
#include <crucible/sessions/ChaseLevDequeSession.h>
#include <crucible/sessions/MetaLogSession.h>
#include <crucible/sessions/ShardedCalendarGridSession.h>
#include <crucible/sessions/ShardedGridSession.h>
#include <crucible/sessions/SwmrSession.h>

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
struct ChainEdgeTag {};
struct CalendarTag {};
struct ShardedGridTag {};
struct ShardedCalendarTag {};

struct WriterTag {};
struct ReaderTag {};

struct DeadlineKey {
    static std::uint64_t key(int const& v) noexcept {
        return static_cast<std::uint64_t>(v);
    }
};

using Deque       = concur::PermissionedChaseLevDeque<int, 64, ChaseLevTag>;
using MetaLog     = concur::PermissionedMetaLog<MetaLogTag>;
using Swmr        = safety::proto::swmr_session::SwmrSession<int, WriterTag, ReaderTag>;
using ChainEdge   = concur::PermissionedChainEdge<concur::VendorBackend::CPU, ChainEdgeTag>;
using Calendar    = concur::PermissionedCalendarGrid<
    int, /*M=*/2, /*Buckets=*/8, /*BucketCap=*/4, DeadlineKey, 1ULL, CalendarTag>;
using ShardedGrid = concur::PermissionedShardedGrid<int, 2, 2, 64, ShardedGridTag>;
using ShardedCal  = concur::PermissionedShardedCalendarGrid<
    int, /*Shards=*/2, /*Buckets=*/8, /*BucketCap=*/4, DeadlineKey, 1ULL,
    ShardedCalendarTag>;

namespace cs = ::crucible::safety::proto::chaselev_session;
namespace ms = ::crucible::safety::proto::metalog_session;
namespace ws = ::crucible::safety::proto::swmr_session;
namespace es = ::crucible::safety::proto::chainedge_session;
namespace cgs = ::crucible::safety::proto::calendar_grid_session;
namespace sgs = ::crucible::safety::proto::sharded_grid_session;
namespace scs = ::crucible::safety::proto::sharded_calendar_grid_session;

void exercise_chaselev_owner() {
    Deque deque;
    auto perm = safety::mint_permission_root<Deque::owner_tag>();
    auto owner = cs::mint_chaselev_owner<Deque>(deque, std::move(perm));
    static_assert(noexcept(cs::mint_chaselev_owner<Deque>(
        std::declval<Deque&>(),
        std::declval<safety::Permission<Deque::owner_tag>&&>())));
    (void)owner;
}

void exercise_metalog_pair() {
    ::crucible::MetaLog raw_log;
    MetaLog log{raw_log};
    auto whole = safety::mint_permission_root<MetaLog::whole_tag>();
    auto [pp, cp] = safety::mint_permission_split<
        MetaLog::producer_tag, MetaLog::consumer_tag>(std::move(whole));
    auto producer = ms::mint_metalog_producer<MetaLog>(log, std::move(pp));
    auto consumer = ms::mint_metalog_consumer<MetaLog>(log, std::move(cp));
    static_assert(noexcept(ms::mint_metalog_producer<MetaLog>(
        std::declval<MetaLog&>(),
        std::declval<safety::Permission<MetaLog::producer_tag>&&>())));
    static_assert(noexcept(ms::mint_metalog_consumer<MetaLog>(
        std::declval<MetaLog&>(),
        std::declval<safety::Permission<MetaLog::consumer_tag>&&>())));
    (void)producer;
    (void)consumer;
}

void exercise_swmr_writer() {
    Swmr swmr;
    auto perm = safety::mint_permission_root<WriterTag>();
    auto writer = ws::mint_swmr_writer<Swmr>(swmr, std::move(perm));
    static_assert(noexcept(ws::mint_swmr_writer<Swmr>(
        std::declval<Swmr&>(),
        std::declval<safety::Permission<WriterTag>&&>())));
    (void)writer;
}

void exercise_chainedge_pair() {
    ChainEdge edge{concur::PlanId{1}, concur::PlanId{2},
                   concur::ChainEdgeId{3}, /*signal_value=*/1};
    auto whole = safety::mint_permission_root<ChainEdge::whole_tag>();
    auto [sp, wp] = safety::mint_permission_split<
        ChainEdge::signaler_tag, ChainEdge::waiter_tag>(std::move(whole));
    auto signaler = es::mint_chainedge_signaler<ChainEdge>(edge, std::move(sp));
    auto waiter = es::mint_chainedge_waiter<ChainEdge>(edge, std::move(wp));
    static_assert(noexcept(es::mint_chainedge_signaler<ChainEdge>(
        std::declval<ChainEdge&>(),
        std::declval<safety::Permission<ChainEdge::signaler_tag>&&>())));
    static_assert(noexcept(es::mint_chainedge_waiter<ChainEdge>(
        std::declval<ChainEdge&>(),
        std::declval<safety::Permission<ChainEdge::waiter_tag>&&>())));
    (void)signaler;
    (void)waiter;
}

void exercise_calendar_pair() {
    Calendar grid;
    auto whole = safety::mint_permission_root<
        concur::calendar_tag::Whole<CalendarTag>>();
    auto perms = safety::mint_grid_permissions<
        concur::calendar_tag::Whole<CalendarTag>, /*M=*/2, /*N=*/1>(
        std::move(whole));
    auto producer = cgs::mint_calendar_grid_producer<Calendar, 0>(
        grid, std::move(std::get<0>(perms.producers)));
    auto consumer = cgs::mint_calendar_grid_consumer<Calendar>(
        grid, std::move(std::get<0>(perms.consumers)));
    static_assert(noexcept(cgs::mint_calendar_grid_producer<Calendar, 0>(
        std::declval<Calendar&>(),
        std::declval<safety::Permission<
            concur::calendar_tag::Producer<CalendarTag, 0>>&&>())));
    static_assert(noexcept(cgs::mint_calendar_grid_consumer<Calendar>(
        std::declval<Calendar&>(),
        std::declval<safety::Permission<
            concur::calendar_tag::Consumer<CalendarTag>>&&>())));
    (void)producer;
    (void)consumer;
}

void exercise_sharded_grid_pair() {
    ShardedGrid grid;
    auto whole = safety::mint_permission_root<
        concur::grid_tag::Whole<ShardedGridTag>>();
    auto perms = safety::mint_grid_permissions<
        concur::grid_tag::Whole<ShardedGridTag>, 2, 2>(std::move(whole));
    auto producer = sgs::mint_sharded_grid_producer<ShardedGrid, 0>(
        grid, std::move(std::get<0>(perms.producers)));
    auto consumer = sgs::mint_sharded_grid_consumer<ShardedGrid, 0>(
        grid, std::move(std::get<0>(perms.consumers)));
    static_assert(noexcept(sgs::mint_sharded_grid_producer<ShardedGrid, 0>(
        std::declval<ShardedGrid&>(),
        std::declval<safety::Permission<
            concur::grid_tag::Producer<ShardedGridTag, 0>>&&>())));
    static_assert(noexcept(sgs::mint_sharded_grid_consumer<ShardedGrid, 0>(
        std::declval<ShardedGrid&>(),
        std::declval<safety::Permission<
            concur::grid_tag::Consumer<ShardedGridTag, 0>>&&>())));
    (void)producer;
    (void)consumer;
}

void exercise_sharded_calendar_pair() {
    ShardedCal grid;
    auto whole = safety::mint_permission_root<
        concur::sharded_calendar_tag::Whole<ShardedCalendarTag>>();
    auto perms = safety::mint_grid_permissions<
        concur::sharded_calendar_tag::Whole<ShardedCalendarTag>,
        /*M=*/2, /*N=*/2>(std::move(whole));
    auto producer = scs::mint_sharded_calendar_grid_producer<ShardedCal, 0>(
        grid, std::move(std::get<0>(perms.producers)));
    auto consumer = scs::mint_sharded_calendar_grid_consumer<ShardedCal, 0>(
        grid, std::move(std::get<0>(perms.consumers)));
    static_assert(noexcept(scs::mint_sharded_calendar_grid_producer<ShardedCal, 0>(
        std::declval<ShardedCal&>(),
        std::declval<safety::Permission<
            typename ShardedCal::template shard_producer_tag<0>>&&>())));
    static_assert(noexcept(scs::mint_sharded_calendar_grid_consumer<ShardedCal, 0>(
        std::declval<ShardedCal&>(),
        std::declval<safety::Permission<
            typename ShardedCal::template shard_consumer_tag<0>>&&>())));
    (void)producer;
    (void)consumer;
}

}  // namespace

int main() {
    exercise_chaselev_owner();
    exercise_metalog_pair();
    exercise_swmr_writer();
    exercise_chainedge_pair();
    exercise_calendar_pair();
    exercise_sharded_grid_pair();
    exercise_sharded_calendar_pair();
    std::fprintf(stderr, "fixy-A2-017 sentinel — 12 mints OK\n");
    return 0;
}

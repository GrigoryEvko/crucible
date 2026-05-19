// ── test_fixy_substr_completeness — Substr.h re-export coverage ───
//
// FIXY-AUDIT-C10: pulls fixy/Substr.h into a TU compiled under
// project warning flags and witnesses that EVERY shipped substrate
// session namespace and its companion concurrent primitive is
// reachable through the fixy::substr:: umbrella with full type
// identity preserved.
//
// Coverage:
//
//   1. Per-namespace protocol-type aliases (ProducerProto /
//      ConsumerProto / WriterProto / ReaderProto / OwnerProto /
//      ThiefProto / SignalerProto / WaiterProto).
//   2. Per-namespace surface concepts (where the substrate ships one).
//   3. Per-namespace mint factories (function-template name lookup).
//   4. concurrent::PermissionedMpscChannel (MPSC raw substrate —
//      no typed-session layer yet).
//   5. concurrent::PermissionedSnapshot (Snapshot raw substrate
//      under SwmrSession's session layer).
//
// HS14: positive-sentinel-only.  No new mint factories are
// introduced — C10 is a re-export/coverage task.  The substrate
// mint factories already ship their own HS14 floors next to their
// definition headers.

#include <crucible/fixy/Substr.h>

#include <type_traits>

namespace fs    = ::crucible::fixy::substr;
namespace cspsc = ::crucible::safety::proto::spsc_session;
namespace cswmr = ::crucible::safety::proto::swmr_session;
namespace cchl  = ::crucible::safety::proto::chaselev_session;
namespace cmet  = ::crucible::safety::proto::metalog_session;
namespace cce   = ::crucible::safety::proto::chainedge_session;
namespace cmpmc = ::crucible::safety::proto::mpmc_channel_session;
namespace ccal  = ::crucible::safety::proto::calendar_grid_session;
namespace cscal = ::crucible::safety::proto::sharded_calendar_grid_session;
namespace csg   = ::crucible::safety::proto::sharded_grid_session;
namespace cconc = ::crucible::concurrent;

// ═════════════════════════════════════════════════════════════════════
// ─── 1. SPSC ─────────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

static_assert(std::is_same_v<
    fs::spsc::ProducerProto<int>,
    cspsc::ProducerProto<int>>);
static_assert(std::is_same_v<
    fs::spsc::ConsumerProto<double>,
    cspsc::ConsumerProto<double>>);

// ═════════════════════════════════════════════════════════════════════
// ─── 2. SWMR ─────────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace test_substr_swmr {
struct WriterTag {};
struct ReaderTag {};
}

static_assert(std::is_same_v<
    fs::swmr::WriterProto<int>,
    cswmr::WriterProto<int>>);
static_assert(std::is_same_v<
    fs::swmr::ReaderProto<int, test_substr_swmr::ReaderTag>,
    cswmr::ReaderProto<int, test_substr_swmr::ReaderTag>>);
static_assert(std::is_same_v<
    fs::swmr::WriterRuntimeProto<int>,
    cswmr::WriterRuntimeProto<int>>);
static_assert(std::is_same_v<
    fs::swmr::ReaderRuntimeProto<int>,
    cswmr::ReaderRuntimeProto<int>>);

static_assert(std::is_same_v<
    fs::swmr::SwmrSession<int,
        test_substr_swmr::WriterTag, test_substr_swmr::ReaderTag>,
    cswmr::SwmrSession<int,
        test_substr_swmr::WriterTag, test_substr_swmr::ReaderTag>>);

// ═════════════════════════════════════════════════════════════════════
// ─── 3. ChaseLev ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace test_substr_chaselev {
struct ThiefTag {};
}

static_assert(std::is_same_v<
    fs::chaselev::OwnerProto<int>,
    cchl::OwnerProto<int>>);
static_assert(std::is_same_v<
    fs::chaselev::ThiefProto<int, test_substr_chaselev::ThiefTag>,
    cchl::ThiefProto<int, test_substr_chaselev::ThiefTag>>);

// ═════════════════════════════════════════════════════════════════════
// ─── 4. MetaLog ──────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

static_assert(std::is_same_v<
    fs::metalog::MetaLogRecord, cmet::MetaLogRecord>);
static_assert(std::is_same_v<
    fs::metalog::ProducerProto, cmet::ProducerProto>);
static_assert(std::is_same_v<
    fs::metalog::ConsumerProto, cmet::ConsumerProto>);

// ═════════════════════════════════════════════════════════════════════
// ─── 5. ChainEdge ────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

static_assert(std::is_same_v<fs::chainedge::Signal, cce::Signal>);
static_assert(std::is_same_v<
    fs::chainedge::SignalerProto, cce::SignalerProto>);
static_assert(std::is_same_v<
    fs::chainedge::WaiterProto, cce::WaiterProto>);

// ═════════════════════════════════════════════════════════════════════
// ─── 6. MPMC ─────────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

static_assert(std::is_same_v<
    fs::mpmc::ProducerProto<int>, cmpmc::ProducerProto<int>>);
static_assert(std::is_same_v<
    fs::mpmc::ConsumerProto<int>, cmpmc::ConsumerProto<int>>);

// ═════════════════════════════════════════════════════════════════════
// ─── 7. CalendarGrid ─────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

static_assert(std::is_same_v<
    fs::calendar_grid::ProducerProto<int>,
    ccal::ProducerProto<int>>);
static_assert(std::is_same_v<
    fs::calendar_grid::ConsumerProto<int>,
    ccal::ConsumerProto<int>>);

// ═════════════════════════════════════════════════════════════════════
// ─── 8. ShardedCalendarGrid ──────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

static_assert(std::is_same_v<
    fs::sharded_calendar_grid::ProducerProto<int>,
    cscal::ProducerProto<int>>);
static_assert(std::is_same_v<
    fs::sharded_calendar_grid::ConsumerProto<int>,
    cscal::ConsumerProto<int>>);

// ═════════════════════════════════════════════════════════════════════
// ─── 9. ShardedGrid ──────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

static_assert(std::is_same_v<
    fs::sharded_grid::ProducerProto<int>,
    csg::ProducerProto<int>>);
static_assert(std::is_same_v<
    fs::sharded_grid::ConsumerProto<int>,
    csg::ConsumerProto<int>>);

// ═════════════════════════════════════════════════════════════════════
// ─── 10. Raw concurrent primitives (no session layer) ────────────────
// ═════════════════════════════════════════════════════════════════════

// MPSC channel — substrate exists, session header does not.
static_assert(std::is_same_v<
    fs::concurrent::PermissionedMpscChannel<int, 16>,
    cconc::PermissionedMpscChannel<int, 16>>);

namespace test_substr_mpsc {
struct UserTag {};
}

static_assert(std::is_same_v<
    fs::concurrent::PermissionedMpscChannel<
        int, 32, test_substr_mpsc::UserTag>,
    cconc::PermissionedMpscChannel<
        int, 32, test_substr_mpsc::UserTag>>);

// Snapshot — raw substrate AND typed-session minters now under
// fixy::substr::snapshot:: (fixy-A4-022; promoted from `concurrent::`).
static_assert(std::is_same_v<
    fs::snapshot::PermissionedSnapshot<int>,
    cconc::PermissionedSnapshot<int>>);

// ═════════════════════════════════════════════════════════════════════
// ─── 11. Mint factory name-lookup (witnesses re-export semantics) ────
// ═════════════════════════════════════════════════════════════════════

namespace test_substr_mint_lookup {
using fs::spsc::mint_producer_session;
using fs::spsc::mint_consumer_session;
using fs::swmr::mint_swmr_writer;
using fs::swmr::mint_swmr_reader;
using fs::swmr::mint_writer_session;
using fs::swmr::mint_reader_session;
using fs::swmr::mint_writer_runtime_session;
using fs::swmr::mint_reader_runtime_session;
using fs::chaselev::mint_chaselev_owner;
using fs::chaselev::mint_chaselev_thief;
using fs::chaselev::mint_owner_session;
using fs::chaselev::mint_thief_session;
using fs::metalog::mint_metalog_producer;
using fs::metalog::mint_metalog_consumer;
using fs::metalog::mint_metalog_producer_session;
using fs::metalog::mint_metalog_consumer_session;
using fs::chainedge::mint_chainedge_signaler;
using fs::chainedge::mint_chainedge_waiter;
using fs::chainedge::mint_chainedge_signaler_session;
using fs::chainedge::mint_chainedge_waiter_session;
using fs::mpmc::mint_mpmc_producer_endpoint;
using fs::mpmc::mint_mpmc_consumer_endpoint;
using fs::mpmc::mint_mpmc_producer_session;
using fs::mpmc::mint_mpmc_consumer_session;
using fs::calendar_grid::mint_calendar_grid_producer;
using fs::calendar_grid::mint_calendar_grid_consumer;
using fs::sharded_calendar_grid::mint_sharded_calendar_grid_producer;
using fs::sharded_calendar_grid::mint_sharded_calendar_grid_consumer;
using fs::sharded_grid::mint_sharded_grid_producer;
using fs::sharded_grid::mint_sharded_grid_consumer;
}

int main() { return 0; }

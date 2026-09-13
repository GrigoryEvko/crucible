// Every name the umbrella offers is compared against the one it comes
// from.  A re-export that had quietly become a parallel declaration
// would satisfy every use site and fail these identities.

#include <crucible/fixy/Substr.h>

#include <type_traits>

namespace fs = ::crucible::fixy::substr;
namespace cspsc = ::crucible::safety::proto::spsc_session;
namespace cswmr = ::crucible::safety::proto::swmr_session;
namespace cchl = ::crucible::safety::proto::chaselev_session;
namespace cmet = ::crucible::safety::proto::metalog_session;
namespace cce = ::crucible::safety::proto::chainedge_session;
namespace cmpmc = ::crucible::safety::proto::mpmc_channel_session;
namespace ccal = ::crucible::safety::proto::calendar_grid_session;
namespace cscal = ::crucible::safety::proto::sharded_calendar_grid_session;
namespace csg = ::crucible::safety::proto::sharded_grid_session;
namespace cconc = ::crucible::concurrent;

static_assert(std::is_same_v<fs::spsc::ProducerProto<int>, cspsc::ProducerProto<int>>);
static_assert(std::is_same_v<fs::spsc::ConsumerProto<double>, cspsc::ConsumerProto<double>>);

namespace test_substr_swmr {
struct WriterTag {};
struct ReaderTag {};
}  // namespace test_substr_swmr

static_assert(std::is_same_v<fs::swmr::WriterProto<int>, cswmr::WriterProto<int>>);
static_assert(std::is_same_v<fs::swmr::ReaderProto<int, test_substr_swmr::ReaderTag>,
                             cswmr::ReaderProto<int, test_substr_swmr::ReaderTag>>);
static_assert(std::is_same_v<fs::swmr::WriterRuntimeProto<int>, cswmr::WriterRuntimeProto<int>>);
static_assert(std::is_same_v<fs::swmr::ReaderRuntimeProto<int>, cswmr::ReaderRuntimeProto<int>>);

static_assert(std::is_same_v<fs::swmr::SwmrSession<int, test_substr_swmr::WriterTag, test_substr_swmr::ReaderTag>,
                             cswmr::SwmrSession<int, test_substr_swmr::WriterTag, test_substr_swmr::ReaderTag>>);

namespace test_substr_chaselev {
struct ThiefTag {};
}  // namespace test_substr_chaselev

static_assert(std::is_same_v<fs::chaselev::OwnerProto<int>, cchl::OwnerProto<int>>);
static_assert(std::is_same_v<fs::chaselev::ThiefProto<int, test_substr_chaselev::ThiefTag>,
                             cchl::ThiefProto<int, test_substr_chaselev::ThiefTag>>);

static_assert(std::is_same_v<fs::metalog::MetaLogRecord, cmet::MetaLogRecord>);
static_assert(std::is_same_v<fs::metalog::ProducerProto, cmet::ProducerProto>);
static_assert(std::is_same_v<fs::metalog::ConsumerProto, cmet::ConsumerProto>);

static_assert(std::is_same_v<fs::chainedge::Signal, cce::Signal>);
static_assert(std::is_same_v<fs::chainedge::SignalerProto, cce::SignalerProto>);
static_assert(std::is_same_v<fs::chainedge::WaiterProto, cce::WaiterProto>);

static_assert(std::is_same_v<fs::mpmc::ProducerProto<int>, cmpmc::ProducerProto<int>>);
static_assert(std::is_same_v<fs::mpmc::ConsumerProto<int>, cmpmc::ConsumerProto<int>>);

static_assert(std::is_same_v<fs::calendar_grid::ProducerProto<int>, ccal::ProducerProto<int>>);
static_assert(std::is_same_v<fs::calendar_grid::ConsumerProto<int>, ccal::ConsumerProto<int>>);

static_assert(std::is_same_v<fs::sharded_calendar_grid::ProducerProto<int>, cscal::ProducerProto<int>>);
static_assert(std::is_same_v<fs::sharded_calendar_grid::ConsumerProto<int>, cscal::ConsumerProto<int>>);

static_assert(std::is_same_v<fs::sharded_grid::ProducerProto<int>, csg::ProducerProto<int>>);
static_assert(std::is_same_v<fs::sharded_grid::ConsumerProto<int>, csg::ConsumerProto<int>>);

// This substrate is the exception: it ships no typed-session header of
// its own, so its mint factories are defined in the umbrella rather
// than re-exported from one.

static_assert(std::is_same_v<fs::mpsc::PermissionedMpscChannel<int, 16>, cconc::PermissionedMpscChannel<int, 16>>);

namespace test_substr_mpsc {
struct UserTag {};
}  // namespace test_substr_mpsc

static_assert(std::is_same_v<fs::mpsc::PermissionedMpscChannel<int, 32, test_substr_mpsc::UserTag>,
                             cconc::PermissionedMpscChannel<int, 32, test_substr_mpsc::UserTag>>);

// Having no session header to compare against, the expected protocol
// is spelled out in full here rather than named.
static_assert(
    std::is_same_v<
        fs::mpsc::ProducerProto<int>,
        ::crucible::safety::proto::Loop<::crucible::safety::proto::Send<int, ::crucible::safety::proto::Continue>>>);
static_assert(
    std::is_same_v<
        fs::mpsc::ConsumerProto<double>,
        ::crucible::safety::proto::Loop<::crucible::safety::proto::Recv<double, ::crucible::safety::proto::Continue>>>);

static_assert(std::is_same_v<fs::snapshot::PermissionedSnapshot<int>, cconc::PermissionedSnapshot<int>>);

// A mint factory is a function template and has no type to compare, so
// naming each one is the test: a name the umbrella failed to re-export
// does not resolve here.
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
}  // namespace test_substr_mint_lookup

int main() { return 0; }

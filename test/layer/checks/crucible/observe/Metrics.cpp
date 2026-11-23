// The compile-time checks of crucible/observe/Metrics.h.

#include <crucible/observe/Metrics.h>

namespace crucible::observe {

static_assert(::fixy::concurrent::SnapshotValue<RuntimeMetricsSample>);

namespace detail::metrics_self_test {

struct probe_reader_brand {};
struct probe_writer_brand {};
// A scope brand that the checks name, and the reader handle of a frame on it.
// No frame door gives a handle on it.
struct probe_scope {};
using Channel = RuntimeMetricsChannel<probe_reader_brand, probe_writer_brand>;
using Handle = Channel::reader_handle<probe_scope>;
using Keeper = KeeperMetricsReader<Handle>;
using Canopy = CanopyMetricsReader<Handle>;

// The whole point of the split: neither role converts to the other, so
// the two door names below differ in what they give a body.
static_assert(!std::is_same_v<Keeper, Canopy>);
static_assert(!std::is_convertible_v<Keeper, Canopy>);
static_assert(!std::is_convertible_v<Canopy, Keeper>);
static_assert(sizeof(Keeper) == sizeof(Handle), "the role is a type-level marker; it must not cost a byte.");

// A reader door lends a share of the reader pool, so its context holds
// Nondet.  A context of the drain holds none.
static_assert(CtxFitsMetricsReaderDoor<::fixy::BgLoadCtx> && !CtxFitsMetricsReaderDoor<::fixy::BgDrainCtx>);

}  // namespace detail::metrics_self_test

}  // namespace crucible::observe

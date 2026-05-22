#pragma once

// ── crucible::fixy::observe — Observe surface under fixy:: ─────────
//
// FIXY-V-214.  Re-exports the SWMR runtime metrics surface
// (observe/Metrics.h) under `fixy::observe::` so Keeper / Canopy
// readers who include only the fixy umbrella never have to descend
// into observe/ to mint a metrics endpoint.
//
// observe::Metrics ships a fixed-size `RuntimeMetrics` payload (8
// doubles + 2 uint32 + 16-double array) wrapped in `Stale<>` for
// staleness propagation, published through an `AtomicSnapshot`-backed
// `SwmrSession`.  The three §XXI mint factories are:
//
//   mint_metrics_writer(channel, Permission<WriterTag>&&)
//      -> ctx-bound mint (writer-tag permission consumed); produces
//         the unique writer handle.
//   mint_keeper_metrics_reader(channel)
//      -> ctx-bound mint (fractional reader from SnapshotPool);
//         returns optional<ReaderHandle> — nullopt if the reader
//         budget is exhausted.
//   mint_canopy_metrics_reader(channel)
//      -> identical-shape reader mint for canopy peers; the keeper /
//         canopy distinction is currently surface-only (substrate
//         shares the underlying mint_swmr_reader call), reserved for
//         future per-role row tagging.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   observe::mint_metrics_writer / mint_keeper_metrics_reader /
//      mint_canopy_metrics_reader                         — factories
//   observe::RuntimeMetrics                               — POD payload
//   observe::RuntimeMetricsSample                         — Stale<RuntimeMetrics>
//   observe::RuntimeMetricsComputation                    — Computation<Row<Bg>, ...>
//   observe::RuntimeMetricsChannel                        — SwmrSession<...>
//   observe::RuntimeMetricsWriter / RuntimeMetricsReader  — handle aliases
//   observe::RuntimeMetricsWriterTag / ReaderTag          — phantom tags
//   observe::fresh_metrics_sample / metrics_sample_at     — sample helpers
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Each `using observe::*;` is a name-lookup directive; the
// call resolves to the same substrate function.

#include <crucible/observe/Metrics.h>

#include <type_traits>

namespace crucible::fixy::observe {

// ── §XXI ctx-bound mints ───────────────────────────────────────────
using ::crucible::observe::mint_metrics_writer;
using ::crucible::observe::mint_keeper_metrics_reader;
using ::crucible::observe::mint_canopy_metrics_reader;

// ── Payload carriers ───────────────────────────────────────────────
using ::crucible::observe::RuntimeMetrics;
using ::crucible::observe::RuntimeMetricsSample;
using ::crucible::observe::RuntimeMetricsComputation;

// ── Channel + role-typed handles ───────────────────────────────────
using ::crucible::observe::RuntimeMetricsChannel;
using ::crucible::observe::RuntimeMetricsWriter;
using ::crucible::observe::RuntimeMetricsReader;

// ── Phantom tags (grep-discoverable for review) ────────────────────
using ::crucible::observe::RuntimeMetricsWriterTag;
using ::crucible::observe::RuntimeMetricsReaderTag;

// ── Sample-construction helpers ────────────────────────────────────
using ::crucible::observe::fresh_metrics_sample;
using ::crucible::observe::metrics_sample_at;

}  // namespace crucible::fixy::observe

// ── Self-test ──────────────────────────────────────────────────────
//
// Witnesses pin the §XXI re-export discipline:
//
//   (1)-(3) Pointer identity for each of the three mint factories —
//           the using-decl is a name-lookup directive, not a fresh
//           overload.
//   (4)-(9) Carrier identity: RuntimeMetrics / Sample / Computation /
//           Channel / Writer / Reader aliases must IS-A the substrate
//           types, not merely convertible.
//   (10)    Stale<>-discipline survives the re-export (Sample stays
//           Stale-wrapped, not unwrapped to a bare RuntimeMetrics).

namespace crucible::fixy::observe::self_test {

static_assert(std::is_same_v<
    decltype(&::crucible::fixy::observe::mint_metrics_writer),
    decltype(&::crucible::observe::mint_metrics_writer)>,
    "FIXY-V-214: fixy::observe::mint_metrics_writer must alias "
    "observe::mint_metrics_writer.");

static_assert(std::is_same_v<
    decltype(&::crucible::fixy::observe::mint_keeper_metrics_reader),
    decltype(&::crucible::observe::mint_keeper_metrics_reader)>,
    "FIXY-V-214: fixy::observe::mint_keeper_metrics_reader must "
    "alias observe::mint_keeper_metrics_reader.");

static_assert(std::is_same_v<
    decltype(&::crucible::fixy::observe::mint_canopy_metrics_reader),
    decltype(&::crucible::observe::mint_canopy_metrics_reader)>,
    "FIXY-V-214: fixy::observe::mint_canopy_metrics_reader must "
    "alias observe::mint_canopy_metrics_reader.");

static_assert(std::is_same_v<
    ::crucible::fixy::observe::RuntimeMetrics,
    ::crucible::observe::RuntimeMetrics>,
    "FIXY-V-214: RuntimeMetrics carrier identity.");

static_assert(std::is_same_v<
    ::crucible::fixy::observe::RuntimeMetricsSample,
    ::crucible::observe::RuntimeMetricsSample>,
    "FIXY-V-214: RuntimeMetricsSample (Stale<>) carrier identity.");

static_assert(std::is_same_v<
    ::crucible::fixy::observe::RuntimeMetricsComputation,
    ::crucible::observe::RuntimeMetricsComputation>,
    "FIXY-V-214: RuntimeMetricsComputation (Bg row) carrier identity.");

static_assert(std::is_same_v<
    ::crucible::fixy::observe::RuntimeMetricsChannel,
    ::crucible::observe::RuntimeMetricsChannel>,
    "FIXY-V-214: RuntimeMetricsChannel (SwmrSession) carrier identity.");

static_assert(std::is_same_v<
    ::crucible::fixy::observe::RuntimeMetricsWriter,
    ::crucible::observe::RuntimeMetricsWriter>,
    "FIXY-V-214: RuntimeMetricsWriter handle alias identity.");

static_assert(std::is_same_v<
    ::crucible::fixy::observe::RuntimeMetricsReader,
    ::crucible::observe::RuntimeMetricsReader>,
    "FIXY-V-214: RuntimeMetricsReader handle alias identity.");

// (10) Stale<> wrapping survives.  Sample MUST be a Stale<> over the
//      payload, not an unwrapped RuntimeMetrics — otherwise the
//      staleness-propagation surface silently degrades to "fresh".
static_assert(!std::is_same_v<
    ::crucible::fixy::observe::RuntimeMetricsSample,
    ::crucible::fixy::observe::RuntimeMetrics>,
    "FIXY-V-214: RuntimeMetricsSample must remain Stale<RuntimeMetrics>, "
    "not collapse to bare RuntimeMetrics through the re-export.");

}  // namespace crucible::fixy::observe::self_test

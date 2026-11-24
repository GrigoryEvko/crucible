// MetaLogSession microbench.
//
// The load-bearing evidence is structural: the PermissionedMetaLog handles
// stay pointer-sized.  The timed results are for drift tracking only.  Each
// body does one append and one drain, so the fixed-size MetaLog ring stays
// at depth 0 or 1 across the auto-batched harness.

#include <cstdint>
#include <utility>

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <fixy/Core.h>
#include <fixy/concurrent/PermissionedChannel.h>

#include <foundation/core/Seat.h>
#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include "bench_harness.h"
#include "../test/trace_rig.h"

namespace {

constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

struct BenchTag {};

// The one call site for the root of the log, so the log type names the
// brand of that site.
[[nodiscard]] auto log_root() noexcept {
    return ::foundation::permissions::mint_permission_root<::crucible::metalog_tag::Whole<BenchTag>>();
}

using PermissionedLog = ::crucible::permissioned_metalog_t<decltype(log_root())>;

[[nodiscard]] ::crucible::TensorMeta make_meta(std::uint32_t id) {
    ::crucible::TensorMeta meta{};
    meta.sizes[0] = ::crucible::tensor_dim(static_cast<std::int64_t>(id));
    meta.strides[0] = ::crucible::tensor_dim(1);
    meta.ndim = 1;
    meta.dtype = ::crucible::ScalarType::Float;
    meta.device_type = ::crucible::DeviceType::CPU;
    meta.device_idx = -1;
    meta.storage_nbytes = id * 16;
    meta.version = id;
    return meta;
}

// The raw log of the bench: each door opens the side of the door for one call
// (OpenLog of test/trace_rig.h).
using RawLog = ::crucible::test::OpenLog;

[[nodiscard]] ::fixy::Option<::crucible::TensorMeta> raw_drain_one(RawLog& log) {
    const std::uint64_t t = log.total_released();
    if (t == log.total_produced()) {
        return ::fixy::none;
    }
    const ::crucible::TensorMeta meta = log.copy_record(t);
    log.advance_tail(t + 1);
    return ::fixy::some(meta);
}

void reset_log(RawLog& log) { log.reset(); }

bench::Report bench_raw_append_raw_drain(RawLog& log) {
    reset_log(log);
    std::uint32_t i = 0;
    auto report = bench::run("round-trip: raw MetaLog append + raw drain", [&] {
        const ::fixy::FixedArray<::crucible::TensorMeta, 1> meta{make_meta(++i)};
        const auto idx = log.try_append(meta.view());
        bench::do_not_optimize(idx);
        const auto drained = raw_drain_one(log).value_or(::crucible::TensorMeta{});
        bench::do_not_optimize(drained.version);
    });
    reset_log(log);
    return report;
}

// The row reaches the two linear handles through a Ref of each, so no closure
// holds a reference to a handle.  The log of the handles is a held split, so
// the row does not reset it: each sample drains the record that it appends.
template <class Producer, class Consumer>
bench::Report bench_permissioned_append_drain(Producer& producer, Consumer& consumer) {
    std::uint32_t i = 0;
    const auto pure = ::foundation::effects::testing::foreground();
    auto report = bench::run("round-trip: permissioned append + drain",
                             [&i, &pure, appender = ::fixy::to(producer), drainer = ::fixy::to(consumer)] {
                                 const auto meta = make_meta(++i);
                                 const bool appended = appender->try_append_one_pure(pure, meta);
                                 bench::do_not_optimize(appended);
                                 const auto drained = drainer->try_drain_one().value_or(::crucible::TensorMeta{});
                                 bench::do_not_optimize(drained.version);
                             });
    return report;
}

// Each session owns its handle for the run and gives it back at End.  A
// MetaLog handle refuses move-assignment, so each loop keeps its current
// session in a Seat of foundation/core/Seat.h, which the harness holds for
// the row (run_with).  Each step runs on the session in the storage of the
// Seat and puts the next session there.  The steps take generic lambdas, so
// the brand of each session stays in the template parameter of its lambda.
template <class Producer, class Consumer>
bench::Report bench_typed_send_recv(Producer producer, Consumer consumer) {
    namespace ses = ::crucible::metalog_session;
    const auto ctx = ::foundation::effects::testing::foreground();

    std::uint32_t i = 0;
    auto [report, seats] = bench::run_with(
        "round-trip: typed session send + recv",
        ::fixy::Record{::foundation::core::mint_seat(
                           ses::mint_metalog_producer_session<PermissionedLog>(ctx, std::move(producer))),
                       ::foundation::core::mint_seat(
                           ses::mint_metalog_consumer_session<PermissionedLog>(ctx, std::move(consumer)))},
        [&i, &ctx](auto& held) noexcept {
            auto& [prod, cons] = held;
            const ::crucible::TensorMeta meta = make_meta(++i);
            prod.step([&](auto&& session) noexcept {
                return std::move(session).template select<0>(::fixy::session::no_label).send(ctx, meta);
            });
            cons.step([&](auto&& session) noexcept {
                auto [received, next] = std::move(session).template select<0>(::fixy::session::no_label).recv(ctx);
                bench::do_not_optimize(received.version);
                return std::move(next);
            });
        });
    auto [prod, cons] = std::move(seats);
    static_cast<void>(std::move(prod).take().template select<1>(::fixy::session::no_label).close());
    static_cast<void>(std::move(cons).take().template select<1>(::fixy::session::no_label).close());
    return std::move(report);
}

}  // namespace

int main() {
    static_assert(sizeof(PermissionedLog::ProducerEnd) == sizeof(void*));
    static_assert(sizeof(PermissionedLog::ConsumerEnd) == sizeof(void*));

    ::fixy::Box<RawLog> raw_owner = ::fixy::mint_box<RawLog>(kAlloc);
    RawLog& raw = *raw_owner;
    PermissionedLog log{kAlloc};

    namespace fp = ::foundation::permissions;
    auto [pp, cp] = fp::mint_permission_split<PermissionedLog::producer_tag, PermissionedLog::consumer_tag>(log_root());

    // The frame of the two ends opens before the timed rows, and the rows run
    // in its body.  A report owns its samples, so it is no result of a frame,
    // and the body also writes the reports.
    ::fixy::concurrent::with_ends(log, std::move(pp), std::move(cp), [&raw](auto producer, auto consumer) noexcept {
        static_assert(sizeof(producer) == sizeof(void*));
        static_assert(sizeof(consumer) == sizeof(void*));

        // A braced list runs its initializers in order, so the typed run takes
        // the handles only after the permissioned run is done with them.
        ::fixy::FixedArray<bench::Report, 3> reports{
            bench_raw_append_raw_drain(raw),
            bench_permissioned_append_drain(producer, consumer),
            bench_typed_send_recv(std::move(producer), std::move(consumer)),
        };

        bench::emit_reports_text(reports.view());

        bench::print("\n=== MetaLogSession deltas ===\n");
        ::fixy::FixedArray<bench::Compare, 2> const compares{
            bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<1>())),
            bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<2>())),
        };
        bench::emit_compares(compares.view());

        bench::print("\n=== verdict (TIER A — structural) ===\n");
        bench::print("  PermissionedMetaLog handles are pointer-sized.\n");
        bench::print("  Timed MetaLog deltas above are informational; the bodies copy a\n");
        bench::print("  168-byte TensorMeta and are sensitive to harness layout.\n");

        bench::emit_reports_json(reports.view(), bench::env_json());
    });
    return 0;
}

// MetaLogSession microbench.
//
// The load-bearing evidence is structural: the PermissionedMetaLog handles
// stay pointer-sized.  The timed results are for drift tracking only.  Each
// body does one append and one drain, so the fixed-size MetaLog ring stays
// at depth 0 or 1 across the auto-batched harness.

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

#include <crucible/MetaLog.h>
#include <crucible/MetaLogSession.h>
#include <crucible/PermissionedMetaLog.h>

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

#include "bench_harness.h"

namespace {

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

[[nodiscard]] std::optional<::crucible::TensorMeta> raw_drain_one(::crucible::MetaLog& log) {
    const std::uint64_t t = log.tail.peek_relaxed();
    if (t == log.head.get()) {
        return std::nullopt;
    }
    ::crucible::TensorMeta meta = log.at(t);
    log.advance_tail(t + 1);
    return meta;
}

void reset_log(::crucible::MetaLog& log) { log.reset(); }

bench::Report bench_raw_append_raw_drain(::crucible::MetaLog& log) {
    reset_log(log);
    std::uint32_t i = 0;
    auto report = bench::run("round-trip: raw MetaLog append + raw drain", [&] {
        const auto meta = make_meta(++i);
        const auto idx = log.try_append(&meta, 1);
        bench::do_not_optimize(idx);
        const auto drained = raw_drain_one(log).value_or(::crucible::TensorMeta{});
        bench::do_not_optimize(drained.version);
    });
    reset_log(log);
    return report;
}

bench::Report bench_permissioned_append_drain(PermissionedLog::ProducerHandle& producer,
                                              PermissionedLog::ConsumerHandle& consumer, ::crucible::MetaLog& log) {
    reset_log(log);
    std::uint32_t i = 0;
    auto report = bench::run("round-trip: permissioned append + drain", [&] {
        const auto meta = make_meta(++i);
        const bool appended = producer.try_append_one(meta);
        bench::do_not_optimize(appended);
        const auto drained = consumer.try_drain_one().value_or(::crucible::TensorMeta{});
        bench::do_not_optimize(drained.version);
    });
    reset_log(log);
    return report;
}

// Each session owns its handle for the run and gives it back at End.  A
// MetaLog handle refuses move-assignment, so each step puts the next
// session in place of the last one with emplace.
bench::Report bench_typed_send_recv(PermissionedLog::ProducerHandle&& producer,
                                    PermissionedLog::ConsumerHandle&& consumer, ::crucible::MetaLog& log) {
    namespace ses = ::crucible::metalog_session;
    const auto ctx = ::foundation::effects::testing::foreground();

    reset_log(log);
    std::optional prod{ses::mint_metalog_producer_session<PermissionedLog>(ctx, std::move(producer))};
    std::optional cons{ses::mint_metalog_consumer_session<PermissionedLog>(ctx, std::move(consumer))};
    std::uint32_t i = 0;
    auto report = bench::run("round-trip: typed session send + recv", [&] {
        prod.emplace(std::move(*prod).select<0>(::fixy::session::no_label).send(make_meta(++i), ses::append_one));
        auto [meta, next] = std::move(*cons).select<0>(::fixy::session::no_label).recv(ses::drain_one);
        bench::do_not_optimize(meta.version);
        cons.emplace(std::move(next));
    });
    (void)std::move(*prod).select<1>(::fixy::session::no_label).close();
    (void)std::move(*cons).select<1>(::fixy::session::no_label).close();
    reset_log(log);
    return report;
}

}  // namespace

int main() {
    static_assert(sizeof(PermissionedLog::ProducerHandle) == sizeof(void*));
    static_assert(sizeof(PermissionedLog::ConsumerHandle) == sizeof(void*));

    auto raw_owner = std::make_unique<::crucible::MetaLog>();
    ::crucible::MetaLog& raw = *raw_owner;
    PermissionedLog log{raw};

    namespace fp = ::foundation::permissions;
    auto [pp, cp] = fp::mint_permission_split<PermissionedLog::producer_tag, PermissionedLog::consumer_tag>(log_root());
    auto producer = log.producer(std::move(pp));
    auto consumer = log.consumer(std::move(cp));

    // A braced list runs its initializers in order, so the typed run takes
    // the handles only after the permissioned run is done with them.
    bench::Report reports[] = {
        bench_raw_append_raw_drain(raw),
        bench_permissioned_append_drain(producer, consumer, raw),
        bench_typed_send_recv(std::move(producer), std::move(consumer), raw),
    };

    bench::emit_reports_text(reports);

    std::printf("\n=== MetaLogSession deltas ===\n");
    const bench::Compare cmps[] = {
        bench::compare(reports[0], reports[1]),
        bench::compare(reports[0], reports[2]),
    };
    bench::emit_compares(cmps);

    std::printf("\n=== verdict (TIER A — structural) ===\n");
    std::printf("  PermissionedMetaLog handles are pointer-sized.\n");
    std::printf("  Timed MetaLog deltas above are informational; the bodies copy a\n");
    std::printf("  168-byte TensorMeta and are sensitive to harness layout.\n");

    bench::emit_reports_json(reports, bench::env_json());
    return 0;
}

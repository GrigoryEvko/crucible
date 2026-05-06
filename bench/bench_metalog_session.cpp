// GAPS-061 MetaLogSession microbench.
//
// Load-bearing evidence is structural: PermissionedMetaLog handles stay
// pointer-sized and PermissionedSessionHandle over Handle* stays the same
// size as the bare SessionHandle.  Timed results are emitted for drift
// tracking only; each body performs one append plus one drain to keep the
// fixed-size MetaLog ring at depth 0/1 across the auto-batched harness.

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

#include <crucible/MetaLog.h>
#include <crucible/concurrent/PermissionedMetaLog.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/MetaLogSession.h>

#include "bench_harness.h"

namespace {

struct BenchTag {};
using PermissionedLog = ::crucible::concurrent::PermissionedMetaLog<BenchTag>;

[[nodiscard]] ::crucible::TensorMeta make_meta(std::uint32_t id) {
    ::crucible::TensorMeta meta{};
    meta.sizes[0] = static_cast<std::int64_t>(id);
    meta.strides[0] = 1;
    meta.ndim = 1;
    meta.dtype = ::crucible::ScalarType::Float;
    meta.device_type = ::crucible::DeviceType::CPU;
    meta.device_idx = -1;
    meta.storage_nbytes = id * 16;
    meta.version = id;
    return meta;
}

[[nodiscard]] std::optional<::crucible::TensorMeta>
raw_drain_one(::crucible::MetaLog& log) {
    const std::uint32_t t = log.tail.peek_relaxed();
    if (t == log.head.get()) {
        return std::nullopt;
    }
    ::crucible::TensorMeta meta = log.at(t);
    log.advance_tail(t + 1);
    return meta;
}

void reset_log(::crucible::MetaLog& log) {
    log.reset();
}

bench::Report bench_raw_append_raw_drain(::crucible::MetaLog& log) {
    reset_log(log);
    std::uint32_t i = 0;
    auto report = bench::run("round-trip: raw MetaLog append + raw drain",
        [&]{
            const auto meta = make_meta(++i);
            const auto idx = log.try_append(&meta, 1);
            bench::do_not_optimize(idx);
            const auto drained = raw_drain_one(log).value_or(::crucible::TensorMeta{});
            bench::do_not_optimize(drained.version);
        });
    reset_log(log);
    return report;
}

bench::Report bench_permissioned_append_drain(
    PermissionedLog::ProducerHandle& producer,
    PermissionedLog::ConsumerHandle& consumer,
    ::crucible::MetaLog& log)
{
    reset_log(log);
    std::uint32_t i = 0;
    auto report = bench::run("round-trip: permissioned append + drain",
        [&]{
            const auto meta = make_meta(++i);
            const bool appended = producer.try_append_one(meta);
            bench::do_not_optimize(appended);
            const auto drained =
                consumer.try_drain_one().value_or(::crucible::TensorMeta{});
            bench::do_not_optimize(drained.version);
        });
    reset_log(log);
    return report;
}

bench::Report bench_typed_send_recv(
    PermissionedLog::ProducerHandle& producer,
    PermissionedLog::ConsumerHandle& consumer,
    ::crucible::MetaLog& log)
{
    namespace ses = ::crucible::safety::proto::metalog_session;
    using ::crucible::safety::proto::detach_reason::TestInstrumentation;

    reset_log(log);
    auto prod_psh = ses::mint_metalog_producer_session<PermissionedLog>(
        producer);
    auto cons_psh = ses::mint_metalog_consumer_session<PermissionedLog>(
        consumer);
    std::uint32_t i = 0;
    auto report = bench::run("round-trip: typed PSH.send + PSH.recv",
        [&]{
            auto p2 = std::move(prod_psh).send(make_meta(++i),
                                               ses::blocking_append);
            prod_psh = std::move(p2);
            auto [meta, c2] = std::move(cons_psh).recv(ses::blocking_drain);
            bench::do_not_optimize(meta.version);
            cons_psh = std::move(c2);
        });
    std::move(prod_psh).detach(TestInstrumentation{});
    std::move(cons_psh).detach(TestInstrumentation{});
    reset_log(log);
    return report;
}

}  // namespace

int main(int argc, char** argv) {
    const char* json = (argc > 1) ? argv[1] : nullptr;

    namespace proto = ::crucible::safety::proto;
    static_assert(sizeof(PermissionedLog::ProducerHandle)
                  == sizeof(::crucible::MetaLog*));
    static_assert(sizeof(PermissionedLog::ConsumerHandle)
                  == sizeof(::crucible::MetaLog*));
    static_assert(sizeof(proto::PermissionedSessionHandle<
                      proto::End, proto::EmptyPermSet,
                      PermissionedLog::ProducerHandle*>)
                  == sizeof(proto::SessionHandle<
                      proto::End, PermissionedLog::ProducerHandle*>));
    static_assert(sizeof(proto::PermissionedSessionHandle<
                      proto::End, proto::EmptyPermSet,
                      PermissionedLog::ConsumerHandle*>)
                  == sizeof(proto::SessionHandle<
                      proto::End, PermissionedLog::ConsumerHandle*>));

    auto raw_owner = std::make_unique<::crucible::MetaLog>();
    ::crucible::MetaLog& raw = *raw_owner;
    PermissionedLog log{raw};

    auto whole =
        ::crucible::safety::mint_permission_root<PermissionedLog::whole_tag>();
    auto [pp, cp] = ::crucible::safety::mint_permission_split<
        PermissionedLog::producer_tag,
        PermissionedLog::consumer_tag>(std::move(whole));
    auto producer = log.producer(std::move(pp));
    auto consumer = log.consumer(std::move(cp));

    bench::Report reports[] = {
        bench_raw_append_raw_drain(raw),
        bench_permissioned_append_drain(producer, consumer, raw),
        bench_typed_send_recv(producer, consumer, raw),
    };

    bench::emit_reports_text(reports);

    std::printf("\n=== MetaLogSession deltas ===\n");
    bench::Compare cmps[] = {
        bench::compare(reports[0], reports[1]),
        bench::compare(reports[0], reports[2]),
    };
    for (const auto& c : cmps) c.print_text(stdout);

    std::printf("\n=== verdict (TIER A — structural) ===\n");
    std::printf("  PermissionedMetaLog handles are pointer-sized.\n");
    std::printf("  PSH<End, EmptyPermSet, Handle*> equals bare SessionHandle size.\n");
    std::printf("  Timed MetaLog deltas above are informational; the bodies copy a\n");
    std::printf("  168-byte TensorMeta and are sensitive to harness layout.\n");

    if (json) bench::emit_reports_json(reports, json);
    return 0;
}

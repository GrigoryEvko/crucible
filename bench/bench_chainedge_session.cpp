// GAPS-062 ChainEdge microbench.
//
// Structural evidence is the gate: pointer-sized role handles and
// PSH-vs-bare SessionHandle size equality.  Timed results compare the
// CPU-oracle semaphore stubs; they are informational until real vendor
// pushbuffer/queue backends replace mimic/Semaphore.h's stub bodies.

#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>

#include <crucible/concurrent/PermissionedChainEdge.h>
#include <crucible/permissions/Permission.h>
#include <crucible/sessions/ChainEdgeSession.h>

#include "bench_harness.h"

namespace {

using ::crucible::concurrent::ChainEdge;
using ::crucible::concurrent::ChainEdgeId;
using ::crucible::concurrent::PermissionedChainEdge;
using ::crucible::concurrent::PlanId;
using ::crucible::concurrent::SemaphoreSignal;
using ::crucible::concurrent::VendorBackend;

struct BenchTag {};
using Edge = PermissionedChainEdge<VendorBackend::CPU, BenchTag>;

bench::Report bench_raw_signal_wait(ChainEdge<VendorBackend::CPU>& edge) {
    edge.reset_under_quiescence();
    const SemaphoreSignal signal = edge.expected_signal();
    auto report = bench::run("round-trip: raw ChainEdge signal + wait",
        [&]{
            edge.signal(signal);
            const bool ok = edge.wait(signal);
            bench::do_not_optimize(ok);
        });
    edge.reset_under_quiescence();
    return report;
}

bench::Report bench_permissioned_signal_wait(Edge& edge,
                                             Edge::SignalerHandle& signaler,
                                             Edge::WaiterHandle& waiter)
{
    edge.edge().reset_under_quiescence();
    const SemaphoreSignal signal = signaler.expected_signal();
    auto report = bench::run("round-trip: permissioned signal + wait",
        [&]{
            signaler.signal(signal);
            const bool ok = waiter.try_wait(signal);
            bench::do_not_optimize(ok);
        });
    edge.edge().reset_under_quiescence();
    return report;
}

bench::Report bench_typed_one_shot(Edge& edge,
                                   Edge::SignalerHandle& signaler,
                                   Edge::WaiterHandle& waiter)
{
    namespace ses = ::crucible::safety::proto::chainedge_session;

    edge.edge().reset_under_quiescence();
    const SemaphoreSignal signal = signaler.expected_signal();
    auto report = bench::run("round-trip: typed one-shot send + recv",
        [&]{
            auto sig_psh = ses::mint_chainedge_signaler_session<Edge>(signaler);
            auto wait_psh = ses::mint_chainedge_waiter_session<Edge>(waiter);
            auto sig_end = std::move(sig_psh).send(signal, ses::signal_transport);
            auto [observed, wait_end] = std::move(wait_psh).recv(ses::wait_transport);
            bench::do_not_optimize(observed.value);
            (void)std::move(sig_end).close();
            (void)std::move(wait_end).close();
        });
    edge.edge().reset_under_quiescence();
    return report;
}

}  // namespace

int main(int argc, char** argv) {
    const char* json = (argc > 1) ? argv[1] : nullptr;

    namespace proto = ::crucible::safety::proto;
    static_assert(sizeof(Edge::SignalerHandle) == sizeof(Edge*));
    static_assert(sizeof(Edge::WaiterHandle) == sizeof(Edge*));
    static_assert(sizeof(proto::PermissionedSessionHandle<
                      proto::End, proto::EmptyPermSet,
                      Edge::SignalerHandle*>)
                  == sizeof(proto::SessionHandle<
                      proto::End, Edge::SignalerHandle*>));
    static_assert(sizeof(proto::PermissionedSessionHandle<
                      proto::End, proto::EmptyPermSet,
                      Edge::WaiterHandle*>)
                  == sizeof(proto::SessionHandle<
                      proto::End, Edge::WaiterHandle*>));

    auto raw = std::make_unique<ChainEdge<VendorBackend::CPU>>(
        PlanId{1}, PlanId{2}, ChainEdgeId{3}, 1);
    auto edge = std::make_unique<Edge>(PlanId{1}, PlanId{2}, ChainEdgeId{3}, 1);

    auto whole = ::crucible::safety::mint_permission_root<Edge::whole_tag>();
    auto [sp, wp] = ::crucible::safety::mint_permission_split<
        Edge::signaler_tag, Edge::waiter_tag>(std::move(whole));
    auto signaler = edge->signaler(std::move(sp));
    auto waiter = edge->waiter(std::move(wp));

    bench::Report reports[] = {
        bench_raw_signal_wait(*raw),
        bench_permissioned_signal_wait(*edge, signaler, waiter),
        bench_typed_one_shot(*edge, signaler, waiter),
    };

    bench::emit_reports_text(reports);

    std::printf("\n=== ChainEdgeSession deltas ===\n");
    bench::Compare cmps[] = {
        bench::compare(reports[0], reports[1]),
        bench::compare(reports[0], reports[2]),
    };
    for (const auto& c : cmps) c.print_text(stdout);

    std::printf("\n=== verdict (TIER A — structural) ===\n");
    std::printf("  PermissionedChainEdge handles are pointer-sized.\n");
    std::printf("  PSH<End, EmptyPermSet, Handle*> equals bare SessionHandle size.\n");
    std::printf("  Timed numbers exercise CPU-oracle semaphore stubs only.\n");

    if (json) bench::emit_reports_json(reports, json);
    return 0;
}

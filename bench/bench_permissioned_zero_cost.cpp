// Permissioned channel zero-cost validation bench.
//
// Compares the permissioned MPSC channel of fixy/concurrent against its
// bare MpscRing on the hot-path operations try_push and try_pop, with
// the harness's bench::compare primitive (Mann-Whitney U test +
// Δp50 / Δp99 / Δμ percentages, with [REGRESS] / [IMPROVE] /
// [indistinguishable] flagging at the 1% significance level).
//
// The zero-cost claim from THREADING.md §5.5 / 27_04 §5.3:
//   sizeof(PermissionedFooHandle) == sizeof(Channel*)  via EBO
//   The handle's hot-path call inlines straight through to the
//   underlying primitive.  No additional atomic, no additional branch,
//   no additional memory access.  Hot-path codegen identical.
//
// This bench is the empirical witness of that claim.  PASS = every
// pair is statistically [indistinguishable] (|z| ≤ 2.576 from Mann-
// Whitney U) OR shows ≤ 5 % Δp99.  FAIL = any pair shows [REGRESS]
// flag at > 5 % Δp99 with distinguishable z.
//
// The pairs are the permissioned MPSC channel and its bare ring.  The
// permissioned SPSC channel has its own bench, bench_spsc_session.cpp.
//
// Methodology:
//   1. For each (bare, wrapped) pair, allocate fresh ring + handle.
//   2. Run bench::run on bare hot-path; same on wrapped hot-path.
//   3. bench::compare emits the structured delta + flag.
//   4. Final exit code: 0 iff every pair is non-REGRESS.
//
// Notes on scope:
//   - Single-threaded only.  Multi-thread Permissioned bench is a
//     separate (heavier) artifact — this one is the per-call
//     overhead claim.
//   - Pop-side benches pre-fill enough items to never hit empty
//     during measurement.
//   - The setup splits one root into two tokens.  The pool of the
//     channel takes the producer token, and the consumer handle takes
//     the consumer token.

#include <cstdint>
#include <utility>

#include <fixy/Core.h>
#include <fixy/channels/Endpoint.h>
#include <fixy/concurrent/MpscRing.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>

#include "bench_harness.h"

namespace {

using Item = std::uint64_t;
constexpr std::size_t kCap = 1U << 20;  // 1M slots — never fills

// The UserTag keeps the Permission tree of this bench distinct from any
// other channel in the process.
struct MpscBenchTag {};

auto bench_root() noexcept {
    return ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::merge, MpscBenchTag>>();
}

using Channel = ::fixy::concurrent::channel_t<Item, kCap, decltype(bench_root())>;

// The two halves of the root: the producer token for the pool and the
// consumer token for the consumer handle.
auto bench_halves() noexcept {
    return ::foundation::permissions::mint_permission_split<Channel::producer_tag, Channel::consumer_tag>(bench_root());
}

// ─────────────────────────────────────────────────────────────────────
// MpscRing pair
// ─────────────────────────────────────────────────────────────────────

// The allocation capability of the rings and the channels of the rows.
constexpr ::foundation::effects::Alloc kAlloc = ::foundation::effects::testing::test().alloc;

// The owner of the bare ring.  It makes the proof of the one consumer (THE
// WRITERS of fixy/concurrent/MpscRing.h), which pops on the thread of the row.
struct BareRingOwner final {
    [[nodiscard]] static constexpr ::fixy::SoleWriterProof<BareRingOwner, ::fixy::concurrent::MpscConsumerRole>
    consumer() noexcept {
        return {};
    }
};

using BareRing = ::fixy::concurrent::MpscRing<Item, kCap, BareRingOwner>;

bench::Report bare_mpsc_push() {
    ::fixy::Box<BareRing> ring_box = ::fixy::mint_box<BareRing>(kAlloc);
    auto const ring = ::fixy::to(*ring_box);
    Item i = 0;
    return bench::run("bare MpscRing.try_push", [&] {
        const bool ok = ring->try_push(++i);
        bench::do_not_optimize(ok);
    });
}

bench::Report wrapped_mpsc_push() {
    auto [producer_root, consumer_perm] = bench_halves();
    (void)consumer_perm;
    ::fixy::Box<Channel> ch = ::fixy::mint_box<Channel>(kAlloc, std::move(producer_root));
    auto p_opt = ch->producer();
    CRUCIBLE_BENCH_CHECK(p_opt.is_some());
    auto p = std::move(p_opt).expect("the test above found a producer");
    Item i = 0;
    return bench::run("wrapped Permissioned MPSC.ProducerHandle::try_push", [&] {
        const bool ok = p.try_push(++i);
        bench::do_not_optimize(ok);
    });
}

bench::Report bare_mpsc_pop() {
    ::fixy::Box<BareRing> ring_box = ::fixy::mint_box<BareRing>(kAlloc);
    auto const ring = ::fixy::to(*ring_box);
    for (Item i = 0; i < kCap / 2; ++i)
        (void)ring->try_push(i);
    // A pop of an MPSC ring is a merge, so it takes the tag of Nondet.
    const ::foundation::effects::cap::Nondet merge = ::foundation::effects::testing::test().nondet;
    return bench::run("bare MpscRing.try_pop", [&] {
        auto v = ring->try_pop(BareRingOwner::consumer(), merge);
        bench::do_not_optimize(v);
    });
}

bench::Report wrapped_mpsc_pop() {
    auto [producer_root, consumer_perm] = bench_halves();
    ::fixy::Box<Channel> ch = ::fixy::mint_box<Channel>(kAlloc, std::move(producer_root));
    {
        auto p_opt = ch->producer();
        CRUCIBLE_BENCH_CHECK(p_opt.is_some());
        auto p = std::move(p_opt).expect("the test above found a producer");
        for (Item i = 0; i < kCap / 2; ++i)
            (void)p.try_push(i);
    }
    auto c = ch->consumer(std::move(consumer_perm));
    // A pop of an MPSC channel is a merge, so it takes the tag of Nondet.
    const ::foundation::effects::cap::Nondet merge = ::foundation::effects::testing::test().nondet;
    return bench::run("wrapped Permissioned MPSC.ConsumerHandle::try_pop", [&] {
        auto v = c.try_pop(merge);
        bench::do_not_optimize(v);
    });
}

// A context that holds Nondet and no other atom: the context of an endpoint
// whose pop is a merge.
using MergeCtx = ::foundation::effects::ExecCtx<::foundation::effects::Test,
                                                ::foundation::effects::Row<::foundation::effects::Effect::Nondet>>;

// The door that ends the closure of a round, with the linear handle that the
// closure owns.  The closure is linear by its capture, and the drop door of
// foundation/Linear.h cannot read a capture.
template <class Round>
[[= ::foundation::linear::drop_door{}]] void end_round(Round round) noexcept {
    static_cast<void>(round);
}

// The hot pop through the consumer handle.  Each round pushes one item and
// pops it, so the ring holds at most one item and each pop finds one.  The
// closure owns the consumer handle, the harness borrows the closure, and
// end_round ends it.
bench::Report handle_mpsc_round_trip() {
    auto [producer_root, consumer_perm] = bench_halves();
    const ::foundation::effects::Alloc alloc = ::foundation::effects::testing::test().alloc;
    ::fixy::Box<Channel> ch = ::fixy::mint_box<Channel>(alloc, std::move(producer_root));
    auto p_opt = ch->producer();
    CRUCIBLE_BENCH_CHECK(p_opt.is_some());
    auto p = std::move(p_opt).expect("the test above found a producer");
    const ::foundation::effects::cap::Nondet merge = ::foundation::effects::testing::test().nondet;
    Item i = 0;
    auto round = [&p, &i, merge, c = ch->consumer(std::move(consumer_perm))]() mutable {
        const bool pushed = p.try_push(++i);
        auto v = c.try_pop(merge);
        bench::do_not_optimize(pushed);
        bench::do_not_optimize(v);
    };
    bench::Report report = bench::run("MPSC push, then ConsumerHandle::try_pop", round);
    end_round(std::move(round));
    return report;
}

// The same round through an endpoint, whose pop takes the tag of Nondet
// from the context of the endpoint.  The closure owns the endpoint, and
// end_round ends it.
bench::Report endpoint_mpsc_round_trip() {
    auto [producer_root, consumer_perm] = bench_halves();
    const ::foundation::effects::Alloc alloc = ::foundation::effects::testing::test().alloc;
    ::fixy::Box<Channel> ch = ::fixy::mint_box<Channel>(alloc, std::move(producer_root));
    auto p_opt = ch->producer();
    CRUCIBLE_BENCH_CHECK(p_opt.is_some());
    auto p = std::move(p_opt).expect("the test above found a producer");
    Item i = 0;
    auto round =
        [&p, &i,
         endpoint = ::fixy::channels::mint_endpoint<Channel, ::fixy::channels::Direction::Consumer>(
             MergeCtx{::foundation::effects::testing::test()}, ch->consumer(std::move(consumer_perm)))]() mutable {
            const bool pushed = p.try_push(++i);
            auto v = endpoint.try_recv();
            bench::do_not_optimize(pushed);
            bench::do_not_optimize(v);
        };
    bench::Report report = bench::run("MPSC push, then Endpoint::try_recv", round);
    end_round(std::move(round));
    return report;
}

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();

    const bench::JsonTail json = bench::env_json();

    using ::fixy::Fixed;
    bench::print("=== permissioned_zero_cost ===\n");
    bench::print("  Item:    uint64_t (8 bytes)\n");
    bench::print("  Cap:     {} slots (1M — never fills)\n", kCap);
    bench::print("  Method:  bare vs wrapped per primitive,\n");
    bench::print("           bench::compare with Mann-Whitney U test\n");
    bench::print("  PASS:    no pair flagged [REGRESS] (Δp99 > {}% with p < 0.01)\n\n",
                 Fixed<0>{bench::Compare::kFlagDeltaP99Pct});

    ::fixy::FixedArray<bench::Report, 6> reports{
        // 0..1   MPSC push
        bare_mpsc_push(),
        wrapped_mpsc_push(),
        // 2..3   MPSC pop
        bare_mpsc_pop(),
        wrapped_mpsc_pop(),
        // 4..5   MPSC push, then pop
        handle_mpsc_round_trip(),
        endpoint_mpsc_round_trip(),
    };

    bench::emit_reports_text(reports.view());

    // ── Pair-by-pair comparison ───────────────────────────────────────
    bench::print("\n=== zero-cost claim — pair deltas ===\n");
    ::fixy::FixedArray<bench::Compare, 3> const compares{
        bench::compare(reports.at(::fixy::index<0>()), reports.at(::fixy::index<1>())),
        bench::compare(reports.at(::fixy::index<2>()), reports.at(::fixy::index<3>())),
        bench::compare(reports.at(::fixy::index<4>()), reports.at(::fixy::index<5>())),
    };
    bench::emit_compares(compares.view());

    // ── Headline verdict ──────────────────────────────────────────────
    bench::print("\n=== verdict ===\n");
    int regressions = 0;
    for (const auto& c : compares) {
        if (c.is_regression()) {
            bench::print("  REGRESS: {} → {}  Δp99={}%\n", c.a_name.view(), c.b_name.view(),
                         Fixed<2, 0, ::fixy::Align::Right, ::fixy::SignMark::Always>{c.delta_p99_pct});
            ++regressions;
        }
    }
    if (regressions == 0) {
        bench::print("  PASS — no permissioned MPSC pair is slower than\n");
        bench::print("         its bare counterpart by more than {}% Δp99.\n",
                     Fixed<0>{bench::Compare::kFlagDeltaP99Pct});
    } else {
        bench::print("  FAIL — {} wrapper(s) regressed beyond {}% Δp99.\n", regressions,
                     Fixed<0>{bench::Compare::kFlagDeltaP99Pct});
    }

    bench::emit_reports_json(reports.view(), json);
    return regressions;  // exit code = number of regressions
}

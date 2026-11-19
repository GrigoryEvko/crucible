#include <crucible/TraceRing.h>
#include <fixy/Aliases.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Platform.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <atomic>
#include <cstdio>
#include <thread>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::SchemaHash;
using crucible::ShapeHash;
using crucible::ScopeHash;
using crucible::CallsiteHash;
using crucible::MetaIndex;
using ::fixy::index;
namespace eff = ::fixy;

namespace {

// A context whose row is empty, which the pure gates admit.  The gates read
// the row of the context, so a context that holds Test, Alloc, IO, Block or
// Nondet does not pass.
constexpr ::fixy::HotFgCtx kPure = ::foundation::effects::testing::foreground();

template <typename Ctx>
concept AppendsPure = requires(crucible::TraceRing& ring, Ctx const& ctx, crucible::TraceRing::Entry const& entry) {
    ring.try_append_pure(ctx, entry);
};
template <typename Ctx>
concept DrainsPure = requires(crucible::TraceRing& ring, Ctx const& ctx, ::fixy::View<crucible::TraceRing::Entry> out) {
    ring.drain_pure(ctx, out);
};
static_assert(AppendsPure<::fixy::HotFgCtx>);
static_assert(DrainsPure<::fixy::HotFgCtx>);
static_assert(!AppendsPure<::fixy::TestRunnerCtx>);
static_assert(!DrainsPure<::fixy::TestRunnerCtx>);
static_assert(!AppendsPure<::fixy::BgDrainCtx>);
static_assert(!DrainsPure<::fixy::BgDrainCtx>);

// Each function holds one group of cases, so that no function of the test
// is larger than the limit of the kind function of the rule object-size.
void single_thread_cases() {
    auto* ring = new crucible::TraceRing();

    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == 0);

    crucible::TraceRing::Entry e{};
    e.schema_hash = SchemaHash{0xDEADBEEF};
    e.num_inputs = 2;
    e.num_outputs = 1;
    CRUCIBLE_TEST_ASSERT(kTest, ring->try_append(e, MetaIndex{42}, ScopeHash{0x1234}, CallsiteHash{0x5678}));
    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == 1);

    ::fixy::FixedArray<crucible::TraceRing::Entry, 1> out{};
    ::fixy::FixedArray<MetaIndex, 1> meta{};
    ::fixy::FixedArray<ScopeHash, 1> scope{};
    ::fixy::FixedArray<CallsiteHash, 1> callsite{};
    uint32_t n = ring->drain(out.view(), meta.view(), scope.view(), callsite.view());
    CRUCIBLE_TEST_ASSERT(kTest, n == 1);
    CRUCIBLE_TEST_ASSERT(kTest, out.at(index<0>()).schema_hash == SchemaHash{0xDEADBEEF});
    CRUCIBLE_TEST_ASSERT(kTest, out.at(index<0>()).num_inputs == 2);
    CRUCIBLE_TEST_ASSERT(kTest, out.at(index<0>()).num_outputs == 1);
    CRUCIBLE_TEST_ASSERT(kTest, meta.at(index<0>()) == MetaIndex{42});
    CRUCIBLE_TEST_ASSERT(kTest, scope.at(index<0>()) == ScopeHash{0x1234});
    CRUCIBLE_TEST_ASSERT(kTest, callsite.at(index<0>()) == CallsiteHash{0x5678});
    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == 0);

    for (uint32_t i = 0; i < 1000; i++) {
        crucible::TraceRing::Entry entry{};
        entry.schema_hash = SchemaHash{i};
        CRUCIBLE_TEST_ASSERT(kTest, ring->try_append(entry));
    }
    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == 1000);

    ::fixy::FixedArray<crucible::TraceRing::Entry, 2048> batch{};
    n = ring->drain(batch.view());
    CRUCIBLE_TEST_ASSERT(kTest, n == 1000);
    for (uint32_t i = 0; i < 1000; i++)
        CRUCIBLE_TEST_ASSERT(
            kTest, batch.get(i).expect("a test reads an element at or past the count of its array")->schema_hash
                       == SchemaHash{i});

    ring->reset();
    CRUCIBLE_TEST_ASSERT(kTest, ring->size().peek() == 0);

    // One cache line.
    static_assert(sizeof(crucible::TraceRing::Entry) == 64);

    // An empty output drains nothing.
    uint32_t zero = ring->drain(::fixy::View<crucible::TraceRing::Entry>{});
    CRUCIBLE_TEST_ASSERT(kTest, zero == 0);

    // Wrap-around: drive head/tail past CAPACITY so drain() exercises the
    // two-segment memcpy path.
    {
        constexpr uint32_t CAP = crucible::TraceRing::CAPACITY;
        for (uint32_t i = 0; i < CAP - 100; i++) {
            crucible::TraceRing::Entry wr{};
            wr.schema_hash = SchemaHash{i};
            CRUCIBLE_TEST_ASSERT(kTest, ring->try_append(wr));
        }
        // A buffer of the whole ring is too large for the stack.
        static ::fixy::FixedArray<crucible::TraceRing::Entry, CAP> big{};
        CRUCIBLE_TEST_ASSERT(kTest, ring->drain(big.view()) == CAP - 100);

        // head = tail = CAP - 100, so the next 200 appends straddle the wrap.
        for (uint32_t i = 0; i < 200; i++) {
            crucible::TraceRing::Entry wr{};
            wr.schema_hash = SchemaHash{100000 + i};
            CRUCIBLE_TEST_ASSERT(kTest, ring->try_append(wr));
        }
        uint32_t n_wrap = ring->drain(big.view());
        CRUCIBLE_TEST_ASSERT(kTest, n_wrap == 200);
        for (uint32_t i = 0; i < 200; i++)
            CRUCIBLE_TEST_ASSERT(
                kTest, big.get(i).expect("a test reads an element at or past the count of its array")->schema_hash
                           == SchemaHash{100000 + i});
    }

    uint64_t hi0 = ring->total_produced();
    crucible::TraceRing::Entry hm{};
    hm.schema_hash = SchemaHash{0xABCD};
    CRUCIBLE_TEST_ASSERT(kTest, ring->try_append(hm));
    CRUCIBLE_TEST_ASSERT(kTest, ring->total_produced() == hi0 + 1);

    delete ring;
}

// Producer and consumer exchange N entries live.  On x86 a wrongly
// relaxed publish store would pass by accident, so the ordering claim
// made here only bites on a weakly ordered target.
void concurrent_raw_case() {
    {
        constexpr uint32_t N = 200000;
        auto* r = new crucible::TraceRing();

        std::atomic<bool> producer_done{false};
        std::atomic<uint32_t> producer_spins{0};

        std::jthread producer{[&] {
            for (uint32_t i = 0; i < N; /* advance only on success */) {
                crucible::TraceRing::Entry entry{};
                entry.schema_hash = SchemaHash{i + 1};  // non-zero, unique
                entry.shape_hash = ShapeHash{~uint64_t{i} + 1};
                if (r->try_append(entry, MetaIndex{i}, ScopeHash{0xA0000000ull | i}, CallsiteHash{0xB0000000ull | i}))
                    [[likely]] {
                    ++i;
                } else {
                    CRUCIBLE_SPIN_PAUSE;
                    producer_spins.fetch_add(1, std::memory_order_relaxed);
                }
            }
            producer_done.store(true, std::memory_order_release);
        }};

        std::jthread consumer{[&] {
            // Smaller than CAPACITY so the consumer must drain more than once.
            constexpr uint32_t DRAIN_CAP = 4096;
            ::fixy::FixedArray<crucible::TraceRing::Entry, DRAIN_CAP> batch_out{};
            ::fixy::FixedArray<MetaIndex, DRAIN_CAP> batch_meta{};
            ::fixy::FixedArray<ScopeHash, DRAIN_CAP> batch_scope{};
            ::fixy::FixedArray<CallsiteHash, DRAIN_CAP> batch_csite{};

            uint32_t next = 0;
            while (next < N) {
                uint32_t got = r->drain(batch_out.view(), batch_meta.view(), batch_scope.view(), batch_csite.view());
                if (got == 0) {
                    if (producer_done.load(std::memory_order_acquire) && r->size().peek() == 0) {
                        break;
                    }
                    CRUCIBLE_SPIN_PAUSE;
                    continue;
                }
                for (uint32_t k = 0; k < got; ++k) {
                    const uint32_t seq = next + k;
                    CRUCIBLE_TEST_ASSERT(kTest,
                                         batch_out.get(k)
                                                 .expect("a test reads an element at or past the count of its array")
                                                 ->schema_hash
                                             == SchemaHash{seq + 1});
                    CRUCIBLE_TEST_ASSERT(
                        kTest,
                        batch_out.get(k).expect("a test reads an element at or past the count of its array")->shape_hash
                            == ShapeHash{~uint64_t{seq} + 1});
                    CRUCIBLE_TEST_ASSERT(
                        kTest, *batch_meta.get(k).expect("a test reads an element at or past the count of its array")
                                   == MetaIndex{seq});
                    CRUCIBLE_TEST_ASSERT(
                        kTest, *batch_scope.get(k).expect("a test reads an element at or past the count of its array")
                                   == ScopeHash{0xA0000000ull | seq});
                    CRUCIBLE_TEST_ASSERT(
                        kTest, *batch_csite.get(k).expect("a test reads an element at or past the count of its array")
                                   == CallsiteHash{0xB0000000ull | seq});
                }
                next += got;
            }
            CRUCIBLE_TEST_ASSERT(kTest, next == N);
        }};

        producer.join();
        consumer.join();
        delete r;

        ::fixy::check::pass(kTest,
                            "test_trace_ring: concurrent SPSC integrity "
                            "(N={}, producer_spins={}) OK\n",
                            N, producer_spins.load());
    }
}

void pure_facade_cases() {
    {
        auto* r = new crucible::TraceRing();

        crucible::TraceRing::Entry e1{};
        e1.schema_hash = SchemaHash{0xCAFE0001};
        CRUCIBLE_TEST_ASSERT(kTest, r->try_append_pure(kPure, e1, MetaIndex{1}, ScopeHash{0xC1}, CallsiteHash{0xD1}));
        CRUCIBLE_TEST_ASSERT(kTest, r->size().peek() == 1);

        crucible::TraceRing::Entry e2{};
        e2.schema_hash = SchemaHash{0xCAFE0002};
        CRUCIBLE_TEST_ASSERT(kTest, r->try_append_pure(kPure, e2, MetaIndex{2}, ScopeHash{0xC2}, CallsiteHash{0xD2}));
        CRUCIBLE_TEST_ASSERT(kTest, r->size().peek() == 2);

        crucible::TraceRing::Entry e3{};
        e3.schema_hash = SchemaHash{0xCAFE0003};
        CRUCIBLE_TEST_ASSERT(kTest, r->try_append(e3, MetaIndex{3}, ScopeHash{0xC3}, CallsiteHash{0xD3}));
        crucible::TraceRing::Entry e4{};
        e4.schema_hash = SchemaHash{0xCAFE0004};
        CRUCIBLE_TEST_ASSERT(kTest, r->try_append_pure(kPure, e4, MetaIndex{4}, ScopeHash{0xC4}, CallsiteHash{0xD4}));
        CRUCIBLE_TEST_ASSERT(kTest, r->size().peek() == 4);

        {
            ::fixy::FixedArray<crucible::TraceRing::Entry, 4> batch_out{};
            ::fixy::FixedArray<MetaIndex, 4> batch_meta{};
            ::fixy::FixedArray<ScopeHash, 4> batch_scope{};
            ::fixy::FixedArray<CallsiteHash, 4> batch_csite{};
            uint32_t got =
                r->drain_pure(kPure, batch_out.view(), batch_meta.view(), batch_scope.view(), batch_csite.view());
            CRUCIBLE_TEST_ASSERT(kTest, got == 4);
            CRUCIBLE_TEST_ASSERT(kTest, batch_out.at(index<0>()).schema_hash == SchemaHash{0xCAFE0001});
            CRUCIBLE_TEST_ASSERT(kTest, batch_out.at(index<1>()).schema_hash == SchemaHash{0xCAFE0002});
            CRUCIBLE_TEST_ASSERT(kTest, batch_out.at(index<2>()).schema_hash == SchemaHash{0xCAFE0003});
            CRUCIBLE_TEST_ASSERT(kTest, batch_out.at(index<3>()).schema_hash == SchemaHash{0xCAFE0004});
            CRUCIBLE_TEST_ASSERT(kTest, batch_meta.at(index<0>()) == MetaIndex{1}
                                            && batch_meta.at(index<3>()) == MetaIndex{4});
            CRUCIBLE_TEST_ASSERT(kTest, batch_scope.at(index<0>()) == ScopeHash{0xC1}
                                            && batch_scope.at(index<3>()) == ScopeHash{0xC4});
            CRUCIBLE_TEST_ASSERT(kTest, batch_csite.at(index<0>()) == CallsiteHash{0xD1}
                                            && batch_csite.at(index<3>()) == CallsiteHash{0xD4});
            CRUCIBLE_TEST_ASSERT(kTest, r->size().peek() == 0);
        }

        {
            uint32_t zero_drained = r->drain_pure(kPure, ::fixy::View<crucible::TraceRing::Entry>{});
            CRUCIBLE_TEST_ASSERT(kTest, zero_drained == 0);
        }

        {
            crucible::TraceRing::Entry e5{};
            e5.schema_hash = SchemaHash{0xCAFE0005};
            CRUCIBLE_TEST_ASSERT(kTest, r->try_append_pure(kPure, e5));

            crucible::TraceRing::Entry e6{};
            e6.schema_hash = SchemaHash{0xCAFE0006};
            CRUCIBLE_TEST_ASSERT(kTest, r->try_append(e6));

            ::fixy::FixedArray<crucible::TraceRing::Entry, 1> one{};
            uint32_t g1 = r->drain_pure(kPure, one.view());
            CRUCIBLE_TEST_ASSERT(kTest, g1 == 1);
            CRUCIBLE_TEST_ASSERT(kTest, one.at(index<0>()).schema_hash == SchemaHash{0xCAFE0005});

            uint32_t g2 = r->drain(one.view());
            CRUCIBLE_TEST_ASSERT(kTest, g2 == 1);
            CRUCIBLE_TEST_ASSERT(kTest, one.at(index<0>()).schema_hash == SchemaHash{0xCAFE0006});
        }

        static_assert(eff::IsPure<eff::Row<>>);
        static_assert(eff::IsPure<eff::PureRow>);
        static_assert(!eff::IsPure<eff::DivRow>);  // Div
        static_assert(!eff::IsPure<eff::Row<eff::Effect::IO>>);
        static_assert(!eff::IsPure<eff::Row<eff::Effect::Bg>>);
        static_assert(!eff::IsPure<eff::Row<eff::Effect::Alloc>>);
        static_assert(!eff::IsPure<eff::AllRow>);  // saturation top
        static_assert(!eff::IsPure<eff::Row<eff::Effect::IO,
                                            eff::Effect::Block>>);  // multi-atom

        delete r;
        ::fixy::check::pass(kTest, "test_trace_ring: try_append_pure / drain_pure OK\n");
    }
}

// The same exchange driven through the row-typed facades, to show the
// wrapper does not change the ordering the raw API provides.
void concurrent_pure_case() {
    {
        constexpr uint32_t N = 100000;
        auto* r = new crucible::TraceRing();

        std::atomic<bool> producer_done{false};
        std::atomic<uint32_t> producer_spins{0};

        std::jthread producer{[&] {
            for (uint32_t i = 0; i < N; /* advance only on success */) {
                crucible::TraceRing::Entry entry{};
                entry.schema_hash = SchemaHash{i + 7};
                entry.shape_hash = ShapeHash{~uint64_t{i} + 7};
                if (r->try_append_pure(kPure, entry, MetaIndex{i + 1}, ScopeHash{0xE0000000ull | i},
                                       CallsiteHash{0xF0000000ull | i})) [[likely]] {
                    ++i;
                } else {
                    CRUCIBLE_SPIN_PAUSE;
                    producer_spins.fetch_add(1, std::memory_order_relaxed);
                }
            }
            producer_done.store(true, std::memory_order_release);
        }};

        std::jthread consumer{[&] {
            constexpr uint32_t DRAIN_CAP = 2048;
            ::fixy::FixedArray<crucible::TraceRing::Entry, DRAIN_CAP> pure_out{};
            ::fixy::FixedArray<MetaIndex, DRAIN_CAP> pure_meta{};
            ::fixy::FixedArray<ScopeHash, DRAIN_CAP> pure_scope{};
            ::fixy::FixedArray<CallsiteHash, DRAIN_CAP> pure_csite{};

            uint32_t next = 0;
            while (next < N) {
                uint32_t got =
                    r->drain_pure(kPure, pure_out.view(), pure_meta.view(), pure_scope.view(), pure_csite.view());
                if (got == 0) {
                    if (producer_done.load(std::memory_order_acquire) && r->size().peek() == 0) {
                        break;
                    }
                    CRUCIBLE_SPIN_PAUSE;
                    continue;
                }
                for (uint32_t k = 0; k < got; ++k) {
                    const uint32_t seq = next + k;
                    CRUCIBLE_TEST_ASSERT(
                        kTest,
                        pure_out.get(k).expect("a test reads an element at or past the count of its array")->schema_hash
                            == SchemaHash{seq + 7});
                    CRUCIBLE_TEST_ASSERT(
                        kTest,
                        pure_out.get(k).expect("a test reads an element at or past the count of its array")->shape_hash
                            == ShapeHash{~uint64_t{seq} + 7});
                    CRUCIBLE_TEST_ASSERT(
                        kTest, *pure_meta.get(k).expect("a test reads an element at or past the count of its array")
                                   == MetaIndex{seq + 1});
                    CRUCIBLE_TEST_ASSERT(
                        kTest, *pure_scope.get(k).expect("a test reads an element at or past the count of its array")
                                   == ScopeHash{0xE0000000ull | seq});
                    CRUCIBLE_TEST_ASSERT(
                        kTest, *pure_csite.get(k).expect("a test reads an element at or past the count of its array")
                                   == CallsiteHash{0xF0000000ull | seq});
                }
                next += got;
            }
            CRUCIBLE_TEST_ASSERT(kTest, next == N);
        }};

        producer.join();
        consumer.join();
        delete r;

        ::fixy::check::pass(kTest,
                            "test_trace_ring: try_append_pure/drain_pure concurrent "
                            "(N={}, producer_spins={}) OK\n",
                            N, producer_spins.load());
    }
}

void pure_edge_cases() {
    {
        constexpr uint32_t CAP = crucible::TraceRing::CAPACITY;
        auto* r = new crucible::TraceRing();

        {
            crucible::TraceRing::Entry e_bare{};
            e_bare.schema_hash = SchemaHash{0xA0AA};
            CRUCIBLE_TEST_ASSERT(kTest, r->try_append_pure(kPure, e_bare));
            CRUCIBLE_TEST_ASSERT(kTest, r->size().peek() == 1);

            ::fixy::FixedArray<crucible::TraceRing::Entry, 1> one_a{};
            uint32_t got_a = r->drain_pure(kPure, one_a.view());
            CRUCIBLE_TEST_ASSERT(kTest, got_a == 1);
            CRUCIBLE_TEST_ASSERT(kTest, one_a.at(index<0>()).schema_hash == SchemaHash{0xA0AA});
            CRUCIBLE_TEST_ASSERT(kTest, r->size().peek() == 0);
        }

        // A wrapper that always returned true on a full ring would slip past
        // the concurrent test above: the spin counter would merely read low.
        // Filling the ring exactly makes the refusal definite.
        {
            for (uint32_t i = 0; i < CAP; ++i) {
                crucible::TraceRing::Entry full_e{};
                full_e.schema_hash = SchemaHash{0xB0000000u | i};
                bool ok = r->try_append_pure(kPure, full_e);
                CRUCIBLE_TEST_ASSERT(kTest, ok && "ring should accept CAPACITY entries");
            }
            CRUCIBLE_TEST_ASSERT(kTest, r->size().peek() == CAP);

            crucible::TraceRing::Entry overflow_e{};
            overflow_e.schema_hash = SchemaHash{0xDEADBEEF};
            bool ok_overflow = r->try_append_pure(kPure, overflow_e);
            CRUCIBLE_TEST_ASSERT(kTest, !ok_overflow);
            CRUCIBLE_TEST_ASSERT(kTest, r->size().peek() == CAP);

            // A buffer of the whole ring is too large for the stack.
            static ::fixy::FixedArray<crucible::TraceRing::Entry, CAP> full_drain{};
            uint32_t got_b = r->drain_pure(kPure, full_drain.view());
            CRUCIBLE_TEST_ASSERT(kTest, got_b == CAP);
            for (uint32_t i = 0; i < CAP; ++i) {
                CRUCIBLE_TEST_ASSERT(
                    kTest,
                    full_drain.get(i).expect("a test reads an element at or past the count of its array")->schema_hash
                        == SchemaHash{0xB0000000u | i});
            }
            CRUCIBLE_TEST_ASSERT(kTest, r->size().peek() == 0);
        }

        {
            // head and tail reach CAP - 100, so the next 200 appends straddle
            // the slot wrap and drain must copy two segments.
            for (uint32_t i = 0; i < CAP - 100; ++i) {
                crucible::TraceRing::Entry pad_e{};
                pad_e.schema_hash = SchemaHash{0xC0000000u | i};
                CRUCIBLE_TEST_ASSERT(kTest, r->try_append_pure(kPure, pad_e));
            }
            static ::fixy::FixedArray<crucible::TraceRing::Entry, CAP> pad_drain{};
            uint32_t got_pad = r->drain_pure(kPure, pad_drain.view());
            CRUCIBLE_TEST_ASSERT(kTest, got_pad == CAP - 100);

            for (uint32_t i = 0; i < 200; ++i) {
                crucible::TraceRing::Entry wrap_e{};
                wrap_e.schema_hash = SchemaHash{0xCAFE0000u | i};
                CRUCIBLE_TEST_ASSERT(kTest, r->try_append_pure(kPure, wrap_e));
            }

            static ::fixy::FixedArray<crucible::TraceRing::Entry, CAP> wrap_drain{};
            static ::fixy::FixedArray<MetaIndex, CAP> wrap_meta{};
            static ::fixy::FixedArray<ScopeHash, CAP> wrap_scope{};
            static ::fixy::FixedArray<CallsiteHash, CAP> wrap_csite{};
            uint32_t got_wrap =
                r->drain_pure(kPure, wrap_drain.view(), wrap_meta.view(), wrap_scope.view(), wrap_csite.view());
            CRUCIBLE_TEST_ASSERT(kTest, got_wrap == 200);
            for (uint32_t i = 0; i < 200; ++i) {
                CRUCIBLE_TEST_ASSERT(
                    kTest,
                    wrap_drain.get(i).expect("a test reads an element at or past the count of its array")->schema_hash
                        == SchemaHash{0xCAFE0000u | i});
                CRUCIBLE_TEST_ASSERT(
                    kTest, *wrap_meta.get(i).expect("a test reads an element at or past the count of its array")
                               == MetaIndex::none());
                CRUCIBLE_TEST_ASSERT(
                    kTest, *wrap_scope.get(i).expect("a test reads an element at or past the count of its array")
                               == ScopeHash{});
                CRUCIBLE_TEST_ASSERT(
                    kTest, *wrap_csite.get(i).expect("a test reads an element at or past the count of its array")
                               == CallsiteHash{});
            }
        }

        // Each parallel output is optional on its own: an empty View skips it.
        {
            crucible::TraceRing::Entry mix_e{};
            mix_e.schema_hash = SchemaHash{0xD0D0};
            CRUCIBLE_TEST_ASSERT(
                kTest, r->try_append_pure(kPure, mix_e, MetaIndex{42}, ScopeHash{0xDEAD}, CallsiteHash{0xBEEF}));

            ::fixy::FixedArray<crucible::TraceRing::Entry, 1> mix_out{};
            ::fixy::FixedArray<MetaIndex, 1> mix_meta{};
            ::fixy::FixedArray<CallsiteHash, 1> mix_csite{};
            uint32_t got_mix = r->drain_pure(kPure, mix_out.view(), mix_meta.view(),
                                             /*out_scope_hashes=*/::fixy::View<ScopeHash>{}, mix_csite.view());
            CRUCIBLE_TEST_ASSERT(kTest, got_mix == 1);
            CRUCIBLE_TEST_ASSERT(kTest, mix_out.at(index<0>()).schema_hash == SchemaHash{0xD0D0});
            CRUCIBLE_TEST_ASSERT(kTest, mix_meta.at(index<0>()) == MetaIndex{42});
            CRUCIBLE_TEST_ASSERT(kTest, mix_csite.at(index<0>()) == CallsiteHash{0xBEEF});

            crucible::TraceRing::Entry mix_e2{};
            mix_e2.schema_hash = SchemaHash{0xD2D2};
            CRUCIBLE_TEST_ASSERT(
                kTest, r->try_append_pure(kPure, mix_e2, MetaIndex{99}, ScopeHash{0xCAFE}, CallsiteHash{0xF00D}));

            ::fixy::FixedArray<crucible::TraceRing::Entry, 1> mix_out2{};
            ::fixy::FixedArray<ScopeHash, 1> mix_scope2{};
            uint32_t got_mix2 = r->drain_pure(kPure, mix_out2.view(),
                                              /*out_meta_starts=*/::fixy::View<MetaIndex>{}, mix_scope2.view(),
                                              /*out_callsite_hashes=*/::fixy::View<CallsiteHash>{});
            CRUCIBLE_TEST_ASSERT(kTest, got_mix2 == 1);
            CRUCIBLE_TEST_ASSERT(kTest, mix_out2.at(index<0>()).schema_hash == SchemaHash{0xD2D2});
            CRUCIBLE_TEST_ASSERT(kTest, mix_scope2.at(index<0>()) == ScopeHash{0xCAFE});
            CRUCIBLE_TEST_ASSERT(kTest, r->size().peek() == 0);
        }

        delete r;
        ::fixy::check::pass(kTest, "test_trace_ring: try_append_pure/drain_pure edge cases "
                                   "(bare default + full ring + wrap-around + "
                                   "nullable outputs) OK\n");
    }
}

}  // namespace

int main() {
    single_thread_cases();
    concurrent_raw_case();
    pure_facade_cases();
    concurrent_pure_case();
    pure_edge_cases();
    ::fixy::check::pass(kTest, "test_trace_ring: all tests passed\n");
    return 0;
}

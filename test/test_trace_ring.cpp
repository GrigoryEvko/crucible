#include <crucible/Platform.h>
#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>

#include <atomic>
#include "test_assert.h"
#include <cstdio>
#include <thread>
#include <vector>

using crucible::SchemaHash;
using crucible::ShapeHash;
using crucible::ScopeHash;
using crucible::CallsiteHash;
using crucible::MetaIndex;
namespace eff = ::crucible::effects;

int main() {
    auto* ring = new crucible::TraceRing();

    assert(ring->size().peek() == 0);

    crucible::TraceRing::Entry e{};
    e.schema_hash = SchemaHash{0xDEADBEEF};
    e.num_inputs = 2;
    e.num_outputs = 1;
    assert(ring->try_append(e, MetaIndex{42}, ScopeHash{0x1234}, CallsiteHash{0x5678}));
    assert(ring->size().peek() == 1);

    crucible::TraceRing::Entry out[1];
    MetaIndex meta[1];
    ScopeHash scope[1];
    CallsiteHash callsite[1];
    uint32_t n = ring->drain(out, 1, meta, scope, callsite);
    assert(n == 1);
    assert(out[0].schema_hash == SchemaHash{0xDEADBEEF});
    assert(out[0].num_inputs == 2);
    assert(out[0].num_outputs == 1);
    assert(meta[0] == MetaIndex{42});
    assert(scope[0] == ScopeHash{0x1234});
    assert(callsite[0] == CallsiteHash{0x5678});
    assert(ring->size().peek() == 0);

    for (uint32_t i = 0; i < 1000; i++) {
        crucible::TraceRing::Entry entry{};
        entry.schema_hash = SchemaHash{i};
        assert(ring->try_append(entry));
    }
    assert(ring->size().peek() == 1000);

    crucible::TraceRing::Entry batch[2048];
    n = ring->drain(batch, 2048);
    assert(n == 1000);
    for (uint32_t i = 0; i < 1000; i++)
        assert(batch[i].schema_hash == SchemaHash{i});

    ring->reset();
    assert(ring->size().peek() == 0);

    // One cache line.
    static_assert(sizeof(crucible::TraceRing::Entry) == 64);

    // A null buffer is permitted when max_count is zero.
    uint32_t zero = ring->drain(nullptr, 0);
    assert(zero == 0);

    // Wrap-around: drive head/tail past CAPACITY so drain() exercises the
    // two-segment memcpy path.
    {
        constexpr uint32_t CAP = crucible::TraceRing::CAPACITY;
        for (uint32_t i = 0; i < CAP - 100; i++) {
            crucible::TraceRing::Entry wr{};
            wr.schema_hash = SchemaHash{i};
            assert(ring->try_append(wr));
        }
        std::vector<crucible::TraceRing::Entry> big(CAP);
        assert(ring->drain(big.data(), CAP) == CAP - 100);

        // head = tail = CAP - 100, so the next 200 appends straddle the wrap.
        for (uint32_t i = 0; i < 200; i++) {
            crucible::TraceRing::Entry wr{};
            wr.schema_hash = SchemaHash{100000 + i};
            assert(ring->try_append(wr));
        }
        uint32_t n_wrap = ring->drain(big.data(), CAP);
        assert(n_wrap == 200);
        for (uint32_t i = 0; i < 200; i++)
            assert(big[i].schema_hash == SchemaHash{100000 + i});
    }

    uint64_t hi0 = ring->total_produced();
    crucible::TraceRing::Entry hm{};
    hm.schema_hash = SchemaHash{0xABCD};
    assert(ring->try_append(hm));
    assert(ring->total_produced() == hi0 + 1);

    delete ring;

    // Producer and consumer exchange N entries live.  On x86 a wrongly
    // relaxed publish store would pass by accident, so the ordering claim
    // made here only bites on a weakly ordered target.
    {
        constexpr uint32_t N = 200'000;
        auto* r = new crucible::TraceRing();

        std::atomic<bool> producer_done{false};
        std::atomic<uint32_t> producer_spins{0};

        std::jthread producer{[&] {
            for (uint32_t i = 0; i < N; /* advance only on success */) {
                crucible::TraceRing::Entry entry{};
                entry.schema_hash = SchemaHash{i + 1};  // non-zero, unique
                entry.shape_hash = ShapeHash{~uint64_t{i} + 1};
                if (r->try_append(entry, MetaIndex{i}, ScopeHash{0xA000'0000ull | i}, CallsiteHash{0xB000'0000ull | i}))
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
            crucible::TraceRing::Entry batch_out[DRAIN_CAP];
            MetaIndex batch_meta[DRAIN_CAP];
            ScopeHash batch_scope[DRAIN_CAP];
            CallsiteHash batch_csite[DRAIN_CAP];

            uint32_t next = 0;
            while (next < N) {
                uint32_t got = r->drain(batch_out, DRAIN_CAP, batch_meta, batch_scope, batch_csite);
                if (got == 0) {
                    if (producer_done.load(std::memory_order_acquire) && r->size().peek() == 0) {
                        break;
                    }
                    CRUCIBLE_SPIN_PAUSE;
                    continue;
                }
                for (uint32_t k = 0; k < got; ++k) {
                    const uint32_t seq = next + k;
                    assert(batch_out[k].schema_hash == SchemaHash{seq + 1});
                    assert(batch_out[k].shape_hash == ShapeHash{~uint64_t{seq} + 1});
                    assert(batch_meta[k] == MetaIndex{seq});
                    assert(batch_scope[k] == ScopeHash{0xA000'0000ull | seq});
                    assert(batch_csite[k] == CallsiteHash{0xB000'0000ull | seq});
                }
                next += got;
            }
            assert(next == N);
        }};

        producer.join();
        consumer.join();
        delete r;

        std::printf("test_trace_ring: concurrent SPSC integrity "
                    "(N=%u, producer_spins=%u) OK\n",
                    N, producer_spins.load());
    }

    {
        auto* r = new crucible::TraceRing();

        crucible::TraceRing::Entry e1{};
        e1.schema_hash = SchemaHash{0xCAFE0001};
        assert(r->try_append_pure(e1, MetaIndex{1}, ScopeHash{0xC1}, CallsiteHash{0xD1}));
        assert(r->size().peek() == 1);

        crucible::TraceRing::Entry e2{};
        e2.schema_hash = SchemaHash{0xCAFE0002};
        assert(r->try_append_pure<eff::Row<>>(e2, MetaIndex{2}, ScopeHash{0xC2}, CallsiteHash{0xD2}));
        assert(r->size().peek() == 2);

        crucible::TraceRing::Entry e3{};
        e3.schema_hash = SchemaHash{0xCAFE0003};
        assert(r->try_append(e3, MetaIndex{3}, ScopeHash{0xC3}, CallsiteHash{0xD3}));
        crucible::TraceRing::Entry e4{};
        e4.schema_hash = SchemaHash{0xCAFE0004};
        assert(r->try_append_pure<eff::PureRow>(e4, MetaIndex{4}, ScopeHash{0xC4}, CallsiteHash{0xD4}));
        assert(r->size().peek() == 4);

        {
            crucible::TraceRing::Entry batch_i16[4];
            MetaIndex meta_i16[4];
            ScopeHash scope_i16[4];
            CallsiteHash csite_i16[4];
            uint32_t got = r->drain_pure(batch_i16, 4, meta_i16, scope_i16, csite_i16);
            assert(got == 4);
            assert(batch_i16[0].schema_hash == SchemaHash{0xCAFE0001});
            assert(batch_i16[1].schema_hash == SchemaHash{0xCAFE0002});
            assert(batch_i16[2].schema_hash == SchemaHash{0xCAFE0003});
            assert(batch_i16[3].schema_hash == SchemaHash{0xCAFE0004});
            assert(meta_i16[0] == MetaIndex{1} && meta_i16[3] == MetaIndex{4});
            assert(scope_i16[0] == ScopeHash{0xC1} && scope_i16[3] == ScopeHash{0xC4});
            assert(csite_i16[0] == CallsiteHash{0xD1} && csite_i16[3] == CallsiteHash{0xD4});
            assert(r->size().peek() == 0);
        }

        {
            uint32_t zero_i16 = r->drain_pure<eff::Row<>>(nullptr, 0);
            assert(zero_i16 == 0);
        }

        {
            crucible::TraceRing::Entry e5{};
            e5.schema_hash = SchemaHash{0xCAFE0005};
            assert(r->try_append_pure(e5));

            crucible::TraceRing::Entry e6{};
            e6.schema_hash = SchemaHash{0xCAFE0006};
            assert(r->try_append(e6));

            crucible::TraceRing::Entry one[1];
            uint32_t g1 = r->drain_pure<eff::TotRow>(one, 1);
            assert(g1 == 1);
            assert(one[0].schema_hash == SchemaHash{0xCAFE0005});

            uint32_t g2 = r->drain(one, 1);
            assert(g2 == 1);
            assert(one[0].schema_hash == SchemaHash{0xCAFE0006});
        }

        static_assert(eff::IsPure<eff::Row<>>);
        static_assert(eff::IsPure<eff::PureRow>);
        static_assert(eff::IsPure<eff::TotRow>);
        static_assert(eff::IsPure<eff::GhostRow>);
        static_assert(!eff::IsPure<eff::DivRow>);  // Block
        static_assert(!eff::IsPure<eff::Row<eff::Effect::IO>>);
        static_assert(!eff::IsPure<eff::Row<eff::Effect::Bg>>);
        static_assert(!eff::IsPure<eff::Row<eff::Effect::Alloc>>);
        static_assert(!eff::IsPure<eff::AllRow>);  // saturation top
        static_assert(!eff::IsPure<eff::Row<eff::Effect::IO,
                                            eff::Effect::Block>>);  // multi-atom

        delete r;
        std::printf("test_trace_ring: try_append_pure / drain_pure FOUND_I16 OK\n");
    }

    // The same exchange driven through the row-typed facades, to show the
    // wrapper does not change the ordering the raw API provides.
    {
        constexpr uint32_t N = 100'000;
        auto* r = new crucible::TraceRing();

        std::atomic<bool> producer_done{false};
        std::atomic<uint32_t> producer_spins{0};

        std::jthread producer{[&] {
            for (uint32_t i = 0; i < N; /* advance only on success */) {
                crucible::TraceRing::Entry entry{};
                entry.schema_hash = SchemaHash{i + 7};
                entry.shape_hash = ShapeHash{~uint64_t{i} + 7};
                if (r->try_append_pure(entry, MetaIndex{i + 1}, ScopeHash{0xE000'0000ull | i},
                                       CallsiteHash{0xF000'0000ull | i})) [[likely]] {
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
            crucible::TraceRing::Entry pure_out[DRAIN_CAP];
            MetaIndex pure_meta[DRAIN_CAP];
            ScopeHash pure_scope[DRAIN_CAP];
            CallsiteHash pure_csite[DRAIN_CAP];

            uint32_t next = 0;
            while (next < N) {
                // Explicit Row<> here and a default template argument on the
                // producer side, so both substitution paths run.
                uint32_t got = r->drain_pure<eff::Row<>>(pure_out, DRAIN_CAP, pure_meta, pure_scope, pure_csite);
                if (got == 0) {
                    if (producer_done.load(std::memory_order_acquire) && r->size().peek() == 0) {
                        break;
                    }
                    CRUCIBLE_SPIN_PAUSE;
                    continue;
                }
                for (uint32_t k = 0; k < got; ++k) {
                    const uint32_t seq = next + k;
                    assert(pure_out[k].schema_hash == SchemaHash{seq + 7});
                    assert(pure_out[k].shape_hash == ShapeHash{~uint64_t{seq} + 7});
                    assert(pure_meta[k] == MetaIndex{seq + 1});
                    assert(pure_scope[k] == ScopeHash{0xE000'0000ull | seq});
                    assert(pure_csite[k] == CallsiteHash{0xF000'0000ull | seq});
                }
                next += got;
            }
            assert(next == N);
        }};

        producer.join();
        consumer.join();
        delete r;

        std::printf("test_trace_ring: try_append_pure/drain_pure concurrent "
                    "(N=%u, producer_spins=%u) OK\n",
                    N, producer_spins.load());
    }

    {
        constexpr uint32_t CAP = crucible::TraceRing::CAPACITY;
        auto* r = new crucible::TraceRing();

        {
            crucible::TraceRing::Entry e_bare{};
            e_bare.schema_hash = SchemaHash{0xA0AA};
            assert(r->try_append_pure(e_bare));
            assert(r->size().peek() == 1);

            crucible::TraceRing::Entry one_a[1];
            uint32_t got_a = r->drain_pure(one_a, 1);
            assert(got_a == 1);
            assert(one_a[0].schema_hash == SchemaHash{0xA0AA});
            assert(r->size().peek() == 0);
        }

        // A wrapper that always returned true on a full ring would slip past
        // the concurrent test above: the spin counter would merely read low.
        // Filling the ring exactly makes the refusal definite.
        {
            for (uint32_t i = 0; i < CAP; ++i) {
                crucible::TraceRing::Entry full_e{};
                full_e.schema_hash = SchemaHash{0xB000'0000u | i};
                bool ok = r->try_append_pure(full_e);
                assert(ok && "ring should accept CAPACITY entries");
            }
            assert(r->size().peek() == CAP);

            crucible::TraceRing::Entry overflow_e{};
            overflow_e.schema_hash = SchemaHash{0xDEAD'BEEF};
            bool ok_overflow = r->try_append_pure(overflow_e);
            assert(!ok_overflow);
            assert(r->size().peek() == CAP);

            std::vector<crucible::TraceRing::Entry> full_drain(CAP);
            uint32_t got_b = r->drain_pure(full_drain.data(), CAP);
            assert(got_b == CAP);
            for (uint32_t i = 0; i < CAP; ++i) {
                assert(full_drain[i].schema_hash == SchemaHash{0xB000'0000u | i});
            }
            assert(r->size().peek() == 0);
        }

        {
            // head and tail reach CAP - 100, so the next 200 appends straddle
            // the slot wrap and drain must copy two segments.
            for (uint32_t i = 0; i < CAP - 100; ++i) {
                crucible::TraceRing::Entry pad_e{};
                pad_e.schema_hash = SchemaHash{0xC000'0000u | i};
                assert(r->try_append_pure(pad_e));
            }
            std::vector<crucible::TraceRing::Entry> pad_drain(CAP);
            uint32_t got_pad = r->drain_pure(pad_drain.data(), CAP);
            assert(got_pad == CAP - 100);

            for (uint32_t i = 0; i < 200; ++i) {
                crucible::TraceRing::Entry wrap_e{};
                wrap_e.schema_hash = SchemaHash{0xCAFE'0000u | i};
                assert(r->try_append_pure(wrap_e));
            }

            std::vector<crucible::TraceRing::Entry> wrap_drain(CAP);
            std::vector<MetaIndex> wrap_meta(CAP);
            std::vector<ScopeHash> wrap_scope(CAP);
            std::vector<CallsiteHash> wrap_csite(CAP);
            uint32_t got_wrap =
                r->drain_pure(wrap_drain.data(), CAP, wrap_meta.data(), wrap_scope.data(), wrap_csite.data());
            assert(got_wrap == 200);
            for (uint32_t i = 0; i < 200; ++i) {
                assert(wrap_drain[i].schema_hash == SchemaHash{0xCAFE'0000u | i});
                assert(wrap_meta[i] == MetaIndex::none());
                assert(wrap_scope[i] == ScopeHash{});
                assert(wrap_csite[i] == CallsiteHash{});
            }
        }

        // Each parallel-output buffer is independently nullable.
        {
            crucible::TraceRing::Entry mix_e{};
            mix_e.schema_hash = SchemaHash{0xD0D0};
            assert(r->try_append_pure(mix_e, MetaIndex{42}, ScopeHash{0xDEAD}, CallsiteHash{0xBEEF}));

            crucible::TraceRing::Entry mix_out[1];
            MetaIndex mix_meta[1];
            CallsiteHash mix_csite[1];
            uint32_t got_mix = r->drain_pure(mix_out, 1, mix_meta,
                                             /*out_scope_hashes=*/nullptr, mix_csite);
            assert(got_mix == 1);
            assert(mix_out[0].schema_hash == SchemaHash{0xD0D0});
            assert(mix_meta[0] == MetaIndex{42});
            assert(mix_csite[0] == CallsiteHash{0xBEEF});

            crucible::TraceRing::Entry mix_e2{};
            mix_e2.schema_hash = SchemaHash{0xD2D2};
            assert(r->try_append_pure(mix_e2, MetaIndex{99}, ScopeHash{0xCAFE}, CallsiteHash{0xF00D}));

            crucible::TraceRing::Entry mix_out2[1];
            ScopeHash mix_scope2[1];
            uint32_t got_mix2 = r->drain_pure(mix_out2, 1,
                                              /*out_meta_starts=*/nullptr, mix_scope2,
                                              /*out_callsite_hashes=*/nullptr);
            assert(got_mix2 == 1);
            assert(mix_out2[0].schema_hash == SchemaHash{0xD2D2});
            assert(mix_scope2[0] == ScopeHash{0xCAFE});
            assert(r->size().peek() == 0);
        }

        delete r;
        std::printf("test_trace_ring: try_append_pure/drain_pure FOUND_I16_AUDIT "
                    "(audit-A bare-default + audit-B capacity-full + "
                    "audit-C wrap-around + audit-D mixed-nullable) OK\n");
    }

    std::printf("test_trace_ring: all tests passed\n");
    return 0;
}

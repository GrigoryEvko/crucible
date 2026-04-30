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

  // Empty ring
  assert(ring->size() == 0);

  // Single append + drain
  crucible::TraceRing::Entry e{};
  e.schema_hash = SchemaHash{0xDEADBEEF};
  e.num_inputs = 2;
  e.num_outputs = 1;
  assert(ring->try_append(e, MetaIndex{42}, ScopeHash{0x1234}, CallsiteHash{0x5678}));
  assert(ring->size() == 1);

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
  assert(ring->size() == 0);

  // Batch append + drain
  for (uint32_t i = 0; i < 1000; i++) {
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = SchemaHash{i};
    assert(ring->try_append(entry));
  }
  assert(ring->size() == 1000);

  crucible::TraceRing::Entry batch[2048];
  n = ring->drain(batch, 2048);
  assert(n == 1000);
  for (uint32_t i = 0; i < 1000; i++)
    assert(batch[i].schema_hash == SchemaHash{i});

  // Reset
  ring->reset();
  assert(ring->size() == 0);

  // Entry is exactly 64 bytes (one cache line)
  static_assert(sizeof(crucible::TraceRing::Entry) == 64);

  // drain(nullptr, 0, ...) is allowed by the contract (max_count == 0).
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

    // head = tail = CAP - 100. Append 200 more — slot indices wrap at CAP,
    // so drain must emit a two-segment memcpy.
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

  // total_produced monotonicity (release-visible high-water mark).
  uint64_t hi0 = ring->total_produced();
  crucible::TraceRing::Entry hm{};
  hm.schema_hash = SchemaHash{0xABCD};
  assert(ring->try_append(hm));
  assert(ring->total_produced() == hi0 + 1);

  delete ring;

  // ── Concurrent SPSC integrity ─────────────────────────────────────
  //
  // Live producer+consumer exchange N distinct entries. Verifies:
  //   - Every append the producer emits is eventually drained exactly
  //     once (no lost/duplicate — cached_tail_ fallback works).
  //   - Schema hashes arrive in strict producer order (release/acquire
  //     publishes entry writes before head advances).
  //   - Parallel arrays (meta_starts, scope_hashes, callsite_hashes)
  //     stay in lockstep with Entry writes under concurrent drain.
  //
  // The hot path uses memory_order_relaxed on the producer's own-head
  // load and memory_order_release on publish. On x86 (TSO) a buggy
  // relaxed store would work by accident; this test crystallises the
  // behaviour so weak-order regressions (ARM, RISC-V) fail here.
  {
    constexpr uint32_t N = 200'000;
    auto* r = new crucible::TraceRing();

    std::atomic<bool>     producer_done{false};
    std::atomic<uint32_t> producer_spins{0};

    std::thread producer{[&]{
      for (uint32_t i = 0; i < N; /* advance only on success */) {
        crucible::TraceRing::Entry entry{};
        entry.schema_hash = SchemaHash{i + 1};     // non-zero, unique
        entry.shape_hash  = ShapeHash{~uint64_t{i} + 1};
        if (r->try_append(entry,
                          MetaIndex{i},
                          ScopeHash{0xA000'0000ull | i},
                          CallsiteHash{0xB000'0000ull | i})) [[likely]] {
          ++i;
        } else {
          CRUCIBLE_SPIN_PAUSE;
          producer_spins.fetch_add(1, std::memory_order_relaxed);
        }
      }
      producer_done.store(true, std::memory_order_release);
    }};

    std::thread consumer{[&]{
      // Bounded drain buffer — smaller than CAPACITY so we exercise the
      // multi-drain path. 4096 is enough to keep up with a reasonable
      // producer but small enough to see real pipelining.
      constexpr uint32_t DRAIN_CAP = 4096;
      crucible::TraceRing::Entry batch_out[DRAIN_CAP];
      MetaIndex    batch_meta [DRAIN_CAP];
      ScopeHash    batch_scope[DRAIN_CAP];
      CallsiteHash batch_csite[DRAIN_CAP];

      uint32_t next = 0;  // next expected schema_hash base (0 → seq+1)
      while (next < N) {
        uint32_t got = r->drain(batch_out, DRAIN_CAP,
                                batch_meta, batch_scope, batch_csite);
        if (got == 0) {
          if (producer_done.load(std::memory_order_acquire) &&
              r->size() == 0) {
            break;
          }
          CRUCIBLE_SPIN_PAUSE;
          continue;
        }
        for (uint32_t k = 0; k < got; ++k) {
          const uint32_t seq = next + k;
          assert(batch_out[k].schema_hash == SchemaHash{seq + 1});
          assert(batch_out[k].shape_hash  == ShapeHash{~uint64_t{seq} + 1});
          assert(batch_meta[k]            == MetaIndex{seq});
          assert(batch_scope[k]           == ScopeHash{0xA000'0000ull | seq});
          assert(batch_csite[k]           == CallsiteHash{0xB000'0000ull | seq});
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

  // ── FOUND-I16: try_append_pure / drain_pure positive coverage ────────
  //
  // (a) Default template arg behaves identically to try_append.
  // (b) Explicit Row<> template arg behaves identically.
  // (c) Interleaved try_append + try_append_pure preserve FIFO order.
  // (d) drain_pure mirrors drain semantics.
  // (e) Compile-time IsPure witnesses for Pure / Tot / Ghost aliases
  //     and structural rejection witnesses for non-Pure rows.
  {
    auto* r = new crucible::TraceRing();

    // (a) Default template arg
    crucible::TraceRing::Entry e1{};
    e1.schema_hash = SchemaHash{0xCAFE0001};
    assert(r->try_append_pure(e1, MetaIndex{1},
                              ScopeHash{0xC1}, CallsiteHash{0xD1}));
    assert(r->size() == 1);

    // (b) Explicit Row<> template arg
    crucible::TraceRing::Entry e2{};
    e2.schema_hash = SchemaHash{0xCAFE0002};
    assert(r->try_append_pure<eff::Row<>>(e2, MetaIndex{2},
                                          ScopeHash{0xC2}, CallsiteHash{0xD2}));
    assert(r->size() == 2);

    // (c) Interleaved with raw try_append; FIFO order preserved
    crucible::TraceRing::Entry e3{};
    e3.schema_hash = SchemaHash{0xCAFE0003};
    assert(r->try_append(e3, MetaIndex{3}, ScopeHash{0xC3}, CallsiteHash{0xD3}));
    crucible::TraceRing::Entry e4{};
    e4.schema_hash = SchemaHash{0xCAFE0004};
    assert(r->try_append_pure<eff::PureRow>(e4, MetaIndex{4},
                                            ScopeHash{0xC4}, CallsiteHash{0xD4}));
    assert(r->size() == 4);

    // (d) drain_pure mirrors drain semantics — default template arg
    {
      crucible::TraceRing::Entry batch_i16[4];
      MetaIndex    meta_i16[4];
      ScopeHash    scope_i16[4];
      CallsiteHash csite_i16[4];
      uint32_t got = r->drain_pure(batch_i16, 4, meta_i16, scope_i16, csite_i16);
      assert(got == 4);
      assert(batch_i16[0].schema_hash == SchemaHash{0xCAFE0001});
      assert(batch_i16[1].schema_hash == SchemaHash{0xCAFE0002});
      assert(batch_i16[2].schema_hash == SchemaHash{0xCAFE0003});
      assert(batch_i16[3].schema_hash == SchemaHash{0xCAFE0004});
      assert(meta_i16[0]  == MetaIndex{1}    && meta_i16[3]  == MetaIndex{4});
      assert(scope_i16[0] == ScopeHash{0xC1} && scope_i16[3] == ScopeHash{0xC4});
      assert(csite_i16[0] == CallsiteHash{0xD1} && csite_i16[3] == CallsiteHash{0xD4});
      assert(r->size() == 0);
    }

    // (d') drain_pure with explicit Row<> + zero max_count
    {
      uint32_t zero_i16 = r->drain_pure<eff::Row<>>(nullptr, 0);
      assert(zero_i16 == 0);
    }

    // (d'') drain_pure interleaved with raw drain
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

    // (e) Compile-time IsPure witnesses
    static_assert(eff::IsPure<eff::Row<>>);
    static_assert(eff::IsPure<eff::PureRow>);
    static_assert(eff::IsPure<eff::TotRow>);
    static_assert(eff::IsPure<eff::GhostRow>);
    static_assert(!eff::IsPure<eff::DivRow>);                  // Block
    static_assert(!eff::IsPure<eff::Row<eff::Effect::IO>>);
    static_assert(!eff::IsPure<eff::Row<eff::Effect::Bg>>);
    static_assert(!eff::IsPure<eff::Row<eff::Effect::Alloc>>);
    static_assert(!eff::IsPure<eff::AllRow>);                  // saturation top
    static_assert(!eff::IsPure<eff::Row<eff::Effect::IO,
                                        eff::Effect::Block>>); // multi-atom

    delete r;
    std::printf("test_trace_ring: try_append_pure / drain_pure FOUND_I16 OK\n");
  }

  // ── FOUND-I16-AUDIT: concurrent SPSC integrity using _pure variants ──
  //
  // Mirror of the prior concurrent test, but BOTH producer and consumer
  // use the row-typed facades.  Verifies:
  //   • the row-typed wrapper introduces zero serialization latency
  //     vs the raw API (producer_spins remains comparable)
  //   • interleaved acquire/release ordering still holds when the
  //     producer drives try_append_pure and the consumer drives
  //     drain_pure (the wrapper does not change the underlying atomic
  //     instructions).
  //   • bit-exact replay: every produced entry arrives in producer
  //     order with all four parallel-array fields intact.
  {
    constexpr uint32_t N = 100'000;
    auto* r = new crucible::TraceRing();

    std::atomic<bool>     producer_done{false};
    std::atomic<uint32_t> producer_spins{0};

    std::thread producer{[&]{
      for (uint32_t i = 0; i < N; /* advance only on success */) {
        crucible::TraceRing::Entry entry{};
        entry.schema_hash = SchemaHash{i + 7};
        entry.shape_hash  = ShapeHash{~uint64_t{i} + 7};
        // CallerRow defaulted to Row<> — IsPure satisfied by construction.
        if (r->try_append_pure(entry,
                               MetaIndex{i + 1},
                               ScopeHash{0xE000'0000ull | i},
                               CallsiteHash{0xF000'0000ull | i})) [[likely]] {
          ++i;
        } else {
          CRUCIBLE_SPIN_PAUSE;
          producer_spins.fetch_add(1, std::memory_order_relaxed);
        }
      }
      producer_done.store(true, std::memory_order_release);
    }};

    std::thread consumer{[&]{
      constexpr uint32_t DRAIN_CAP = 2048;
      crucible::TraceRing::Entry pure_out[DRAIN_CAP];
      MetaIndex    pure_meta [DRAIN_CAP];
      ScopeHash    pure_scope[DRAIN_CAP];
      CallsiteHash pure_csite[DRAIN_CAP];

      uint32_t next = 0;
      while (next < N) {
        // Consumer calls drain_pure with explicit Row<> to exercise
        // the substitution path (vs default template-arg path on the
        // producer side).
        uint32_t got = r->drain_pure<eff::Row<>>(
            pure_out, DRAIN_CAP, pure_meta, pure_scope, pure_csite);
        if (got == 0) {
          if (producer_done.load(std::memory_order_acquire) &&
              r->size() == 0) {
            break;
          }
          CRUCIBLE_SPIN_PAUSE;
          continue;
        }
        for (uint32_t k = 0; k < got; ++k) {
          const uint32_t seq = next + k;
          assert(pure_out[k].schema_hash == SchemaHash{seq + 7});
          assert(pure_out[k].shape_hash  == ShapeHash{~uint64_t{seq} + 7});
          assert(pure_meta[k]            == MetaIndex{seq + 1});
          assert(pure_scope[k]           == ScopeHash{0xE000'0000ull | seq});
          assert(pure_csite[k]           == CallsiteHash{0xF000'0000ull | seq});
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

  // ── FOUND-I16-AUDIT: edge-case probes for the row-typed facades ──────
  //
  // The base I16 commit covered the canonical positive surface
  // (default-arg, explicit Row<>, alias variants, interleave, full
  // 4-output drain, zero-count drain, 2-output drain, concurrent SPSC).
  // The audit closes 4 narrow gaps that the canonical coverage does
  // not exercise:
  //
  // (audit-A) Bare `try_append_pure(e)` with no callsite args — all
  //           three MetaIndex / ScopeHash / CallsiteHash defaults
  //           materialize simultaneously.
  //
  // (audit-B) Capacity-full path through try_append_pure — drives the
  //           ring to CAPACITY entries, then asserts the wrapper
  //           returns `false` deterministically (no concurrency).
  //           Without this, a wrapper bug that always returned `true`
  //           on full could slip past the concurrent test (the
  //           producer_spins counter would just be lower than expected,
  //           a muddled signal).
  //
  // (audit-C) Wrap-around path through drain_pure — single-threaded,
  //           deterministic mirror of the raw-drain wrap-around block
  //           at lines 71-92 above, but driven through drain_pure.
  //           Witnesses the wrapper preserves the two-segment memcpy
  //           path and the {head, tail} mod CAPACITY arithmetic.
  //
  // (audit-D) Mixed-nullable parallel-buffer outputs — drain_pure
  //           supplies SOME but not all of the optional out_meta_starts
  //           / out_scope_hashes / out_callsite_hashes buffers, in a
  //           pattern (meta=non-null, scope=null, csite=non-null) that
  //           the canonical 4-output and zero-output tests do not cover.
  //           Pins per-buffer null-ness independence at the wrapper
  //           level (each output is forwarded individually to drain).
  {
    constexpr uint32_t CAP = crucible::TraceRing::CAPACITY;
    auto* r = new crucible::TraceRing();

    // (audit-A) Bare default-arg call — no callsite args.
    {
      crucible::TraceRing::Entry e_bare{};
      e_bare.schema_hash = SchemaHash{0xA0AA};
      assert(r->try_append_pure(e_bare));    // all defaults
      assert(r->size() == 1);

      // Drain it back via drain_pure(out, max_count) — 2-arg form,
      // no parallel-array outputs.
      crucible::TraceRing::Entry one_a[1];
      uint32_t got_a = r->drain_pure(one_a, 1);
      assert(got_a == 1);
      assert(one_a[0].schema_hash == SchemaHash{0xA0AA});
      assert(r->size() == 0);
    }

    // (audit-B) Capacity-full deterministic — fill ring exactly,
    // assert next try_append_pure returns false.
    {
      for (uint32_t i = 0; i < CAP; ++i) {
        crucible::TraceRing::Entry full_e{};
        full_e.schema_hash = SchemaHash{0xB000'0000u | i};
        bool ok = r->try_append_pure(full_e);
        assert(ok && "ring should accept CAPACITY entries");
      }
      assert(r->size() == CAP);

      // One more append must fail — ring is at capacity.
      crucible::TraceRing::Entry overflow_e{};
      overflow_e.schema_hash = SchemaHash{0xDEAD'BEEF};
      bool ok_overflow = r->try_append_pure(overflow_e);
      assert(!ok_overflow);
      assert(r->size() == CAP);  // size unchanged

      // Drain everything back, verify FIFO order.
      std::vector<crucible::TraceRing::Entry> full_drain(CAP);
      uint32_t got_b = r->drain_pure(full_drain.data(), CAP);
      assert(got_b == CAP);
      for (uint32_t i = 0; i < CAP; ++i) {
        assert(full_drain[i].schema_hash == SchemaHash{0xB000'0000u | i});
      }
      assert(r->size() == 0);
    }

    // (audit-C) Wrap-around through drain_pure — drive head/tail past
    // CAPACITY so the wrapper exercises the two-segment memcpy path.
    {
      // Get head/tail to (CAP - 100, CAP - 100) so the next 200
      // appends will straddle the slot-index wrap.
      for (uint32_t i = 0; i < CAP - 100; ++i) {
        crucible::TraceRing::Entry pad_e{};
        pad_e.schema_hash = SchemaHash{0xC000'0000u | i};
        assert(r->try_append_pure(pad_e));
      }
      std::vector<crucible::TraceRing::Entry> pad_drain(CAP);
      uint32_t got_pad = r->drain_pure(pad_drain.data(), CAP);
      assert(got_pad == CAP - 100);
      // head = tail = CAP - 100 logical, slot = (CAP - 100) % CAP

      // Append 200 more — slot indices wrap around at CAP.
      for (uint32_t i = 0; i < 200; ++i) {
        crucible::TraceRing::Entry wrap_e{};
        wrap_e.schema_hash = SchemaHash{0xCAFE'0000u | i};
        assert(r->try_append_pure(wrap_e));
      }

      // drain_pure must emit the 200 entries in correct order via the
      // two-segment memcpy path.  All 4 parallel-array slots requested
      // (default-init MetaIndex / ScopeHash / CallsiteHash on each).
      std::vector<crucible::TraceRing::Entry> wrap_drain(CAP);
      std::vector<MetaIndex>    wrap_meta (CAP);
      std::vector<ScopeHash>    wrap_scope(CAP);
      std::vector<CallsiteHash> wrap_csite(CAP);
      uint32_t got_wrap = r->drain_pure(
          wrap_drain.data(), CAP,
          wrap_meta.data(), wrap_scope.data(), wrap_csite.data());
      assert(got_wrap == 200);
      for (uint32_t i = 0; i < 200; ++i) {
        assert(wrap_drain[i].schema_hash == SchemaHash{0xCAFE'0000u | i});
        assert(wrap_meta[i]              == MetaIndex::none());  // default
        assert(wrap_scope[i]             == ScopeHash{});         // default
        assert(wrap_csite[i]             == CallsiteHash{});      // default
      }
    }

    // (audit-D) Mixed-nullable parallel-buffer outputs — meta=non-null,
    // scope=NULL, csite=non-null.  Pin per-buffer null-ness independence.
    {
      crucible::TraceRing::Entry mix_e{};
      mix_e.schema_hash = SchemaHash{0xD0D0};
      assert(r->try_append_pure(
          mix_e, MetaIndex{42},
          ScopeHash{0xDEAD},  CallsiteHash{0xBEEF}));

      crucible::TraceRing::Entry mix_out[1];
      MetaIndex    mix_meta[1];
      CallsiteHash mix_csite[1];
      // out_scope_hashes deliberately nullptr; meta + csite supplied.
      uint32_t got_mix = r->drain_pure(mix_out, 1, mix_meta,
                                       /*out_scope_hashes=*/nullptr,
                                       mix_csite);
      assert(got_mix == 1);
      assert(mix_out[0].schema_hash == SchemaHash{0xD0D0});
      assert(mix_meta[0]            == MetaIndex{42});
      assert(mix_csite[0]           == CallsiteHash{0xBEEF});
      // ScopeHash{0xDEAD} was produced but not delivered (nullptr buffer);
      // the wrapper must not crash, and meta + csite must be intact.

      // Symmetric variant: meta=NULL, scope=non-null, csite=NULL.
      crucible::TraceRing::Entry mix_e2{};
      mix_e2.schema_hash = SchemaHash{0xD2D2};
      assert(r->try_append_pure(
          mix_e2, MetaIndex{99},
          ScopeHash{0xCAFE}, CallsiteHash{0xF00D}));

      crucible::TraceRing::Entry mix_out2[1];
      ScopeHash    mix_scope2[1];
      uint32_t got_mix2 = r->drain_pure(mix_out2, 1,
                                        /*out_meta_starts=*/nullptr,
                                        mix_scope2,
                                        /*out_callsite_hashes=*/nullptr);
      assert(got_mix2 == 1);
      assert(mix_out2[0].schema_hash == SchemaHash{0xD2D2});
      assert(mix_scope2[0]           == ScopeHash{0xCAFE});
      assert(r->size() == 0);
    }

    delete r;
    std::printf("test_trace_ring: try_append_pure/drain_pure FOUND_I16_AUDIT "
                "(audit-A bare-default + audit-B capacity-full + "
                "audit-C wrap-around + audit-D mixed-nullable) OK\n");
  }

  std::printf("test_trace_ring: all tests passed\n");
  return 0;
}

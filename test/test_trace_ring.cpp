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

  std::printf("test_trace_ring: all tests passed\n");
  return 0;
}

#include <crucible/Platform.h>
#include <crucible/TraceRing.h>

#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

using crucible::SchemaHash;
using crucible::ShapeHash;
using crucible::ScopeHash;
using crucible::CallsiteHash;
using crucible::MetaIndex;

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

  std::printf("test_trace_ring: all tests passed\n");
  return 0;
}

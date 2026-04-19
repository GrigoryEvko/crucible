#include <crucible/TraceRing.h>
#include <cassert>
#include <cstdio>
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
  std::printf("test_trace_ring: all tests passed\n");
  return 0;
}

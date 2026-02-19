#include <crucible/TraceRing.h>
#include <cassert>
#include <cstdio>

int main() {
  auto* ring = new crucible::TraceRing();

  // Empty ring
  assert(ring->size() == 0);

  // Single append + drain
  crucible::TraceRing::Entry e{};
  e.schema_hash = 0xDEADBEEF;
  e.num_inputs = 2;
  e.num_outputs = 1;
  assert(ring->try_append(e, 42, 0x1234, 0x5678));
  assert(ring->size() == 1);

  crucible::TraceRing::Entry out[1];
  uint32_t meta[1];
  uint64_t scope[1];
  uint64_t callsite[1];
  uint32_t n = ring->drain(out, 1, meta, scope, callsite);
  assert(n == 1);
  assert(out[0].schema_hash == 0xDEADBEEF);
  assert(out[0].num_inputs == 2);
  assert(out[0].num_outputs == 1);
  assert(meta[0] == 42);
  assert(scope[0] == 0x1234);
  assert(callsite[0] == 0x5678);
  assert(ring->size() == 0);

  // Batch append + drain
  for (uint32_t i = 0; i < 1000; i++) {
    crucible::TraceRing::Entry entry{};
    entry.schema_hash = i;
    assert(ring->try_append(entry));
  }
  assert(ring->size() == 1000);

  crucible::TraceRing::Entry batch[2048];
  n = ring->drain(batch, 2048);
  assert(n == 1000);
  for (uint32_t i = 0; i < 1000; i++)
    assert(batch[i].schema_hash == i);

  // Reset
  ring->reset();
  assert(ring->size() == 0);

  // Entry is exactly 64 bytes (one cache line)
  static_assert(sizeof(crucible::TraceRing::Entry) == 64);

  delete ring;
  std::printf("test_trace_ring: all tests passed\n");
  return 0;
}

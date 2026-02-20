#include <crucible/MerkleDag.h>
#include <cassert>
#include <cstdio>
#include <cstring>

using crucible::SchemaHash;
using crucible::ContentHash;
using crucible::MerkleHash;

int main() {
  crucible::Arena arena(1 << 16);

  // Test compute_storage_nbytes
  crucible::TensorMeta m{};
  m.ndim = 2;
  m.sizes[0] = 32;
  m.sizes[1] = 64;
  m.strides[0] = 64;
  m.strides[1] = 1;
  m.dtype = crucible::ScalarType::Float;
  uint64_t nbytes = crucible::compute_storage_nbytes(m);
  // (31*64 + 63*1 + 1) * 4 = 2048 * 4 = 8192 (= 32 * 64 * sizeof(float))
  assert(nbytes == 8192);

  // Test make_region
  crucible::TraceEntry ops[3];
  std::memset(ops, 0, sizeof(ops));
  ops[0].schema_hash = SchemaHash{0xAABB};
  ops[1].schema_hash = SchemaHash{0xCCDD};
  ops[2].schema_hash = SchemaHash{0xEEFF};
  auto* region = crucible::make_region(arena, ops, 3);
  assert(region != nullptr);
  assert(region->kind == crucible::TraceNodeKind::REGION);
  assert(region->num_ops == 3);
  assert(region->ops[0].schema_hash == SchemaHash{0xAABB});
  assert(region->ops[2].schema_hash == SchemaHash{0xEEFF});
  assert(region->first_op_schema == SchemaHash{0xAABB});
  assert(static_cast<bool>(region->content_hash));

  // Test compute_content_hash determinism
  ContentHash h1 = region->content_hash;
  auto* region2 = crucible::make_region(arena, ops, 3);
  assert(region2->content_hash == h1);

  // Test make_terminal
  auto* terminal = crucible::make_terminal(arena);
  assert(terminal->kind == crucible::TraceNodeKind::TERMINAL);
  assert(terminal->next == nullptr);

  // Test recompute_merkle
  region->next = terminal;
  crucible::recompute_merkle(region);
  assert(static_cast<bool>(region->merkle_hash));

  // Test KernelCache
  crucible::KernelCache cache;
  assert(cache.lookup(ContentHash{0x1234}) == nullptr);
  struct FakeKernel { int x; };
  FakeKernel fk{42};
  cache.insert(ContentHash{0x1234}, reinterpret_cast<crucible::CompiledKernel*>(&fk));
  assert(cache.lookup(ContentHash{0x1234}) == reinterpret_cast<crucible::CompiledKernel*>(&fk));
  // Duplicate insert: overwrites to newer variant
  FakeKernel fk2{99};
  cache.insert(ContentHash{0x1234}, reinterpret_cast<crucible::CompiledKernel*>(&fk2));
  assert(cache.lookup(ContentHash{0x1234}) == reinterpret_cast<crucible::CompiledKernel*>(&fk2));

  // Test element_size
  assert(crucible::element_size(crucible::ScalarType::Float) == 4);
  assert(crucible::element_size(crucible::ScalarType::Double) == 8);
  assert(crucible::element_size(crucible::ScalarType::Half) == 2);
  assert(crucible::element_size(crucible::ScalarType::Byte) == 1);
  assert(crucible::element_size(crucible::ScalarType::ComplexDouble) == 16);

  std::printf("test_merkle_dag: all tests passed\n");
  return 0;
}

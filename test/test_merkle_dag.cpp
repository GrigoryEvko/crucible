#include <crucible/MerkleDag.h>
#include <crucible/Effects.h>
#include <cassert>
#include <cstdio>
#include <cstring>

using crucible::SchemaHash;
using crucible::ContentHash;
using crucible::MerkleHash;

int main() {
  crucible::fx::Test test;
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
  crucible::TraceEntry ops[3]{};
  ops[0].schema_hash = SchemaHash{0xAABB};
  ops[1].schema_hash = SchemaHash{0xCCDD};
  ops[2].schema_hash = SchemaHash{0xEEFF};
  auto* region = crucible::make_region(test.alloc, arena, ops, 3);
  assert(region != nullptr);
  assert(region->kind == crucible::TraceNodeKind::REGION);
  assert(region->num_ops == 3);
  assert(region->ops[0].schema_hash == SchemaHash{0xAABB});
  assert(region->ops[2].schema_hash == SchemaHash{0xEEFF});
  assert(region->first_op_schema == SchemaHash{0xAABB});
  assert(static_cast<bool>(region->content_hash));

  // Test compute_content_hash determinism
  ContentHash h1 = region->content_hash;
  auto* region2 = crucible::make_region(test.alloc, arena, ops, 3);
  assert(region2->content_hash == h1);

  // Test make_terminal
  auto* terminal = crucible::make_terminal(test.alloc, arena);
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

  // ═══ LoopNode tests ═════════════════════════════════════════════

  // Build a body sub-DAG: region1 -> region2 -> terminal
  crucible::TraceEntry body_ops1[2]{};
  body_ops1[0].schema_hash = SchemaHash{0x1111};
  body_ops1[1].schema_hash = SchemaHash{0x2222};
  auto* body_r1 = crucible::make_region(test.alloc, arena, body_ops1, 2);

  crucible::TraceEntry body_ops2[1]{};
  body_ops2[0].schema_hash = SchemaHash{0x3333};
  auto* body_r2 = crucible::make_region(test.alloc, arena, body_ops2, 1);

  auto* body_term = crucible::make_terminal(test.alloc, arena);
  body_r1->next = body_r2;
  body_r2->next = body_term;

  // Compute body content hash from the chain
  ContentHash body_ch = crucible::compute_body_content_hash(body_r1);
  assert(static_cast<bool>(body_ch));

  // Body content hash is deterministic
  assert(crucible::compute_body_content_hash(body_r1) == body_ch);

  // make_loop: Repeat(4) with one feedback edge
  crucible::FeedbackEdge fb_edges[1]{};
  fb_edges[0].output_idx = 0;
  fb_edges[0].input_idx = 0;

  auto* loop = crucible::make_loop(
      test.alloc, arena, body_r1, body_ch,
      fb_edges, 1,
      crucible::LoopTermKind::REPEAT, 4);

  assert(loop->kind == crucible::TraceNodeKind::LOOP);
  assert(loop->body == body_r1);
  assert(loop->body_content_hash == body_ch);
  assert(loop->num_feedback == 1);
  assert(loop->feedback_edges[0].output_idx == 0);
  assert(loop->term_kind == crucible::LoopTermKind::REPEAT);
  assert(loop->repeat_count == 4);
  assert(std::bit_cast<uint32_t>(loop->epsilon) == 0);
  static_assert(sizeof(crucible::LoopNode) == 64);
  static_assert(sizeof(crucible::FeedbackEdge) == 4);

  // Wire: loop -> final_region -> terminal2
  crucible::TraceEntry final_ops[1]{};
  final_ops[0].schema_hash = SchemaHash{0x4444};
  auto* final_region = crucible::make_region(test.alloc, arena, final_ops, 1);
  auto* dag_term = crucible::make_terminal(test.alloc, arena);
  final_region->next = dag_term;
  loop->next = final_region;

  // Merkle hash covers loop body, feedback, termination, and continuation
  crucible::recompute_merkle(loop);
  MerkleHash loop_merkle = loop->merkle_hash;
  assert(static_cast<bool>(loop_merkle));

  // Different repeat count → different merkle hash
  auto* loop2 = crucible::make_loop(
      test.alloc, arena, body_r1, body_ch,
      fb_edges, 1,
      crucible::LoopTermKind::REPEAT, 8);
  loop2->next = final_region;
  crucible::recompute_merkle(loop2);
  assert(loop2->merkle_hash != loop_merkle);

  // Different termination kind → different merkle hash
  auto* loop3 = crucible::make_loop(
      test.alloc, arena, body_r1, body_ch,
      fb_edges, 1,
      crucible::LoopTermKind::UNTIL, 4, 0.001f);
  loop3->next = final_region;
  crucible::recompute_merkle(loop3);
  assert(loop3->merkle_hash != loop_merkle);
  assert(loop3->merkle_hash != loop2->merkle_hash);

  // Different feedback → different merkle hash
  crucible::FeedbackEdge fb_edges2[2]{};
  fb_edges2[0] = {.output_idx = 0, .input_idx = 0};
  fb_edges2[1] = {.output_idx = 1, .input_idx = 1};
  auto* loop4 = crucible::make_loop(
      test.alloc, arena, body_r1, body_ch,
      fb_edges2, 2,
      crucible::LoopTermKind::REPEAT, 4);
  loop4->next = final_region;
  crucible::recompute_merkle(loop4);
  assert(loop4->merkle_hash != loop_merkle);

  // feedback_signature: empty → 0, non-empty → nonzero
  assert(crucible::feedback_signature({}) == 0);
  assert(crucible::feedback_signature(loop->feedback_span()) != 0);

  // collect_regions: finds body regions inside LoopNode
  crucible::RegionNode* collected[16]{};
  uint32_t n_collected = crucible::collect_regions(
      loop, std::span{collected, 16});
  // loop body has 2 regions (body_r1, body_r2) + final_region after loop = 3
  assert(n_collected == 3);
  assert(collected[0] == body_r1);
  assert(collected[1] == body_r2);
  assert(collected[2] == final_region);

  // replay: Repeat(4) executes body regions 4 times, then final region
  uint32_t exec_count = 0;
  std::vector<ContentHash> replay_log;
  replay_log.reserve(16);
  bool replay_ok = crucible::replay(
      loop,
      [](const crucible::Guard&) -> int64_t { return 0; },
      [&](crucible::RegionNode* r) {
        replay_log.push_back(r->content_hash);
        exec_count++;
      });
  assert(replay_ok);
  // 4 iterations × 2 body regions + 1 final = 9
  assert(exec_count == 9);
  assert(replay_log.size() == 9);
  // First 8 entries alternate: body_r1, body_r2, body_r1, body_r2, ...
  for (uint32_t i = 0; i < 8; i += 2) {
    assert(replay_log[i] == body_r1->content_hash);
    assert(replay_log[i + 1] == body_r2->content_hash);
  }
  // Last entry is final_region
  assert(replay_log[8] == final_region->content_hash);

  // replay: Repeat(0) skips body entirely, only executes continuation
  auto* loop_zero = crucible::make_loop(
      test.alloc, arena, body_r1, body_ch,
      fb_edges, 1,
      crucible::LoopTermKind::REPEAT, 0);
  loop_zero->next = final_region;
  uint32_t zero_count = 0;
  bool zero_ok = crucible::replay(
      loop_zero,
      [](const crucible::Guard&) -> int64_t { return 0; },
      [&](crucible::RegionNode*) { zero_count++; });
  assert(zero_ok);
  assert(zero_count == 1); // only final_region

  // compute_body_content_hash: different body → different hash
  crucible::TraceEntry alt_body_ops[1]{};
  alt_body_ops[0].schema_hash = SchemaHash{0x9999};
  auto* alt_body = crucible::make_region(test.alloc, arena, alt_body_ops, 1);
  auto* alt_body_term = crucible::make_terminal(test.alloc, arena);
  alt_body->next = alt_body_term;
  ContentHash alt_ch = crucible::compute_body_content_hash(alt_body);
  assert(alt_ch != body_ch);

  std::printf("test_merkle_dag: all tests passed\n");
  return 0;
}

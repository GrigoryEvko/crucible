#include <crucible/effects/Capabilities.h>
#include <crucible/vis/LiveTraceVisualizer.h>

#include "test_assert.h"

#include <cstdio>
#include <string>

namespace {

[[nodiscard]] bool contains(std::string const& text, char const* needle) {
  return text.find(needle) != std::string::npos;
}

[[nodiscard]] crucible::RegionNode* make_one_op_region(
    crucible::effects::Alloc alloc,
    crucible::Arena& arena,
    crucible::TraceEntry* op,
    std::uint64_t schema) {
  op->schema_hash = crucible::SchemaHash{schema};
  return crucible::make_region(alloc, arena, op, 1);
}

} // namespace

int main() {
  crucible::effects::Test test;
  crucible::Arena arena(1 << 16);

  crucible::TraceEntry arm0_op{};
  crucible::TraceEntry arm1_op{};
  crucible::TraceEntry body_op{};

  auto* arm0 = make_one_op_region(test.alloc, arena, &arm0_op, 0xA001);
  auto* arm1 = make_one_op_region(test.alloc, arena, &arm1_op, 0xA002);
  auto* body = make_one_op_region(test.alloc, arena, &body_op, 0xA003);

  auto* body_term = crucible::make_terminal(test.alloc, arena);
  body->next = body_term;

  crucible::FeedbackEdge feedback[1]{{.output_idx = 0, .input_idx = 0}};
  auto* loop = crucible::make_loop(
      test.alloc, arena, body, crucible::compute_body_content_hash(body),
      feedback, 1, crucible::LoopTermKind::REPEAT, 3);
  auto* terminal = crucible::make_terminal(test.alloc, arena);
  loop->next = terminal;

  arm0->next = loop;
  arm1->next = loop;

  auto* branch = new (arena.alloc_obj<crucible::BranchNode>(test.alloc))
      crucible::BranchNode{};
  branch->kind = crucible::TraceNodeKind::BRANCH;
  branch->guard.kind = crucible::Guard::Kind::SHAPE_DIM;
  branch->num_arms = 2;
  branch->arms = arena.alloc_array<crucible::BranchNode::Arm>(test.alloc, 2);
  branch->arms[0] = {.value = 0, .target = arm0};
  branch->arms[1] = {.value = 1, .target = arm1};
  branch->next = loop;

  crucible::recompute_merkle(branch);

  auto view = crucible::vis::extract_block_view(
      static_cast<const crucible::TraceNode*>(branch));
  assert(view.detection.blocks.size() == 5);

  std::uint32_t regions = 0;
  std::uint32_t branches = 0;
  std::uint32_t loops = 0;
  for (const auto& block : view.detection.blocks) {
    if (block.kind == crucible::vis::BlockKind::MODULE) ++regions;
    if (block.kind == crucible::vis::BlockKind::BRANCH) ++branches;
    if (block.kind == crucible::vis::BlockKind::LOOP) ++loops;
  }
  assert(regions == 3);
  assert(branches == 1);
  assert(loops == 1);

  bool saw_guard0 = false;
  bool saw_guard1 = false;
  bool saw_feedback = false;
  for (const auto& edge : view.edges) {
    if (edge.label == "guard=0") saw_guard0 = true;
    if (edge.label == "guard=1") saw_guard1 = true;
    if (edge.kind == crucible::vis::DagEdgeKind::LOOP_FEEDBACK)
      saw_feedback = true;
  }
  assert(saw_guard0);
  assert(saw_guard1);
  assert(saw_feedback);

  auto metrics = crucible::augur::metrics_sample_at(
      crucible::augur::AugurMetrics{.ntk_alpha_drift = 0.125}, 4);
  crucible::vis::LiveTraceVisualizer live{
      static_cast<const crucible::TraceNode&>(*branch), metrics};
  const std::string svg = live.render_svg("adapter-test");

  assert(contains(svg, "<svg"));
  assert(contains(svg, "adapter-test"));
  assert(contains(svg, "guard=0"));
  assert(contains(svg, "guard=1"));
  assert(contains(svg, "feedback"));
  assert(contains(svg, "loop x3"));

  std::printf("test_merkle_dag_adapter: all tests passed\n");
  return 0;
}

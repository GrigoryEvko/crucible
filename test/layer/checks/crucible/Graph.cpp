// The compile-time checks of crucible/Graph.h.

#include <crucible/Graph.h>

namespace crucible {

static_assert(sizeof(InstIndex) == sizeof(uint16_t), "InstIndex must stay 2 bytes to preserve sizeof(Inst) == 8");
static_assert(std::is_standard_layout_v<InstIndex>);

static_assert(sizeof(Inst) == 8, "Inst must be 8 bytes");
static_assert(offsetof(Inst, operands) == 2, "Inst's operands start after the op byte and the dtype byte");

static_assert(sizeof(GraphNode) == 64, "GraphNode must be 64 bytes");

}  // namespace crucible

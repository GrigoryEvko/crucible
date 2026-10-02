#pragma once

// The scope hash on each op is recorded from the forward pre-hook of the
// enclosing module, so one scope path is one module.
//
// The detection and the string helpers are in src/vis/BlockDetector.cpp.

#include <crucible/Types.h>
#include <fixy/Core.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crucible {
struct LoadedTrace;
}  // namespace crucible

namespace crucible::vis {

enum class OpFamily : uint8_t {
    GEMM,
    CONV,
    NORM,
    ATTN,
    ACT,
    ELEM,
    MOVE,
    REDUCE,
    OPTIM,
    LOSS,
    EMBED,
    POOL,
    OTHER,
};

[[nodiscard]] constexpr const char* family_name(OpFamily f) {
    switch (f) {
        case OpFamily::GEMM:
            return "gemm";
        case OpFamily::CONV:
            return "conv";
        case OpFamily::NORM:
            return "norm";
        case OpFamily::ATTN:
            return "attn";
        case OpFamily::ACT:
            return "act";
        case OpFamily::ELEM:
            return "elem";
        case OpFamily::MOVE:
            return "move";
        case OpFamily::REDUCE:
            return "reduce";
        case OpFamily::OPTIM:
            return "optim";
        case OpFamily::LOSS:
            return "loss";
        case OpFamily::EMBED:
            return "embed";
        case OpFamily::POOL:
            return "pool";
        case OpFamily::OTHER:
            return "other";
        default:
            ::fixy::unreachable();
    }
}

enum class BlockKind : uint8_t {
    MODULE,
    MODULE_BWD,
    OPTIMIZER,
    EPILOGUE,
    ROOT,  // Ops that sit in no module scope.
    BRANCH,
    LOOP,
};

[[nodiscard]] constexpr const char* block_kind_name(BlockKind k) {
    switch (k) {
        case BlockKind::MODULE:
            return "Module";
        case BlockKind::MODULE_BWD:
            return "BWD";
        case BlockKind::OPTIMIZER:
            return "Optimizer";
        case BlockKind::EPILOGUE:
            return "Epilogue";
        case BlockKind::ROOT:
            return "Root";
        case BlockKind::BRANCH:
            return "Branch";
        case BlockKind::LOOP:
            return "Loop";
        default:
            ::fixy::unreachable();
    }
}

[[nodiscard]] constexpr const char* block_kind_icon(BlockKind k) {
    switch (k) {
        case BlockKind::MODULE:
            return "\xe2\x96\xa3";
        case BlockKind::MODULE_BWD:
            return "\xe2\x97\x87";
        case BlockKind::OPTIMIZER:
            return "\xe2\x9a\x99";
        case BlockKind::EPILOGUE:
            return "\xc2\xb7";
        case BlockKind::ROOT:
            return "\xe2\x94\x80";
        case BlockKind::BRANCH:
            return "?";
        case BlockKind::LOOP:
            return "@";
        default:
            ::fixy::unreachable();
    }
}

enum class Phase : uint8_t {
    FORWARD,
    BACKWARD,
    OPTIMIZER
};
enum class Architecture : uint8_t {
    UNET,
    VIT,
    GENERIC
};

struct Op {
    uint32_t idx = 0;
    SchemaHash schema{};
    ScopeHash scope{};
    const char* name = nullptr;
    const char* scope_name = nullptr;
    OpFamily family = OpFamily::OTHER;
    uint16_t n_in = 0;
    uint16_t n_out = 0;
    bool grad_enabled = false;
    int64_t out_sizes[4]{};
    uint8_t out_ndim = 0;
    uint64_t data_ptr_in[8]{};
    uint64_t data_ptr_out[4]{};
};

struct Block {
    BlockKind kind = BlockKind::ROOT;
    Phase phase = Phase::FORWARD;
    uint32_t start_op = 0;
    uint32_t end_op = 0;
    uint32_t num_ops = 0;
    std::string label;
    std::string scope_path;
    std::string out_shape;
    int32_t spatial_h = 0;
    int32_t spatial_w = 0;
};

struct DetectionResult {
    std::vector<Block> blocks;
    Architecture architecture = Architecture::GENERIC;
    uint32_t fwd_end = 0;
    uint32_t bwd_end = 0;
    uint32_t optim_start = 0;
};

[[nodiscard]] OpFamily classify_family(std::string_view name);

[[nodiscard]] std::vector<Op> build_ops(const LoadedTrace& trace);

[[nodiscard]] std::string shape_string(const Op& op);

// Depth 3 of "a.b.c.d.e" is "a.b.c".
[[nodiscard]] std::string truncate_scope(const char* path, uint32_t depth);

// Tail 2 of "a.b.c.d.e" is "d.e".
[[nodiscard]] std::string scope_tail(const char* path, uint32_t tail = 2);

Block make_block(std::span<const Op> ops, uint32_t start, uint32_t end, BlockKind kind, Phase phase, std::string label,
                 std::string scope_path = {});

[[nodiscard]] DetectionResult detect_blocks(const LoadedTrace& trace, uint32_t scope_depth = 4);

}  // namespace crucible::vis

// The detection and the string helpers of crucible/vis/BlockDetector.h.

#include <crucible/vis/BlockDetector.h>

#include <crucible/MerkleDag.h>
#include <crucible/SchemaTable.h>
#include <crucible/TraceLoader.h>

#include <algorithm>
#include <bit>
#include <cstring>

namespace crucible::vis {

OpFamily classify_family(std::string_view name) {
    if (name.empty()) return OpFamily::OTHER;
    auto has = [&](std::string_view p) { return name.find(p) != name.npos; };
    if (has("attention") || has("sdp")) return OpFamily::ATTN;
    if (has("loss") || has("nll") || has("mse_loss")) return OpFamily::LOSS;
    if (has("foreach") || has("fused_adam") || has("addcmul") || has("addcdiv") || has("lerp")) return OpFamily::OPTIM;
    if (has("mm") || has("matmul") || has("linear")) return OpFamily::GEMM;
    if (has("conv")) return OpFamily::CONV;
    if (has("norm")) return OpFamily::NORM;
    if (has("gelu") || has("silu") || has("relu") || has("sigmoid") || has("softmax") || has("dropout"))
        return OpFamily::ACT;
    if (has("sum") || has("mean") || has("argmax") || has("topk")) return OpFamily::REDUCE;
    if (has("add") || has("mul") || has("sub") || has("div") || has("neg") || has("exp") || has("log") || has("sqrt")
        || has("pow") || has("sin") || has("cos"))
        return OpFamily::ELEM;
    if (has("view") || has("reshape") || has("permute") || has("transpose") || has("expand") || has("squeeze")
        || has("clone") || has("detach") || has("cat") || has("split") || has("slice") || has("select") || has("arange")
        || has("zeros") || has("ones") || has("copy") || has("to_copy"))
        return OpFamily::MOVE;
    if (has("embedding")) return OpFamily::EMBED;
    if (has("upsample") || has("pool")) return OpFamily::POOL;
    return OpFamily::OTHER;
}

std::vector<Op> build_ops(const LoadedTrace& trace) {
    std::vector<Op> ops(trace.num_ops);
    for (uint32_t i = 0; i < trace.num_ops; i++) {
        const auto& e = trace.entries[i];
        auto& op = ops[i];
        op.idx = i;
        op.schema = e.schema_hash;
        op.scope = trace.scope_hashes[i];
        op.name = schema_short_name(e.schema_hash).value().data();
        // Scope names live in the same table, keyed by the raw hash value.
        // That is why the scope hash is rewrapped as a schema hash here.
        op.scope_name = global_schema_table().lookup(SchemaHash{trace.scope_hashes[i].raw()}).value().data();
        op.family = classify_family(op.name ? std::string_view{op.name} : "");
        op.n_in = e.num_inputs;
        op.n_out = e.num_outputs;
        op.grad_enabled = (e.op_flags & op_flag::GRAD_ENABLED) != 0;

        const auto mi = trace.meta_starts[i];
        if (mi.is_valid()) {
            const uint64_t base = mi.raw();
            for (uint16_t j = 0; j < e.num_inputs && j < 8; j++)
                if (base + j < trace.num_metas)
                    op.data_ptr_in[j] = std::bit_cast<uint64_t>(raw_data_ptr(trace.metas[base + j]));
            for (uint16_t j = 0; j < e.num_outputs && j < 4; j++) {
                const uint64_t mi_out = base + e.num_inputs + j;
                if (mi_out < trace.num_metas) {
                    op.data_ptr_out[j] = std::bit_cast<uint64_t>(raw_data_ptr(trace.metas[mi_out]));
                    if (j == 0) {
                        const auto& m = trace.metas[mi_out];
                        op.out_ndim = m.ndim;
                        for (uint8_t d = 0; d < m.ndim && d < 4; d++)
                            op.out_sizes[d] = raw_tensor_dim(m.sizes[d]);
                    }
                }
            }
        }
    }
    return ops;
}

std::string shape_string(const Op& op) {
    if (op.out_ndim == 0) return {};
    std::string s;
    for (uint8_t d = 0; d < op.out_ndim && d < 4; d++) {
        if (d > 0) s += 'x';
        s += std::to_string(op.out_sizes[d]);
    }
    return s;
}

std::string truncate_scope(const char* path, uint32_t depth) {
    if (!path || !*path) return {};
    std::string_view sv{path};
    size_t pos = 0;
    for (uint32_t d = 0; d < depth; d++) {
        auto dot = sv.find('.', pos);
        if (dot == sv.npos) return std::string{sv};
        pos = dot + 1;
    }
    return std::string{sv.substr(0, pos > 0 ? pos - 1 : 0)};
}

std::string scope_tail(const char* path, uint32_t tail) {
    if (!path || !*path) return "(root)";
    std::string_view sv{path};
    size_t pos = sv.size();
    for (uint32_t i = 0; i < tail; i++) {
        auto dot = sv.rfind('.', pos > 0 ? pos - 1 : 0);
        if (dot == sv.npos) return std::string{sv};
        pos = dot;
    }
    return std::string{sv.substr(pos + 1)};
}

Block make_block(std::span<const Op> ops, uint32_t start, uint32_t end, BlockKind kind, Phase phase, std::string label,
                 std::string scope_path) {
    Block b;
    b.kind = kind;
    b.phase = phase;
    b.start_op = start;
    b.end_op = end;
    b.num_ops = end - start + 1;
    b.scope_path = std::move(scope_path);
    for (uint32_t j = end + 1; j > start; j--) {
        if (ops[j - 1].out_ndim > 0) {
            b.out_shape = shape_string(ops[j - 1]);
            if (ops[j - 1].out_ndim == 4) {
                b.spatial_h = static_cast<int32_t>(ops[j - 1].out_sizes[2]);
                b.spatial_w = static_cast<int32_t>(ops[j - 1].out_sizes[3]);
            }
            break;
        }
    }
    b.label = std::move(label);
    if (!b.out_shape.empty()) b.label += " [" + b.out_shape + "]";
    return b;
}

DetectionResult detect_blocks(const LoadedTrace& trace, uint32_t scope_depth) {
    DetectionResult result;
    auto ops = build_ops(trace);
    if (ops.empty()) return result;
    const auto n = static_cast<uint32_t>(ops.size());

    uint32_t first_bwd = n;
    for (uint32_t i = 0; i < n; i++) {
        if (ops[i].name && std::strstr(ops[i].name, "backward")) {
            first_bwd = i;
            break;
        }
    }
    result.fwd_end = first_bwd > 0 ? first_bwd - 1 : 0;

    uint32_t optim_start = n;
    for (uint32_t i = first_bwd; i < n; i++) {
        if (ops[i].family == OpFamily::OPTIM) {
            optim_start = i;
            break;
        }
    }
    result.bwd_end = optim_start > 0 ? optim_start - 1 : n - 1;
    result.optim_start = optim_start;

    std::string prev_scope;
    uint32_t block_start = 0;

    auto emit_fwd_block = [&](uint32_t start, uint32_t end) {
        std::string label = scope_tail(truncate_scope(ops[start].scope_name, scope_depth).c_str(), 2);
        result.blocks.push_back(make_block(ops, start, end, BlockKind::MODULE, Phase::FORWARD, std::move(label),
                                           truncate_scope(ops[start].scope_name, scope_depth)));
    };

    for (uint32_t i = 0; i <= result.fwd_end; i++) {
        std::string scope = truncate_scope(ops[i].scope_name, scope_depth);
        if (scope != prev_scope && i > block_start) {
            emit_fwd_block(block_start, i - 1);
            block_start = i;
        }
        prev_scope = scope;
    }
    if (block_start <= result.fwd_end) emit_fwd_block(block_start, result.fwd_end);

    // The scope hash is stale during autograd, so the backward range cannot
    // be grouped by scope. It is chunked by op count instead.
    if (first_bwd < optim_start) {
        constexpr uint32_t CHUNK = 30;
        uint32_t bwd_start = first_bwd;
        for (uint32_t i = first_bwd; i < optim_start; i++) {
            if (i - bwd_start >= CHUNK || i == optim_start - 1) {
                result.blocks.push_back(
                    make_block(ops, bwd_start, i, BlockKind::MODULE_BWD, Phase::BACKWARD, "Backward"));
                bwd_start = i + 1;
            }
        }
    }

    if (optim_start < n) {
        uint32_t n_params = 0, last = optim_start;
        for (uint32_t i = optim_start; i < n; i++) {
            if (ops[i].name && std::strcmp(ops[i].name, "addcdiv_") == 0) {
                n_params++;
                last = i;
            }
        }
        result.blocks.push_back(make_block(ops, optim_start, last, BlockKind::OPTIMIZER, Phase::OPTIMIZER,
                                           "Optimizer (" + std::to_string(n_params) + " params)"));

        if (last + 1 < n)
            result.blocks.push_back(
                make_block(ops, last + 1, n - 1, BlockKind::EPILOGUE, Phase::OPTIMIZER, "Epilogue"));
    }

    std::vector<int32_t> res;
    for (const auto& b : result.blocks)
        if (b.phase == Phase::FORWARD && b.spatial_h > 0) res.push_back(b.spatial_h);

    if (res.size() >= 5) {
        auto [lo, hi] = std::ranges::minmax(res);
        if (hi >= 2 * lo)
            result.architecture = Architecture::UNET;
        else if (hi - lo <= 1)
            result.architecture = Architecture::VIT;
    } else if (!res.empty()) {
        auto [lo, hi] = std::ranges::minmax(res);
        if (hi - lo <= 1) result.architecture = Architecture::VIT;
    }

    return result;
}

}  // namespace crucible::vis

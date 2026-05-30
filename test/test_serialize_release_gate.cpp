// Release-mode trust-boundary rejection witness for fix-11.
//
// Serialize.h's deserialize boundary (read_meta / read_header /
// deserialize_region) reconstructs enum fields (ndim / dtype /
// device_type / layout / TraceNodeKind) from untrusted wire bytes and
// wraps each in a fixy::wrap::Refined<Pred, T>.  A Refined ctor enforces
// its `pre(Pred(v))` only under contract semantic enforce/observe; under
// the release preset (-DNDEBUG, semantic=ignore) the clause collapses to
// `[[assume(Pred(v))]]` — NO runtime branch.  fix-11 added the HARD
// (non-contract) `Reader::read_gated` branch BEFORE each Refined ctor so a
// malformed byte is rejected (deserialize_region → LoadedRegionNode{nullptr})
// in release too.
//
// This fixture is the witness that the rejection holds WITH CONTRACTS
// STRIPPED.  It is compiled BOTH as a normal test (default preset,
// contracts enforced) AND — via the direct-compile path documented in the
// fix-11 report — as a -DNDEBUG -O1 binary.  Because `assert()` is a no-op
// under -DNDEBUG, this file uses explicit `if (...) return <nonzero>;`
// checks and exit codes, never `assert`, so the witness survives the
// release preset.  Exit 0 = every malformed byte rejected AND the
// untouched happy path still round-trips; nonzero = a specific failure.

#include <crucible/Serialize.h>
#include <crucible/effects/Capabilities.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace {

// Build a minimal 1-op region with a single input meta and serialize it.
// Returns the byte count (0 on failure).  `out_buf` receives the bytes.
[[nodiscard]] size_t build_and_serialize(crucible::effects::Test bg,
                                         crucible::Arena& arena,
                                         std::span<uint8_t> out_buf) {
    auto* ops = arena.alloc_array<crucible::TraceEntry>(bg.alloc, 1);
    std::uninitialized_value_construct_n(ops, 1);

    ops[0].schema_hash     = crucible::SchemaHash{0xAA};
    ops[0].shape_hash      = crucible::ShapeHash{0x100};
    ops[0].scope_hash      = crucible::ScopeHash{0x200};
    ops[0].callsite_hash   = crucible::CallsiteHash{0x300};
    ops[0].num_inputs      = 1;
    ops[0].num_outputs     = 0;
    ops[0].num_scalar_args = 0;
    ops[0].grad_enabled    = true;

    auto* in_metas = arena.alloc_array<crucible::TensorMeta>(bg.alloc, 1);
    std::uninitialized_value_construct_n(in_metas, 1);
    in_metas[0].ndim        = 1;
    in_metas[0].sizes[0]    = crucible::tensor_dim(8);
    in_metas[0].strides[0]  = crucible::tensor_dim(1);
    in_metas[0].dtype       = crucible::ScalarType::Float;
    in_metas[0].device_type = crucible::DeviceType::CPU;
    in_metas[0].device_idx  = -1;
    in_metas[0].layout      = crucible::Layout::Strided;
    ops[0].input_metas      = in_metas;

    ops[0].input_trace_indices = arena.alloc_array<crucible::OpIndex>(bg.alloc, 1);
    ops[0].input_trace_indices[0] = crucible::OpIndex{0};
    ops[0].input_slot_ids = arena.alloc_array<crucible::SlotId>(bg.alloc, 1);
    ops[0].input_slot_ids[0] = crucible::SlotId{};

    auto* region = crucible::make_region(bg.alloc, arena, ops, 1);
    if (region == nullptr) return 0;
    return crucible::serialize_region(region, nullptr, out_buf);
}

// Wire offsets (little-endian, derived from write_header / write_meta /
// serialize_region in Serialize.h — pinned here as the witness's ground
// truth; a wire-format change that moves these reddens this test, which is
// correct).
//
// Header: magic(4) version(4) kind(1) pad(7) merkle(8) content(8) = 32B.
constexpr size_t kKindOffset = 8;  // first byte after magic+version.

// Region payload after the 32B header:
//   num_ops(4) first_op_schema(8) measured_ms(4) variant_id(4) has_plan(1)
constexpr size_t kRegionPrefix = 32 + 4 + 8 + 4 + 4 + 1;  // = 53
// TraceEntry header:
//   schema(8) shape(8) scope(8) callsite(8) num_inputs(2) num_outputs(2)
//   num_scalar_args(2) grad_enabled(1) op_flags(1) kernel_id(1)
constexpr size_t kEntryHeader = 8 + 8 + 8 + 8 + 2 + 2 + 2 + 1 + 1 + 1;  // = 41
// write_meta layout: sizes[8]*8 + strides[8]*8 + data_ptr(8) + ndim(1)
//   + dtype(1) + ...
constexpr size_t kMetaBase   = kRegionPrefix + kEntryHeader;            // first meta start
constexpr size_t kMetaNdim   = kMetaBase + 64 + 64 + 8;                 // ndim byte
constexpr size_t kMetaDtype  = kMetaNdim + 1;                           // dtype byte
constexpr size_t kMetaDevice = kMetaDtype + 1;                          // device_type byte
constexpr size_t kMetaLayout = kMetaDevice + 1 + 1;                     // +device_idx, layout byte

// Deserialize `bytes` and report whether it was REJECTED (returned a null
// region pointer).
[[nodiscard]] bool is_rejected(crucible::effects::Test bg,
                               std::span<const uint8_t> bytes) {
    crucible::Arena arena(1 << 16);
    auto loaded = crucible::deserialize_region(bg.alloc, bytes, arena);
    return loaded.value() == nullptr;
}

}  // namespace

[[gnu::cold]] int main() {
    auto bg = crucible::effects::testing::test();
    crucible::Arena arena(1 << 16);

    uint8_t buf[65536];
    const size_t n = build_and_serialize(bg, arena, std::span<uint8_t>{buf, sizeof(buf)});
    if (n == 0) { std::fprintf(stderr, "serialize failed\n"); return 1; }

    // ── Sanity: the untouched happy path round-trips (DetSafe preserved). ──
    if (is_rejected(bg, std::span<const uint8_t>{buf, n})) {
        std::fprintf(stderr, "happy path wrongly rejected — gate broke success path\n");
        return 2;
    }

    // For each trust-boundary enum field, corrupt exactly one byte to a
    // value the predicate rejects, and assert the malformed input is
    // rejected.  Each case copies the pristine bytes first.
    struct Corruption { const char* name; size_t off; uint8_t bad; };
    const Corruption cases[] = {
        // TraceNodeKind: valid range [0, TERMINAL]; 99 is far out of range.
        {"header.kind",      kKindOffset,  99},
        // ndim: valid [0, 8]; 200 exceeds kMaxTensorNDim.
        {"meta.ndim",        kMetaNdim,    200},
        // dtype: ScalarType has a gap at 14 (sparse enum); 14 is invalid.
        {"meta.dtype",       kMetaDtype,   14},
        // device_type: gap at 3 (sparse enum); 3 is invalid.
        {"meta.device_type", kMetaDevice,  3},
        // layout: dense [0,5]; 99 is out of range.
        {"meta.layout",      kMetaLayout,  99},
    };

    for (const auto& c : cases) {
        if (c.off >= n) {
            std::fprintf(stderr, "offset for %s (%zu) past wire size %zu — layout drift\n",
                         c.name, c.off, n);
            return 3;
        }
        uint8_t corrupt[65536];
        std::memcpy(corrupt, buf, n);
        // Confirm we are actually flipping the byte to a DIFFERENT value
        // (otherwise the test would pass vacuously).
        if (corrupt[c.off] == c.bad) {
            std::fprintf(stderr, "%s: corrupt byte equals original — no mutation\n", c.name);
            return 4;
        }
        corrupt[c.off] = c.bad;
        if (!is_rejected(bg, std::span<const uint8_t>{corrupt, n})) {
            std::fprintf(stderr,
                "%s: malformed byte 0x%02x at offset %zu NOT rejected "
                "(release-stripped contract let it through)\n",
                c.name, c.bad, c.off);
            return 5;
        }
    }

    std::fprintf(stderr, "OK: all trust-boundary enum gates reject malformed wire bytes\n");
    return 0;
}

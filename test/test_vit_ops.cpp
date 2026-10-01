// The op table and the op packets of test_vit.
//
// The op table below encodes one transformer layer:
//   input → conv(patch_embed) → reshape → add(pos_embed) →
//   layer_norm → mm(Q) → mm(K) → mm(V) → sdpa → mm(out_proj) →
//   add(residual) → layer_norm → mm(mlp_fc1) → gelu → mm(mlp_fc2) →
//   add(residual) → layer_norm → index(CLS) → mm(head) → cross_entropy,
// then eleven backward ops.

#include "vit.h"

#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <bit>
#include <cstdint>

using namespace crucible;

namespace test_vit {

namespace {

constexpr SchemaHash CONV{0xD001}, RESHAPE{0xD002}, ADD{0xD003}, LN{0xD004}, MM{0xD005}, SDPA{0xD006}, GELU{0xD007},
    INDEX{0xD008}, XENT{0xD009}, LOSS_BWD{0xD00A}, MM_BWD{0xD00B}, SCATTER{0xD00C}, LN_BWD{0xD00D}, GELU_BWD{0xD00E},
    SDPA_BWD{0xD00F};

// The high bit of a tensor reference marks it as a parameter rather than
// an activation.
constexpr uint8_t P = 0x80;

struct TSpec {
    uint8_t ref, ndim;
    int64_t d[4];
};

constexpr TSpec pr(uint8_t id, uint8_t n, int64_t a = 0, int64_t b = 0, int64_t c = 0, int64_t e = 0) {
    return {.ref = uint8_t(P | id), .ndim = n, .d = {a, b, c, e}};
}
constexpr TSpec ac(uint8_t id, uint8_t n, int64_t a = 0, int64_t b = 0, int64_t c = 0, int64_t e = 0) {
    return {.ref = id, .ndim = n, .d = {a, b, c, e}};
}

struct OpDef {
    SchemaHash schema;
    ShapeHash shape;
    uint8_t n_in, n_out;
    TSpec t[6];  // max: sdpa_bwd has 4 inputs + 1 output = 5
};

// Parameters (16):
//   P0=input[2,3,8,8]  P1=patch_w[16,3,4,4]  P2=pos_embed[1,4,16]
//   P3=ln1_w[16]  P4=ln1_b[16]  P5=wq[16,16]  P6=wk[16,16]  P7=wv[16,16]
//   P8=wo[16,16]  P9=ln2_w[16]  P10=ln2_b[16]  P11=mlp_w1[16,32]
//   P12=mlp_w2[32,16]  P13=ln_f_w[16]  P14=ln_f_b[16]  P15=head_w[10,16]
//
// Activations (30): A0-A18 (forward), A19-A29 (backward)
//   Key lifetimes: A4(Q), A5(K), A6(V) live from op 4-6 until op 28 (sdpa_bwd)

const OpDef OPS[] = {
    //   Patch embedding
    /*  0 */ {.schema = CONV,
              .shape = ShapeHash{0xD101},
              .n_in = 2,
              .n_out = 1,
              .t = {pr(0, 4, B, 3, 8, 8), pr(1, 4, D, 3, 4, 4), ac(0, 4, B, D, 2, 2)}},
    /*  1 */
    {.schema = RESHAPE,
     .shape = ShapeHash{0xD102},
     .n_in = 1,
     .n_out = 1,
     .t = {ac(0, 4, B, D, 2, 2), ac(1, 3, B, SEQ, D)}},
    /*  2 */
    {.schema = ADD,
     .shape = ShapeHash{0xD103},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(1, 3, B, SEQ, D), pr(2, 3, 1, SEQ, D), ac(2, 3, B, SEQ, D)}},

    //   Transformer layer: self-attention
    /*  3 */
    {.schema = LN,
     .shape = ShapeHash{0xD104},
     .n_in = 3,
     .n_out = 1,
     .t = {ac(2, 3, B, SEQ, D), pr(3, 1, D), pr(4, 1, D), ac(3, 3, B, SEQ, D)}},
    /*  4 */
    {.schema = MM,
     .shape = ShapeHash{0xD105},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(3, 3, B, SEQ, D), pr(5, 2, D, D), ac(4, 3, B, SEQ, D)}},  // Q
    /*  5 */
    {.schema = MM,
     .shape = ShapeHash{0xD106},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(3, 3, B, SEQ, D), pr(6, 2, D, D), ac(5, 3, B, SEQ, D)}},  // K
    /*  6 */
    {.schema = MM,
     .shape = ShapeHash{0xD107},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(3, 3, B, SEQ, D), pr(7, 2, D, D), ac(6, 3, B, SEQ, D)}},  // V
    /*  7 */
    {.schema = SDPA,
     .shape = ShapeHash{0xD108},
     .n_in = 3,
     .n_out = 1,
     .t = {ac(4, 3, B, SEQ, D), ac(5, 3, B, SEQ, D), ac(6, 3, B, SEQ, D), ac(7, 3, B, SEQ, D)}},
    /*  8 */
    {.schema = MM,
     .shape = ShapeHash{0xD109},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(7, 3, B, SEQ, D), pr(8, 2, D, D), ac(8, 3, B, SEQ, D)}},  // out proj
    /*  9 */
    {.schema = ADD,
     .shape = ShapeHash{0xD10A},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(8, 3, B, SEQ, D), ac(2, 3, B, SEQ, D), ac(9, 3, B, SEQ, D)}},  // residual 1

    //   Transformer layer: MLP
    /* 10 */
    {.schema = LN,
     .shape = ShapeHash{0xD10B},
     .n_in = 3,
     .n_out = 1,
     .t = {ac(9, 3, B, SEQ, D), pr(9, 1, D), pr(10, 1, D), ac(10, 3, B, SEQ, D)}},
    /* 11 */
    {.schema = MM,
     .shape = ShapeHash{0xD10C},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(10, 3, B, SEQ, D), pr(11, 2, D, MLP), ac(11, 3, B, SEQ, MLP)}},  // expand
    /* 12 */
    {.schema = GELU,
     .shape = ShapeHash{0xD10D},
     .n_in = 1,
     .n_out = 1,
     .t = {ac(11, 3, B, SEQ, MLP), ac(12, 3, B, SEQ, MLP)}},
    /* 13 */
    {.schema = MM,
     .shape = ShapeHash{0xD10E},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(12, 3, B, SEQ, MLP), pr(12, 2, MLP, D), ac(13, 3, B, SEQ, D)}},  // contract
    /* 14 */
    {.schema = ADD,
     .shape = ShapeHash{0xD10F},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(13, 3, B, SEQ, D), ac(9, 3, B, SEQ, D), ac(14, 3, B, SEQ, D)}},  // residual 2

    //   Classification head
    /* 15 */
    {.schema = LN,
     .shape = ShapeHash{0xD110},
     .n_in = 3,
     .n_out = 1,
     .t = {ac(14, 3, B, SEQ, D), pr(13, 1, D), pr(14, 1, D), ac(15, 3, B, SEQ, D)}},
    /* 16 */
    {.schema = INDEX, .shape = ShapeHash{0xD111}, .n_in = 1, .n_out = 1, .t = {ac(15, 3, B, SEQ, D), ac(16, 2, B, D)}},
    /* 17 */
    {.schema = MM,
     .shape = ShapeHash{0xD112},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(16, 2, B, D), pr(15, 2, CL, D), ac(17, 2, B, CL)}},
    /* 18 */
    {.schema = XENT, .shape = ShapeHash{0xD113}, .n_in = 1, .n_out = 1, .t = {ac(17, 2, B, CL), ac(18, 2, B, CL)}},

    //   Backward
    /* 19 */
    {.schema = LOSS_BWD, .shape = ShapeHash{0xD114}, .n_in = 1, .n_out = 1, .t = {ac(18, 2, B, CL), ac(19, 2, B, CL)}},
    /* 20 */
    {.schema = MM_BWD,
     .shape = ShapeHash{0xD115},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(19, 2, B, CL), pr(15, 2, CL, D), ac(20, 2, B, D)}},
    /* 21 */
    {.schema = SCATTER,
     .shape = ShapeHash{0xD116},
     .n_in = 1,
     .n_out = 1,
     .t = {ac(20, 2, B, D), ac(21, 3, B, SEQ, D)}},
    /* 22 */
    {.schema = LN_BWD,
     .shape = ShapeHash{0xD117},
     .n_in = 1,
     .n_out = 1,
     .t = {ac(21, 3, B, SEQ, D), ac(22, 3, B, SEQ, D)}},
    /* 23 */
    {.schema = MM_BWD,
     .shape = ShapeHash{0xD118},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(22, 3, B, SEQ, D), pr(12, 2, MLP, D), ac(23, 3, B, SEQ, MLP)}},
    /* 24 */
    {.schema = GELU_BWD,
     .shape = ShapeHash{0xD119},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(23, 3, B, SEQ, MLP), ac(11, 3, B, SEQ, MLP), ac(24, 3, B, SEQ, MLP)}},
    /* 25 */
    {.schema = MM_BWD,
     .shape = ShapeHash{0xD11A},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(24, 3, B, SEQ, MLP), pr(11, 2, D, MLP), ac(25, 3, B, SEQ, D)}},
    /* 26 */
    {.schema = LN_BWD,
     .shape = ShapeHash{0xD11B},
     .n_in = 1,
     .n_out = 1,
     .t = {ac(25, 3, B, SEQ, D), ac(26, 3, B, SEQ, D)}},
    /* 27 */
    {.schema = MM_BWD,
     .shape = ShapeHash{0xD11C},
     .n_in = 2,
     .n_out = 1,
     .t = {ac(26, 3, B, SEQ, D), pr(8, 2, D, D), ac(27, 3, B, SEQ, D)}},
    /* 28 */
    {.schema = SDPA_BWD,
     .shape = ShapeHash{0xD11D},
     .n_in = 4,
     .n_out = 1,
     .t = {ac(27, 3, B, SEQ, D), ac(4, 3, B, SEQ, D), ac(5, 3, B, SEQ, D), ac(6, 3, B, SEQ, D), ac(28, 3, B, SEQ, D)}},
    /* 29 */
    {.schema = LN_BWD,
     .shape = ShapeHash{0xD11E},
     .n_in = 1,
     .n_out = 1,
     .t = {ac(28, 3, B, SEQ, D), ac(29, 3, B, SEQ, D)}},
};

static_assert(sizeof(OPS) / sizeof(OPS[0]) == NUM_OPS, "ViT: 19 forward + 11 backward");

void* param_ptr(uint8_t idx) { return std::bit_cast<void*>(static_cast<std::uintptr_t>((idx + 1) * 0x10000)); }

void* act_ptr(uint32_t iter, uint8_t idx) {
    return std::bit_cast<void*>((static_cast<std::uintptr_t>(iter) + 1) * 0x1000000ULL
                                + (static_cast<std::uintptr_t>(idx) + 1) * 0x10000);
}

TensorMeta make_meta(const TSpec& s, uint32_t iter) {
    TensorMeta m{};
    m.data_ptr = external_data_ptr((s.ref & P) ? param_ptr(s.ref & 0x7F) : act_ptr(iter, s.ref));
    m.dtype = ScalarType::Float;
    m.device_type = DeviceType::CPU;
    m.ndim = s.ndim;
    for (uint8_t i = 0; i < s.ndim; i++)
        m.sizes[i] = ::crucible::tensor_dim(s.d[i]);
    if (s.ndim > 0) {
        const std::size_t ndim = static_cast<std::size_t>(s.ndim);
        m.strides[ndim - 1] = ::crucible::tensor_dim(1);
        for (std::size_t i = ndim - 1; i-- > 0;)
            m.strides[i] = ::crucible::tensor_dim(::crucible::raw_tensor_dim(m.strides[i + 1])
                                                  * ::crucible::raw_tensor_dim(m.sizes[i + 1]));
    }
    return m;
}

}  // namespace

OpPacket build_op(uint32_t op_idx, uint32_t iter) {
    OpPacket pkt;
    const auto& op = OPS[op_idx];
    pkt.entry.schema_hash = op.schema;
    pkt.entry.shape_hash = op.shape;
    pkt.entry.num_inputs = op.n_in;
    pkt.entry.num_outputs = op.n_out;
    pkt.n_metas = static_cast<uint16_t>(op.n_in + op.n_out);
    for (uint8_t i = 0; i < pkt.n_metas; i++)
        pkt.metas[i] = make_meta(op.t[i], iter);
    return pkt;
}

void feed_iteration(Vigil& v, uint32_t iter) {
    for (uint32_t i = 0; i < NUM_OPS; i++) {
        auto p = build_op(i, iter);
        assert(v.record_op(crucible::test::certify_synthetic_entry(p.entry), p.metas, p.n_metas));
    }
}

void feed_trigger(Vigil& v, uint32_t iter) {
    for (uint32_t i = 0; i < IterationDetector::K; i++) {
        auto p = build_op(i, iter);
        assert(v.record_op(crucible::test::certify_synthetic_entry(p.entry), p.metas, p.n_metas));
    }
}

}  // namespace test_vit

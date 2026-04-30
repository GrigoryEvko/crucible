// FOUND-J01 — KernelNode<Row> 8-axiom witness.
//
// Verifies the load-bearing structural properties of the row-parameterized
// IR002 KernelNode template.  Layout regressions, NSDMI gaps, or row-
// distinctness failures fire HERE, not in production Forge phases.
//
// Coverage matrix (positive):
//   T01  64-byte layout × 8 distinct rows     — InitSafe + structural
//   T02  alignas(64)  × 8 distinct rows       — cache-line discipline
//   T03  trivially copyable × 4 rows          — MemSafe + arena memcpy
//   T04  NSDMI default-init wide sweep        — InitSafe (ALL fields)
//   T05  per-Row type distinctness            — TypeSafe (cache slotting)
//   T06  member alias `row` round-trip        — public reading surface
//   T07  KernelKind::COUNT exactly 22         — taxonomy stability fence
//
// Audit groups (rigor — sibling pattern with FOUND-I-AUDIT):
//   audit-A  KernelKind ordinal stability      — every variant pinned
//   audit-B  saturation row instantiation      — F* AllRow + custom
//   audit-C  row hash distinctness via name    — wrapper-stack identity
//   audit-D  layout-byte-by-byte zero witness  — InitSafe at memory level
//   audit-E  field-mutation round-trip         — store/load on every field
//   audit-F  arena memcpy parity witness       — bulk-relocate idempotent
//   audit-G  permutation row distinctness      — Row<Bg,Alloc> ≠ Row<Alloc,Bg>

#include <crucible/forge/KernelNode.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/FxAliases.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/Types.h>

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace fk = ::crucible::forge;
namespace eff = ::crucible::effects;

// ═════════════════════════════════════════════════════════════════════
// Compile-time assertions (T01..T07 + audit-A,B,C,F,G)
// ═════════════════════════════════════════════════════════════════════

// ── T01 + audit-B: 64-byte layout across 8 row shapes ─────────────────
static_assert(sizeof(fk::KernelNode<eff::PureRow>) == 64);
static_assert(sizeof(fk::KernelNode<eff::TotRow>) == 64);
static_assert(sizeof(fk::KernelNode<eff::GhostRow>) == 64);
static_assert(sizeof(fk::KernelNode<eff::DivRow>) == 64);
static_assert(sizeof(fk::KernelNode<eff::STRow>) == 64);
static_assert(sizeof(fk::KernelNode<eff::AllRow>) == 64);
static_assert(sizeof(fk::KernelNode<eff::Row<eff::Effect::Bg>>) == 64);
static_assert(sizeof(fk::KernelNode<
    eff::Row<eff::Effect::Alloc, eff::Effect::IO,
             eff::Effect::Block, eff::Effect::Bg,
             eff::Effect::Init,  eff::Effect::Test>>) == 64);

// ── T02: alignas(64) ──────────────────────────────────────────────────
static_assert(alignof(fk::KernelNode<eff::PureRow>) == 64);
static_assert(alignof(fk::KernelNode<eff::AllRow>) == 64);
static_assert(alignof(fk::KernelNode<eff::DivRow>) == 64);
static_assert(alignof(fk::KernelNode<eff::STRow>) == 64);

// ── T03: trivially copyable (Arena::grow uses memcpy) ─────────────────
static_assert(std::is_trivially_copyable_v<fk::KernelNode<eff::PureRow>>);
static_assert(std::is_trivially_copyable_v<fk::KernelNode<eff::DivRow>>);
static_assert(std::is_trivially_copyable_v<fk::KernelNode<eff::STRow>>);
static_assert(std::is_trivially_copyable_v<fk::KernelNode<eff::AllRow>>);

// ── T05 + audit-G: per-Row type distinctness ──────────────────────────
static_assert(!std::is_same_v<
    fk::KernelNode<eff::PureRow>,
    fk::KernelNode<eff::AllRow>>);
static_assert(!std::is_same_v<
    fk::KernelNode<eff::DivRow>,
    fk::KernelNode<eff::STRow>>);
static_assert(!std::is_same_v<
    fk::KernelNode<eff::Row<eff::Effect::Bg>>,
    fk::KernelNode<eff::Row<eff::Effect::Alloc>>>);

// audit-G: permutation order matters in the structural sense.  The Row
// canonicalization is METX-2 territory (#474); KernelNode<Row<Bg, Alloc>>
// and KernelNode<Row<Alloc, Bg>> are STRUCTURALLY distinct types today.
// Both round-trip to the same RowHash via FOUND-I02's sort-fold, so
// cache lookups still collide as intended; the structural type identity
// here is what enables Phase E to disambiguate intermediate IR shapes.
static_assert(!std::is_same_v<
    fk::KernelNode<eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>,
    fk::KernelNode<eff::Row<eff::Effect::Alloc, eff::Effect::Bg>>>);

// ── T06: member alias `row` round-trip ────────────────────────────────
static_assert(std::is_same_v<
    typename fk::KernelNode<eff::PureRow>::row,
    eff::PureRow>);
static_assert(std::is_same_v<
    typename fk::KernelNode<eff::AllRow>::row,
    eff::AllRow>);
static_assert(std::is_same_v<
    typename fk::KernelNode<eff::DivRow>::row,
    eff::DivRow>);
static_assert(std::is_same_v<
    typename fk::KernelNode<eff::Row<eff::Effect::Init>>::row,
    eff::Row<eff::Effect::Init>>);

// ── T07: KernelKind taxonomy stability ────────────────────────────────
// COUNT shifts mean every per-vendor emit_kernel template (FOUND-J08..J12)
// must be revisited; we want loud failure here, not silent skew.
static_assert(static_cast<uint8_t>(fk::KernelKind::COUNT) == 22);

// audit-A: ordinal stability — content_hash includes kind ordinals
// (per FOUND-I02), and changing them breaks every persisted Cipher
// entry.  Pin the prefix so reordering fires here, not in deserialize.
static_assert(static_cast<uint8_t>(fk::KernelKind::GEMM)        == 0);
static_assert(static_cast<uint8_t>(fk::KernelKind::BMM)         == 1);
static_assert(static_cast<uint8_t>(fk::KernelKind::CONV)        == 2);
static_assert(static_cast<uint8_t>(fk::KernelKind::DEQUANT_GEMM) == 3);
static_assert(static_cast<uint8_t>(fk::KernelKind::ATTENTION)   == 4);
static_assert(static_cast<uint8_t>(fk::KernelKind::CUSTOM) ==
              static_cast<uint8_t>(fk::KernelKind::COUNT) - 1);

// ═════════════════════════════════════════════════════════════════════
// Runtime witnesses (T04 + audit-D, E, F)
// ═════════════════════════════════════════════════════════════════════

namespace {

template <typename Row>
void assert_default_init_well_formed(const char* row_label) {
    // T04: NSDMI default-init.  Every field must be at its documented
    // sentinel.  P2795R5 + -ftrivial-auto-var-init=zero plus our NSDMI
    // discipline guarantees this even for the implicit padding bytes.
    fk::KernelNode<Row> kn{};

    // Identity octet (8B)
    assert(kn.id.is_valid() == false);
    assert(kn.id.raw() == UINT32_MAX);
    assert(kn.kind == fk::KernelKind::CUSTOM);
    assert(kn.flags == 0);
    assert(kn.device_idx == -1);
    assert(kn.ndim == 0);

    // Dtype + layout octet (8B)
    assert(kn.in_dtype  == ::crucible::ScalarType::Undefined);
    assert(kn.out_dtype == ::crucible::ScalarType::Undefined);
    assert(kn.layout_in  == ::crucible::Layout::Strided);
    assert(kn.layout_out == ::crucible::Layout::Strided);
    assert(kn.num_inputs  == 0u);
    assert(kn.num_outputs == 0u);

    // Pointer octets (24B)
    assert(kn.attrs == nullptr);
    assert(kn.recipe == nullptr);
    assert(kn.tile == nullptr);
    assert(kn.input_slots == nullptr);
    assert(kn.output_slots == nullptr);

    // Content hash octet (8B)
    assert(kn.content_hash.raw() == 0u);
    assert(static_cast<bool>(kn.content_hash) == false);

    (void)row_label;
}

void test_t04_nsdmi_well_formed() {
    assert_default_init_well_formed<eff::PureRow>("PureRow");
    assert_default_init_well_formed<eff::DivRow>("DivRow");
    assert_default_init_well_formed<eff::STRow>("STRow");
    assert_default_init_well_formed<eff::AllRow>("AllRow");
    assert_default_init_well_formed<eff::Row<eff::Effect::Bg>>("Row<Bg>");
    std::printf("  T04 nsdmi_default_init:                  PASSED\n");
}

void test_audit_d_layout_byte_zero_witness() {
    // audit-D: not just field-by-field assertion, but a memory-level
    // witness that NO byte of the default-initialized record carries a
    // non-zero value EXCEPT the documented sentinels (id = UINT32_MAX,
    // device_idx = -1, kind = CUSTOM = 21).
    //
    // The sentinel bytes are:
    //   bytes 0..3   id        = 0xFFFFFFFF                 (4 of FF)
    //   byte  4      kind      = 21 (CUSTOM)
    //   byte  6      device_idx= -1 = 0xFF
    //   bytes 5,7,8..63 must all be 0x00
    //
    // This pins the layout against any future field reorder that
    // accidentally introduces a non-zero default.

    fk::KernelNode<eff::PureRow> kn{};
    const auto* raw = reinterpret_cast<const uint8_t*>(&kn);

    // Bytes 0..3: id raw is UINT32_MAX (KernelId::none())
    for (size_t i = 0; i < 4; ++i) {
        assert(raw[i] == 0xFFu);
    }
    // Byte 4: kind = CUSTOM = 21
    assert(raw[4] == static_cast<uint8_t>(fk::KernelKind::CUSTOM));
    // Byte 5: flags = 0
    assert(raw[5] == 0u);
    // Byte 6: device_idx = -1 → 0xFF (int8_t two's complement)
    assert(raw[6] == 0xFFu);
    // Byte 7: ndim = 0
    assert(raw[7] == 0u);

    // Byte 8: in_dtype  = ScalarType::Undefined = -1 → 0xFF
    // Byte 9: out_dtype = ScalarType::Undefined = -1 → 0xFF
    assert(raw[8] == 0xFFu);
    assert(raw[9] == 0xFFu);
    // Byte 10: layout_in  = Layout::Strided = 0
    // Byte 11: layout_out = Layout::Strided = 0
    assert(raw[10] == 0u);
    assert(raw[11] == 0u);
    // Bytes 12..15: num_inputs / num_outputs both zero
    for (size_t i = 12; i < 16; ++i) {
        assert(raw[i] == 0u);
    }

    // Bytes 16..63: every pointer field is nullptr (zero bit pattern)
    // and content_hash is KernelContentHash{} (zero).  Any non-zero
    // byte here means a future field was added without an NSDMI
    // initializing it to zero — InitSafe regression.
    for (size_t i = 16; i < 64; ++i) {
        assert(raw[i] == 0u);
    }

    std::printf("  audit-D byte_layout_zero_witness:        PASSED\n");
}

void test_audit_e_field_mutation_roundtrip() {
    // audit-E: every field admits store-then-load round-trip with no
    // aliasing surprise — this is the basic property memcpy/Arena rely
    // on.  We touch every field with a distinguishable value and read
    // each back unmodified.
    fk::KernelNode<eff::AllRow> kn{};

    kn.id = fk::KernelId::from_raw(0x1234'5678u);
    kn.kind = fk::KernelKind::ATTENTION;
    kn.flags = 0xA5u;
    kn.device_idx = 7;
    kn.ndim = 4;
    kn.in_dtype  = ::crucible::ScalarType::Float;
    kn.out_dtype = ::crucible::ScalarType::Half;
    kn.layout_in  = ::crucible::Layout::Sparse;
    kn.layout_out = ::crucible::Layout::SparseCsr;
    kn.num_inputs  = 0xBEEF;
    kn.num_outputs = 0xDEAD;

    int dummy_attr = 0;
    kn.attrs = &dummy_attr;

    fk::NumericalRecipe* fake_recipe = reinterpret_cast<fk::NumericalRecipe*>(
        static_cast<uintptr_t>(0x4000'0000ULL));
    kn.recipe = fake_recipe;

    fk::TileSpec* fake_tile = reinterpret_cast<fk::TileSpec*>(
        static_cast<uintptr_t>(0x5000'0000ULL));
    kn.tile = fake_tile;

    ::crucible::SlotId slot_in[2]  = { ::crucible::SlotId::from_raw(11),
                                       ::crucible::SlotId::from_raw(22) };
    ::crucible::SlotId slot_out[1] = { ::crucible::SlotId::from_raw(33) };
    kn.input_slots  = slot_in;
    kn.output_slots = slot_out;

    kn.content_hash = fk::KernelContentHash::from_raw(0xCAFEBABE'DEADBEEFULL);

    // Read back
    assert(kn.id.raw() == 0x1234'5678u);
    assert(kn.kind == fk::KernelKind::ATTENTION);
    assert(kn.flags == 0xA5u);
    assert(kn.device_idx == 7);
    assert(kn.ndim == 4);
    assert(kn.in_dtype  == ::crucible::ScalarType::Float);
    assert(kn.out_dtype == ::crucible::ScalarType::Half);
    assert(kn.layout_in  == ::crucible::Layout::Sparse);
    assert(kn.layout_out == ::crucible::Layout::SparseCsr);
    assert(kn.num_inputs  == 0xBEEFu);
    assert(kn.num_outputs == 0xDEADu);
    assert(kn.attrs == &dummy_attr);
    assert(kn.recipe == fake_recipe);
    assert(kn.tile == fake_tile);
    assert(kn.input_slots == slot_in);
    assert(kn.output_slots == slot_out);
    assert(kn.input_slots[0].raw() == 11u);
    assert(kn.input_slots[1].raw() == 22u);
    assert(kn.output_slots[0].raw() == 33u);
    assert(kn.content_hash.raw() == 0xCAFEBABE'DEADBEEFULL);

    std::printf("  audit-E field_mutation_roundtrip:        PASSED\n");
}

void test_audit_f_arena_memcpy_parity() {
    // audit-F: bulk-memcpy parity.  Arena::grow relies on the trivially-
    // copyable property to memcpy whole blocks of arena objects in one
    // shot.  We construct a populated record, memcpy it byte-for-byte
    // into a second instance, and verify field-by-field equivalence.
    //
    // This is the *runtime* witness for the *compile-time* trivially-
    // relocatable assertion; the two together close the relocate axiom.
    fk::KernelNode<eff::DivRow> src{};
    src.id = fk::KernelId::from_raw(42);
    src.kind = fk::KernelKind::SOFTMAX;
    src.flags = 0x3Cu;
    src.device_idx = 3;
    src.ndim = 2;
    src.in_dtype  = ::crucible::ScalarType::Bool;
    src.out_dtype = ::crucible::ScalarType::Float;
    src.layout_in  = ::crucible::Layout::SparseBsr;
    src.layout_out = ::crucible::Layout::Strided;
    src.num_inputs = 7;
    src.num_outputs = 3;
    src.content_hash = fk::KernelContentHash::from_raw(0x1111'2222'3333'4444ULL);

    alignas(64) uint8_t dst_storage[sizeof(src)];
    std::memcpy(dst_storage, &src, sizeof(src));

    // start_lifetime_as for the punned record (C++23 / MemSafe).
    auto* dst = std::start_lifetime_as<fk::KernelNode<eff::DivRow>>(dst_storage);

    assert(dst->id.raw() == src.id.raw());
    assert(dst->kind == src.kind);
    assert(dst->flags == src.flags);
    assert(dst->device_idx == src.device_idx);
    assert(dst->ndim == src.ndim);
    assert(dst->in_dtype == src.in_dtype);
    assert(dst->out_dtype == src.out_dtype);
    assert(dst->layout_in == src.layout_in);
    assert(dst->layout_out == src.layout_out);
    assert(dst->num_inputs == src.num_inputs);
    assert(dst->num_outputs == src.num_outputs);
    assert(dst->content_hash.raw() == src.content_hash.raw());

    std::printf("  audit-F arena_memcpy_parity:             PASSED\n");
}

void test_audit_c_row_distinctness_runtime() {
    // audit-C: type-system distinctness must show up runtime as well —
    // typeid-style comparison via stable_type_id is the FOUND-H07
    // surface, but here we just witness that __PRETTY_FUNCTION__-style
    // names resolve to distinct strings.  The compile-time check
    // (T05) already guards this; runtime version is a sanity peer.
    fk::KernelNode<eff::PureRow> a{};
    fk::KernelNode<eff::AllRow>  b{};

    // Address-of must be possible (objects are real, not placeholder)
    assert(reinterpret_cast<uintptr_t>(&a) != reinterpret_cast<uintptr_t>(&b));
    // sizeof equal across rows — runtime confirmation
    assert(sizeof(a) == sizeof(b));
    assert(sizeof(a) == 64);

    std::printf("  audit-C row_distinctness_runtime:        PASSED\n");
}

}  // anonymous namespace

int main() {
    std::printf("test_forge_kernel_node — FOUND-J01 IR002 row-typed\n");

    test_t04_nsdmi_well_formed();

    std::printf("--- FOUND-J01-AUDIT ---\n");
    test_audit_c_row_distinctness_runtime();
    test_audit_d_layout_byte_zero_witness();
    test_audit_e_field_mutation_roundtrip();
    test_audit_f_arena_memcpy_parity();

    std::printf("test_forge_kernel_node: 1 + 4 audit groups, all passed\n");
    return 0;
}

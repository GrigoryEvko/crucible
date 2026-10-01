#include <crucible/SchemaTable.h>

#include "padding_bytes.h"
#include "test_assert.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace crucible;
namespace source = ::fixy::tags::source;

static SchemaHash H(uint64_t v) { return SchemaHash{v}; }

// The inputs here are string literals in the source, so crucible produced
// them: they register as internal names.
static SchemaTable::InternalName S(const char* s) { return ::fixy::mint_tagged<source::FromInternal>(s); }

static const char* C(SchemaTable::LookupName name) { return name.value().data(); }

static bool missing(SchemaTable::LookupName name) { return name.value().data() == nullptr; }

static bool eq(SchemaTable::LookupName name, const char* expected) {
    const auto& view = name.value();
    return view.data() != nullptr && view.size() == std::strlen(expected) && std::strcmp(view.data(), expected) == 0;
}

// A registration into an open table always lands.
template <SchemaNameSource Tag>
static void reg(SchemaTable& t, SchemaTable::MutableView const& view, SchemaHash hash, SchemaTable::Name<Tag> name) {
    const bool was_registered = t.register_name(view, hash, name);
    assert(was_registered);
}

// The table admits exactly two provenances, and a looked-up name claims
// neither of them: it is the table's interned copy.
static_assert(SchemaNameSource<source::Sanitized>);
static_assert(SchemaNameSource<source::FromInternal>);
static_assert(!SchemaNameSource<source::External>);
static_assert(!SchemaNameSource<source::ABIBoundary>);
static_assert(!SchemaNameSource<source::Interned>);
static_assert(std::is_same_v<SchemaTable::LookupName::tag_type, source::Interned>);

template <typename Tag>
concept registers_as = requires(SchemaTable& t, SchemaTable::MutableView const& v, ::fixy::Tagged<const char*, Tag> n) {
    t.register_name(v, SchemaHash{1}, n);
};
static_assert(registers_as<source::Sanitized>);
static_assert(registers_as<source::FromInternal>);
static_assert(!registers_as<source::External>, "a raw external name must not reach the table");
static_assert(!registers_as<source::ABIBoundary>, "a C ABI name must pass its boundary check first");

// The test context that lets a test reopen a table it reuses.
static ::foundation::effects::Test test_ctx() { return ::foundation::effects::testing::test(); }

// A mutable view asks for the context of a Vigil's producer claim.  These
// tests use no Vigil, so they take that context from the test door.
static constexpr VigilFgCtx kVigilForeground = ::foundation::effects::testing::foreground<Vigil>();

static void test_empty_lookup_returns_nullptr() {
    SchemaTable t;
    assert(missing(t.lookup(H(0xDEAD))));
    assert(missing(t.short_name(H(0xDEAD))));
    assert(t.count() == 0);
    std::printf("  test_empty:                     PASSED\n");
}

static void test_register_and_lookup() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground);
    assert(mv.has_value());
    reg(t, *mv, H(0x100), S("aten::mm"));
    reg(t, *mv, H(0x200), S("aten::add.Tensor"));
    reg(t, *mv, H(0x300), S("aten::linear"));

    assert(eq(t.lookup(H(0x100)), "aten::mm"));
    assert(eq(t.lookup(H(0x200)), "aten::add.Tensor"));
    assert(eq(t.lookup(H(0x300)), "aten::linear"));
    assert(t.count() == 3);
    std::printf("  test_register_lookup:           PASSED\n");
}

static void test_short_name_strips_aten_prefix() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground);
    assert(mv.has_value());
    reg(t, *mv, H(0x100), S("aten::mm"));
    reg(t, *mv, H(0x200), S("aten::scaled_dot_product_attention"));
    reg(t, *mv, H(0x300), S("prim::TupleConstruct"));  // non-aten

    assert(eq(t.short_name(H(0x100)), "mm"));
    assert(eq(t.short_name(H(0x200)), "scaled_dot_product_attention"));
    // Non-aten names pass through unchanged.
    assert(eq(t.short_name(H(0x300)), "prim::TupleConstruct"));
    std::printf("  test_short_name:                PASSED\n");
}

static void test_idempotent_re_register() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground);
    assert(mv.has_value());
    reg(t, *mv, H(0x42), S("first"));
    assert(t.count() == 1);
    reg(t, *mv, H(0x42), S("updated"));  // same hash, new name
    assert(t.count() == 1);  // no duplicate
    assert(eq(t.lookup(H(0x42)), "updated"));
    std::printf("  test_re_register:               PASSED\n");
}

static void test_binary_search_across_many() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground);
    assert(mv.has_value());
    constexpr uint32_t N = 256;
    char names[N][16];
    for (uint32_t i = 0; i < N; ++i) {
        std::snprintf(names[i], sizeof(names[i]), "op_%u", i);
        // Deterministic but shuffled — test sort-on-insert.
        const uint64_t key = 0x9E3779B97F4A7C15ULL * (i + 1);
        reg(t, *mv, SchemaHash{key}, S(names[i]));
    }
    assert(t.count() == N);
    for (uint32_t i = 0; i < N; ++i) {
        const uint64_t key = 0x9E3779B97F4A7C15ULL * (i + 1);
        const char* got = C(t.lookup(SchemaHash{key}));
        assert(got != nullptr);
        assert(std::strcmp(got, names[i]) == 0);
    }
    assert(missing(t.lookup(H(0xCAFEBABEDEADBEEFULL))));
    std::printf("  test_binary_search:             PASSED\n");
}

static void test_global_table_convenience() {
    global_schema_table().clear(test_ctx());  // this table outlives the test
    const auto gv = global_schema_table().mint_mutable_view(kVigilForeground);
    assert(gv.has_value());
    const bool was_registered = register_schema_name(*gv, H(0xAA), S("aten::relu"));
    assert(was_registered);
    assert(eq(schema_name(H(0xAA)), "aten::relu"));
    assert(eq(schema_short_name(H(0xAA)), "relu"));
    assert(missing(schema_name(H(0xBB))));
    global_schema_table().clear(test_ctx());
    std::printf("  test_global_helpers:            PASSED\n");
}

static void test_null_name_is_noop() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground);
    assert(mv.has_value());
    reg(t, *mv, H(0x77), S(nullptr));  // must not crash or corrupt
    assert(t.count() == 0);
    assert(missing(t.lookup(H(0x77))));
    std::printf("  test_null_name:                 PASSED\n");
}

static void test_default_is_mutable_and_seal_flips() {
    SchemaTable t;
    assert(!t.is_sealed());
    t.seal();
    assert(t.is_sealed());
    // Idempotent: re-seal keeps the state sealed.
    t.seal();
    assert(t.is_sealed());
    std::printf("  test_seal_flips:                PASSED\n");
}

static void test_sealed_table_mints_no_mutable_view() {
    // The check is a plain branch, so it holds in a build with NDEBUG as
    // well: a sealed table hands out no view that could write to it.
    SchemaTable t;
    t.seal();
    assert(!t.mint_mutable_view(kVigilForeground).has_value());
    assert(t.count() == 0);
    std::printf("  test_sealed_no_mutable_view:    PASSED\n");
}

static void test_clear_resets_seal() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground);
    assert(mv.has_value());
    reg(t, *mv, H(0xAB), S("aten::matmul"));
    t.seal();
    assert(t.is_sealed());
    assert(t.count() == 1);

    t.clear(test_ctx());
    assert(!t.is_sealed());
    assert(t.count() == 0);
    // After clear, the table is Mutable again — register works.
    const auto mv_after_clear = t.mint_mutable_view(kVigilForeground);
    assert(mv_after_clear.has_value());
    reg(t, *mv_after_clear, H(0xCD), S("aten::add"));
    assert(t.count() == 1);
    assert(eq(t.lookup(H(0xCD)), "aten::add"));
    std::printf("  test_clear_resets_seal:         PASSED\n");
}

static void test_write_after_seal_through_an_older_view_is_refused() {
    // The view proves only that the table was open when it was minted.  A
    // seal that lands between the mint and the write must refuse the write.
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground);
    assert(mv.has_value());
    reg(t, *mv, H(0x10), S("aten::before"));
    t.seal();
    const bool was_registered = t.register_name(*mv, H(0x20), S("aten::after"));
    assert(!was_registered);
    assert(t.count() == 1);
    assert(missing(t.lookup(H(0x20))));
    // A write to an existing hash is refused too, and keeps the old name.
    const bool was_renamed = t.register_name(*mv, H(0x10), S("aten::renamed"));
    assert(!was_renamed);
    assert(eq(t.lookup(H(0x10)), "aten::before"));
    std::printf("  test_write_after_seal_refused:  PASSED\n");
}

static void test_typed_register_with_mutable_view() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground);
    assert(mv.has_value());
    reg(t, *mv, H(0xBEEF), S("aten::conv2d"));
    assert(eq(t.lookup(H(0xBEEF)), "aten::conv2d"));
    std::printf("  test_typed_register:            PASSED\n");
}

static void test_sanitized_boundary_name_registers() {
    // A name from the C ABI registers only after the boundary check, which
    // the retag from ABIBoundary to Sanitized records.
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground);
    assert(mv.has_value());
    auto sanitized = ::fixy::mint_tagged<source::ABIBoundary>("aten::from_abi").retag<source::Sanitized>();
    reg(t, *mv, H(0xAB1), sanitized);
    assert(eq(t.lookup(H(0xAB1)), "aten::from_abi"));
    std::printf("  test_sanitized_boundary_name:   PASSED\n");
}

static void test_lookup_works_post_seal() {
    // Sealing stops writers, not readers.  Lookup is the background
    // thread's path and has to keep working afterwards.
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground);
    assert(mv.has_value());
    reg(t, *mv, H(0x111), S("aten::sum"));
    reg(t, *mv, H(0x222), S("aten::mean"));
    t.seal();

    const auto sv = t.mint_sealed_view();
    assert(eq(t.lookup(sv, H(0x111)), "aten::sum"));
    assert(eq(t.short_name(H(0x222)), "mean"));
    assert(t.count() == 2);
    (void)sv;
    std::printf("  test_lookup_post_seal:          PASSED\n");
}

// A table holds 512 entries, and each padding byte of an entry costs one
// store at each initialization of an automatic table (padding_bytes.h).
static void test_entry_has_no_padding_byte() {
    crucible::test::expect_no_padding_byte<^^SchemaEntry>();
    std::printf("  test_entry_has_no_padding_byte: PASSED\n");
}

int main() {
    test_empty_lookup_returns_nullptr();
    test_register_and_lookup();
    test_short_name_strips_aten_prefix();
    test_idempotent_re_register();
    test_binary_search_across_many();
    test_global_table_convenience();
    test_null_name_is_noop();
    test_default_is_mutable_and_seal_flips();
    test_sealed_table_mints_no_mutable_view();
    test_clear_resets_seal();
    test_write_after_seal_through_an_older_view_is_refused();
    test_typed_register_with_mutable_view();
    test_sanitized_boundary_name_registers();
    test_lookup_works_post_seal();
    test_entry_has_no_padding_byte();
    std::printf("test_schema_table: 15 groups, all passed\n");
    return 0;
}

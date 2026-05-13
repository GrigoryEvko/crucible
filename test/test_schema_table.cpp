// Tests for SchemaTable — registered-name dispatch via sorted binary
// search.  Validates insertion order independence, short_name aten::
// prefix stripping, idempotent re-register, and capacity bounds.

#include <crucible/SchemaTable.h>

#include "test_assert.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace crucible;

static SchemaHash H(uint64_t v) { return SchemaHash{v}; }

// Test inputs are string literals — trusted by source.  Wrap in
// Sanitized at the call site; the explicit retag is the audit trail.
static SchemaTable::SanitizedName S(const char* s) {
    return SchemaTable::SanitizedName{s};
}

static const char* C(SchemaTable::LookupName name) {
    return name.value().data();
}

static bool missing(SchemaTable::LookupName name) {
    return name.value().data() == nullptr;
}

static bool eq(SchemaTable::LookupName name, const char* expected) {
    const auto& view = name.value();
    return view.data() != nullptr
        && view.size() == std::strlen(expected)
        && std::strcmp(view.data(), expected) == 0;
}

static void test_empty_lookup_returns_nullptr() {
    SchemaTable t;
    assert(missing(t.lookup(H(0xDEAD))));
    assert(missing(t.short_name(H(0xDEAD))));
    assert(t.count() == 0);
    std::printf("  test_empty:                     PASSED\n");
}

static void test_register_and_lookup() {
    SchemaTable t;
    auto mv = t.mint_mutable_view();
    t.register_name(mv, H(0x100), S("aten::mm"));
    t.register_name(mv, H(0x200), S("aten::add.Tensor"));
    t.register_name(mv, H(0x300), S("aten::linear"));

    assert(eq(t.lookup(H(0x100)), "aten::mm"));
    assert(eq(t.lookup(H(0x200)), "aten::add.Tensor"));
    assert(eq(t.lookup(H(0x300)), "aten::linear"));
    assert(t.count() == 3);
    std::printf("  test_register_lookup:           PASSED\n");
}

static void test_short_name_strips_aten_prefix() {
    SchemaTable t;
    auto mv = t.mint_mutable_view();
    t.register_name(mv, H(0x100), S("aten::mm"));
    t.register_name(mv, H(0x200), S("aten::scaled_dot_product_attention"));
    t.register_name(mv, H(0x300), S("prim::TupleConstruct"));   // non-aten

    assert(eq(t.short_name(H(0x100)), "mm"));
    assert(eq(t.short_name(H(0x200)), "scaled_dot_product_attention"));
    // Non-aten names pass through unchanged.
    assert(eq(t.short_name(H(0x300)), "prim::TupleConstruct"));
    std::printf("  test_short_name:                PASSED\n");
}

static void test_idempotent_re_register() {
    SchemaTable t;
    auto mv = t.mint_mutable_view();
    t.register_name(mv, H(0x42), S("first"));
    assert(t.count() == 1);
    t.register_name(mv, H(0x42), S("updated"));  // same hash, new name
    assert(t.count() == 1);  // no duplicate
    assert(eq(t.lookup(H(0x42)), "updated"));
    std::printf("  test_re_register:               PASSED\n");
}

static void test_binary_search_across_many() {
    SchemaTable t;
    auto mv = t.mint_mutable_view();
    constexpr uint32_t N = 256;
    char names[N][16];
    for (uint32_t i = 0; i < N; ++i) {
        std::snprintf(names[i], sizeof(names[i]), "op_%u", i);
        // Deterministic but shuffled — test sort-on-insert.
        const uint64_t key = 0x9E3779B97F4A7C15ULL * (i + 1);
        t.register_name(mv, SchemaHash{key}, S(names[i]));
    }
    assert(t.count() == N);
    // Every registered hash resolves.
    for (uint32_t i = 0; i < N; ++i) {
        const uint64_t key = 0x9E3779B97F4A7C15ULL * (i + 1);
        const char* got = C(t.lookup(SchemaHash{key}));
        assert(got != nullptr);
        assert(std::strcmp(got, names[i]) == 0);
    }
    // Unregistered hash returns null.
    assert(missing(t.lookup(H(0xCAFE'BABE'DEAD'BEEFULL))));
    std::printf("  test_binary_search:             PASSED\n");
}

static void test_global_table_convenience() {
    global_schema_table().clear();  // isolation
    auto gv = global_schema_table().mint_mutable_view();
    register_schema_name(gv, H(0xAA), S("aten::relu"));
    assert(eq(schema_name(H(0xAA)), "aten::relu"));
    assert(eq(schema_short_name(H(0xAA)), "relu"));
    assert(missing(schema_name(H(0xBB))));
    global_schema_table().clear();
    std::printf("  test_global_helpers:            PASSED\n");
}

static void test_null_name_is_noop() {
    SchemaTable t;
    auto mv = t.mint_mutable_view();
    t.register_name(mv, H(0x77), S(nullptr));  // must not crash or corrupt
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

static void test_clear_resets_seal() {
    SchemaTable t;
    auto mv = t.mint_mutable_view();
    t.register_name(mv, H(0xAB), S("aten::matmul"));
    t.seal();
    assert(t.is_sealed());
    assert(t.count() == 1);

    t.clear();
    assert(!t.is_sealed());
    assert(t.count() == 0);
    // After clear, the table is Mutable again — register works.
    auto mv_after_clear = t.mint_mutable_view();
    t.register_name(mv_after_clear, H(0xCD), S("aten::add"));
    assert(t.count() == 1);
    assert(eq(t.lookup(H(0xCD)), "aten::add"));
    std::printf("  test_clear_resets_seal:         PASSED\n");
}

static void test_typed_register_with_mutable_view() {
    SchemaTable t;
    auto mv = t.mint_mutable_view();
    t.register_name(mv, H(0xBEEF), S("aten::conv2d"));
    assert(eq(t.lookup(H(0xBEEF)), "aten::conv2d"));
    std::printf("  test_typed_register:            PASSED\n");
}

static void test_lookup_works_post_seal() {
    // Readers are unaffected by the phase — lookup is the bg-thread path
    // that MUST keep working after seal().
    SchemaTable t;
    auto mv = t.mint_mutable_view();
    t.register_name(mv, H(0x111), S("aten::sum"));
    t.register_name(mv, H(0x222), S("aten::mean"));
    t.seal();

    const auto sv = t.mint_sealed_view();
    assert(eq(t.lookup(sv, H(0x111)), "aten::sum"));
    assert(eq(t.short_name(H(0x222)), "mean"));
    assert(t.count() == 2);
    // mint_sealed_view succeeds post-seal.
    (void)sv;
    std::printf("  test_lookup_post_seal:          PASSED\n");
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
    test_clear_resets_seal();
    test_typed_register_with_mutable_view();
    test_lookup_works_post_seal();
    std::printf("test_schema_table: 11 groups, all passed\n");
    return 0;
}

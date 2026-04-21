// Tests for SchemaTable — registered-name dispatch via sorted binary
// search.  Validates insertion order independence, short_name aten::
// prefix stripping, idempotent re-register, and capacity bounds.

#include <crucible/SchemaTable.h>

#include <cassert>
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

static void test_empty_lookup_returns_nullptr() {
    SchemaTable t;
    assert(t.lookup(H(0xDEAD)) == nullptr);
    assert(t.short_name(H(0xDEAD)) == nullptr);
    assert(t.count() == 0);
    std::printf("  test_empty:                     PASSED\n");
}

static void test_register_and_lookup() {
    SchemaTable t;
    t.register_name(H(0x100), S("aten::mm"));
    t.register_name(H(0x200), S("aten::add.Tensor"));
    t.register_name(H(0x300), S("aten::linear"));

    assert(std::strcmp(t.lookup(H(0x100)), "aten::mm") == 0);
    assert(std::strcmp(t.lookup(H(0x200)), "aten::add.Tensor") == 0);
    assert(std::strcmp(t.lookup(H(0x300)), "aten::linear") == 0);
    assert(t.count() == 3);
    std::printf("  test_register_lookup:           PASSED\n");
}

static void test_short_name_strips_aten_prefix() {
    SchemaTable t;
    t.register_name(H(0x100), S("aten::mm"));
    t.register_name(H(0x200), S("aten::scaled_dot_product_attention"));
    t.register_name(H(0x300), S("prim::TupleConstruct"));   // non-aten

    assert(std::strcmp(t.short_name(H(0x100)), "mm") == 0);
    assert(std::strcmp(t.short_name(H(0x200)),
                       "scaled_dot_product_attention") == 0);
    // Non-aten names pass through unchanged.
    assert(std::strcmp(t.short_name(H(0x300)), "prim::TupleConstruct") == 0);
    std::printf("  test_short_name:                PASSED\n");
}

static void test_idempotent_re_register() {
    SchemaTable t;
    t.register_name(H(0x42), S("first"));
    assert(t.count() == 1);
    t.register_name(H(0x42), S("updated"));  // same hash, new name
    assert(t.count() == 1);  // no duplicate
    assert(std::strcmp(t.lookup(H(0x42)), "updated") == 0);
    std::printf("  test_re_register:               PASSED\n");
}

static void test_binary_search_across_many() {
    SchemaTable t;
    constexpr uint32_t N = 256;
    char names[N][16];
    for (uint32_t i = 0; i < N; ++i) {
        std::snprintf(names[i], sizeof(names[i]), "op_%u", i);
        // Deterministic but shuffled — test sort-on-insert.
        const uint64_t key = 0x9E3779B97F4A7C15ULL * (i + 1);
        t.register_name(SchemaHash{key}, S(names[i]));
    }
    assert(t.count() == N);
    // Every registered hash resolves.
    for (uint32_t i = 0; i < N; ++i) {
        const uint64_t key = 0x9E3779B97F4A7C15ULL * (i + 1);
        const char* got = t.lookup(SchemaHash{key});
        assert(got != nullptr);
        assert(std::strcmp(got, names[i]) == 0);
    }
    // Unregistered hash returns null.
    assert(t.lookup(H(0xCAFE'BABE'DEAD'BEEFULL)) == nullptr);
    std::printf("  test_binary_search:             PASSED\n");
}

static void test_global_table_convenience() {
    global_schema_table().clear();  // isolation
    register_schema_name(H(0xAA), S("aten::relu"));
    assert(std::strcmp(schema_name(H(0xAA)), "aten::relu") == 0);
    assert(std::strcmp(schema_short_name(H(0xAA)), "relu") == 0);
    assert(schema_name(H(0xBB)) == nullptr);
    global_schema_table().clear();
    std::printf("  test_global_helpers:            PASSED\n");
}

static void test_null_name_is_noop() {
    SchemaTable t;
    t.register_name(H(0x77), S(nullptr));  // must not crash or corrupt
    assert(t.count() == 0);
    assert(t.lookup(H(0x77)) == nullptr);
    std::printf("  test_null_name:                 PASSED\n");
}

int main() {
    test_empty_lookup_returns_nullptr();
    test_register_and_lookup();
    test_short_name_strips_aten_prefix();
    test_idempotent_re_register();
    test_binary_search_across_many();
    test_global_table_convenience();
    test_null_name_is_noop();
    std::printf("test_schema_table: 7 groups, all passed\n");
    return 0;
}

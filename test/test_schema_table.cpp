#include <crucible/SchemaTable.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include "padding_bytes.h"
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;
namespace source = ::fixy::tags::source;

static SchemaHash H(uint64_t v) { return SchemaHash{v}; }

// The inputs here are string literals in the source, so crucible produced
// them: they register as internal names.
static SchemaTable::InternalName S(::fixy::TextView s) { return ::fixy::mint_tagged<source::FromInternal>(s); }

static bool missing(::fixy::Option<SchemaTable::LookupName> const& name) { return name.is_none(); }

// True when the table gives the expected characters.
static bool eq(::fixy::Option<SchemaTable::LookupName> const& name, ::fixy::TextView expected) {
    return name.match(
        [expected](SchemaTable::LookupName const& found) noexcept { return found.value().view() == expected; },
        [] noexcept { return false; });
}

// A registration into an open table always lands.
template <SchemaNameSource Tag>
static void reg(SchemaTable& t, SchemaTable::MutableView const& view, SchemaHash hash, SchemaTable::Name<Tag> name) {
    const bool was_registered = t.register_name(::foundation::effects::testing::test().alloc, view, hash, name).is_ok();
    CRUCIBLE_TEST_ASSERT(kTest, was_registered);
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
concept registers_as =
    requires(SchemaTable& t, SchemaTable::MutableView const& v, ::fixy::Tagged<::fixy::TextView, Tag> n,
             ::foundation::effects::Alloc a) { t.register_name(a, v, SchemaHash{1}, n); };
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
    CRUCIBLE_TEST_ASSERT(kTest, missing(t.lookup(H(0xDEAD))));
    CRUCIBLE_TEST_ASSERT(kTest, missing(t.short_name(H(0xDEAD))));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 0);
    ::fixy::check::pass(kTest, "  test_empty:                     PASSED\n");
}

static void test_register_and_lookup() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    reg(t, mv, H(0x100), S("aten::mm"));
    reg(t, mv, H(0x200), S("aten::add.Tensor"));
    reg(t, mv, H(0x300), S("aten::linear"));

    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(H(0x100)), "aten::mm"));
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(H(0x200)), "aten::add.Tensor"));
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(H(0x300)), "aten::linear"));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 3);
    ::fixy::check::pass(kTest, "  test_register_lookup:           PASSED\n");
}

static void test_short_name_strips_aten_prefix() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    reg(t, mv, H(0x100), S("aten::mm"));
    reg(t, mv, H(0x200), S("aten::scaled_dot_product_attention"));
    reg(t, mv, H(0x300), S("prim::TupleConstruct"));  // non-aten

    CRUCIBLE_TEST_ASSERT(kTest, eq(t.short_name(H(0x100)), "mm"));
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.short_name(H(0x200)), "scaled_dot_product_attention"));
    // Non-aten names pass through unchanged.
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.short_name(H(0x300)), "prim::TupleConstruct"));
    ::fixy::check::pass(kTest, "  test_short_name:                PASSED\n");
}

// A second registration of one hash with the same name writes nothing.  The
// table keeps its first copy, so a looked-up name that a caller holds stays
// valid, and a lookup gives the same characters at the same address.
static void test_same_name_registers_once() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    reg(t, mv, H(0x42), S("first"));
    const SchemaTable::LookupName held = t.lookup(H(0x42)).expect("the hash is registered");
    reg(t, mv, H(0x42), S("first"));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, held.value().view() == ::fixy::TextView{"first"});
    const SchemaTable::LookupName again = t.lookup(H(0x42)).expect("the hash is registered");
    CRUCIBLE_TEST_ASSERT(kTest, ::foundation::core::foreign_c_str(again.value())
                                    == ::foundation::core::foreign_c_str(held.value()));
    ::fixy::check::pass(kTest, "  test_same_name_registers_once:  PASSED\n");
}

// Registers two names for one hash.
static void register_two_names_of_one_hash() noexcept {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    reg(t, mv, H(0x42), S("first"));
    reg(t, mv, H(0x42), S("second"));
}

// A name from outside the process can be wrong, so a different name of a
// held hash is refused with collision, and the table keeps its first name.
// The same name from outside registers as an internal name does.
static void test_outside_name_collision_is_refused() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    reg(t, mv, H(0x42), S("first"));
    const SchemaTable::SanitizedName other =
        ::fixy::mint_tagged<source::External>(::fixy::TextView{"second"}).retag<source::Sanitized>();
    CRUCIBLE_TEST_ASSERT(
        kTest, is_collision(t.register_name(::foundation::effects::testing::test().alloc, mv, H(0x42), other)));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 1 && eq(t.lookup(H(0x42)), "first"));
    const SchemaTable::SanitizedName same =
        ::fixy::mint_tagged<source::External>(::fixy::TextView{"first"}).retag<source::Sanitized>();
    CRUCIBLE_TEST_ASSERT(kTest,
                         t.register_name(::foundation::effects::testing::test().alloc, mv, H(0x42), same).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 1 && eq(t.lookup(H(0x42)), "first"));
    ::fixy::check::pass(kTest, "  test_outside_name_collision:    PASSED\n");
}

// A different internal name of a held hash is a collision of two schemas, a
// defect of the program, and the registration ends the process with the two
// names, in a child process of expect_dies.
static void test_other_name_of_one_hash_is_a_collision() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(test, register_two_names_of_one_hash,
                               ::fixy::TextView{"crucible: the schema hash 66 names two schemas, first and second"});
    ::fixy::check::pass(kTest, "  test_other_name_is_a_collision: PASSED\n");
}

// The name of operation i of the search test.
static ::fixy::FixedText<16> op_name(uint32_t i) {
    ::fixy::FixedText<16> name{};
    static_cast<void>(::fixy::format(name, "op_{}", i).expect("op_ and a 32-bit number fit in 16 characters"));
    return name;
}

// The hash of filler i of a full table.  Each hash of a filler is above the
// hash that the full table refuses.
static SchemaHash filler_hash(uint32_t i) { return H(0x1000 + i); }

// Fills the table with SCHEMA_TABLE_CAP internal names.
static void fill_table(SchemaTable& t, SchemaTable::MutableView const& view) {
    for (uint32_t i = 0; i < SCHEMA_TABLE_CAP; ++i) {
        const ::fixy::FixedText<16> name = op_name(i);
        reg(t, view, filler_hash(i), S(name.view()));
    }
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == SCHEMA_TABLE_CAP);
}

// A full table refuses a new hash from outside with full, and the process
// lives.  The refused hash is below each held hash, so a refusal after the
// pass of the order would lose the entry of the largest hash.  The table keeps
// each name, and a held hash still gives its first name or a collision.
static void test_outside_name_at_full_table_is_refused() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    fill_table(t, mv);
    const SchemaTable::SanitizedName outside =
        ::fixy::mint_tagged<source::External>(::fixy::TextView{"aten::outside"}).retag<source::Sanitized>();
    CRUCIBLE_TEST_ASSERT(kTest,
                         is_full(t.register_name(::foundation::effects::testing::test().alloc, mv, H(1), outside)));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == SCHEMA_TABLE_CAP);
    CRUCIBLE_TEST_ASSERT(kTest, missing(t.lookup(H(1))));
    const ::fixy::FixedText<16> first = op_name(0);
    const ::fixy::FixedText<16> last = op_name(SCHEMA_TABLE_CAP - 1);
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(filler_hash(0)), first.view()));
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(filler_hash(SCHEMA_TABLE_CAP - 1)), last.view()));
    const SchemaTable::SanitizedName same =
        ::fixy::mint_tagged<source::External>(first.view()).retag<source::Sanitized>();
    CRUCIBLE_TEST_ASSERT(
        kTest, t.register_name(::foundation::effects::testing::test().alloc, mv, filler_hash(0), same).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, is_collision(t.register_name(::foundation::effects::testing::test().alloc, mv,
                                                             filler_hash(0), outside)));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == SCHEMA_TABLE_CAP);
    ::fixy::check::pass(kTest, "  test_outside_name_at_full_table: PASSED\n");
}

// Fills a table and registers one more internal name.
static void register_past_a_full_table() noexcept {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    fill_table(t, mv);
    reg(t, mv, H(1), S("aten::one_too_many"));
}

// A full table has no place for an internal name, a defect of the program,
// and the registration ends the process with the reason, in a child process
// of expect_dies.
static void test_internal_name_at_full_table_ends_the_process() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(
        test, register_past_a_full_table,
        ::fixy::TextView{"crucible: the schema table holds 512 names, so it has no place for the schema "
                         "aten::one_too_many"});
    ::fixy::check::pass(kTest, "  test_internal_name_at_full_table: PASSED\n");
}

static void test_binary_search_across_many() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    constexpr uint32_t N = 256;
    for (uint32_t i = 0; i < N; ++i) {
        // Deterministic but shuffled — test sort-on-insert.
        const uint64_t key = 0x9E3779B97F4A7C15ULL * (i + 1);
        const ::fixy::FixedText<16> name = op_name(i);
        reg(t, mv, SchemaHash{key}, S(name.view()));
    }
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == N);
    for (uint32_t i = 0; i < N; ++i) {
        const uint64_t key = 0x9E3779B97F4A7C15ULL * (i + 1);
        const ::fixy::FixedText<16> name = op_name(i);
        CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(SchemaHash{key}), name.view()));
    }
    CRUCIBLE_TEST_ASSERT(kTest, missing(t.lookup(H(0xCAFEBABEDEADBEEFULL))));
    ::fixy::check::pass(kTest, "  test_binary_search:             PASSED\n");
}

static void test_global_table_convenience() {
    global_schema_table().clear(test_ctx());  // this table outlives the test
    const auto gv = global_schema_table().mint_mutable_view(kVigilForeground).expect("a cleared table is open");
    const bool was_registered =
        register_schema_name(::foundation::effects::testing::test().alloc, gv, H(0xAA), S("aten::relu")).is_ok();
    CRUCIBLE_TEST_ASSERT(kTest, was_registered);
    CRUCIBLE_TEST_ASSERT(kTest, eq(schema_name(H(0xAA)), "aten::relu"));
    CRUCIBLE_TEST_ASSERT(kTest, eq(schema_short_name(H(0xAA)), "relu"));
    CRUCIBLE_TEST_ASSERT(kTest, missing(schema_name(H(0xBB))));
    global_schema_table().clear(test_ctx());
    ::fixy::check::pass(kTest, "  test_global_helpers:            PASSED\n");
}

static void test_empty_name_is_noop() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    reg(t, mv, H(0x77), S(::fixy::TextView{}));  // must not crash or corrupt
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, missing(t.lookup(H(0x77))));
    ::fixy::check::pass(kTest, "  test_empty_name:                PASSED\n");
}

// A name holds the characters before its first zero, as a C reader sees it.
static void test_name_ends_at_its_first_zero() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    ::fixy::FixedText<16> name{"aten::mm"};
    name.append('\0').expect("the name fits");
    name.append("tail").expect("the name fits");
    reg(t, mv, H(0x78), S(name.view()));
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(H(0x78)), "aten::mm"));
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.short_name(H(0x78)), "mm"));
    ::fixy::check::pass(kTest, "  test_name_ends_at_first_zero:   PASSED\n");
}

static void test_default_is_mutable_and_seal_flips() {
    SchemaTable t;
    CRUCIBLE_TEST_ASSERT(kTest, !t.is_sealed());
    t.seal();
    CRUCIBLE_TEST_ASSERT(kTest, t.is_sealed());
    // Idempotent: re-seal keeps the state sealed.
    t.seal();
    CRUCIBLE_TEST_ASSERT(kTest, t.is_sealed());
    ::fixy::check::pass(kTest, "  test_seal_flips:                PASSED\n");
}

static void test_sealed_table_mints_no_mutable_view() {
    // The check is a plain branch, so it holds in a build with NDEBUG as
    // well: a sealed table hands out no view that could write to it.
    SchemaTable t;
    t.seal();
    CRUCIBLE_TEST_ASSERT(kTest, t.mint_mutable_view(kVigilForeground).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 0);
    ::fixy::check::pass(kTest, "  test_sealed_no_mutable_view:    PASSED\n");
}

static void test_clear_resets_seal() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    reg(t, mv, H(0xAB), S("aten::matmul"));
    t.seal();
    CRUCIBLE_TEST_ASSERT(kTest, t.is_sealed());
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 1);

    t.clear(test_ctx());
    CRUCIBLE_TEST_ASSERT(kTest, !t.is_sealed());
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 0);
    // After clear, the table is Mutable again — register works.
    const auto mv_after_clear = t.mint_mutable_view(kVigilForeground).expect("a cleared table is open");
    reg(t, mv_after_clear, H(0xCD), S("aten::add"));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(H(0xCD)), "aten::add"));
    ::fixy::check::pass(kTest, "  test_clear_resets_seal:         PASSED\n");
}

static void test_write_after_seal_through_an_older_view_is_refused() {
    // The view proves only that the table was open when it was minted.  A
    // seal that lands between the mint and the write must refuse the write.
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    reg(t, mv, H(0x10), S("aten::before"));
    t.seal();
    CRUCIBLE_TEST_ASSERT(kTest, is_sealed_refusal(t.register_name(::foundation::effects::testing::test().alloc, mv,
                                                                  H(0x20), S("aten::after"))));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, missing(t.lookup(H(0x20))));
    // A write to an existing hash is refused too, and keeps the old name.
    CRUCIBLE_TEST_ASSERT(kTest, is_sealed_refusal(t.register_name(::foundation::effects::testing::test().alloc, mv,
                                                                  H(0x10), S("aten::renamed"))));
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(H(0x10)), "aten::before"));
    ::fixy::check::pass(kTest, "  test_write_after_seal_refused:  PASSED\n");
}

static void test_typed_register_with_mutable_view() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    reg(t, mv, H(0xBEEF), S("aten::conv2d"));
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(H(0xBEEF)), "aten::conv2d"));
    ::fixy::check::pass(kTest, "  test_typed_register:            PASSED\n");
}

static void test_sanitized_boundary_name_registers() {
    // A name from the C ABI registers only after the boundary check, which
    // the retag from ABIBoundary to Sanitized records.
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    auto sanitized =
        ::fixy::mint_tagged<source::ABIBoundary>(::fixy::TextView{"aten::from_abi"}).retag<source::Sanitized>();
    reg(t, mv, H(0xAB1), sanitized);
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(H(0xAB1)), "aten::from_abi"));
    ::fixy::check::pass(kTest, "  test_sanitized_boundary_name:   PASSED\n");
}

static void test_lookup_works_post_seal() {
    // Sealing stops writers, not readers.  Lookup is the background
    // thread's path and has to keep working afterwards.
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    reg(t, mv, H(0x111), S("aten::sum"));
    reg(t, mv, H(0x222), S("aten::mean"));
    t.seal();

    const auto sv = t.mint_sealed_view();
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(sv, H(0x111)), "aten::sum"));
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.short_name(H(0x222)), "mean"));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 2);
    (void)sv;
    ::fixy::check::pass(kTest, "  test_lookup_post_seal:          PASSED\n");
}

// A table holds 512 entries, and each padding byte of an entry costs one
// store at each initialization of an automatic table (padding_bytes.h).
static void test_entry_has_no_padding_byte() {
    crucible::test::expect_no_padding_byte<^^SchemaEntry>(kTest);
    ::fixy::check::pass(kTest, "  test_entry_has_no_padding_byte: PASSED\n");
}

int main() {
    test_empty_lookup_returns_nullptr();
    test_register_and_lookup();
    test_short_name_strips_aten_prefix();
    test_same_name_registers_once();
    test_other_name_of_one_hash_is_a_collision();
    test_outside_name_collision_is_refused();
    test_binary_search_across_many();
    test_outside_name_at_full_table_is_refused();
    test_internal_name_at_full_table_ends_the_process();
    test_global_table_convenience();
    test_empty_name_is_noop();
    test_name_ends_at_its_first_zero();
    test_default_is_mutable_and_seal_flips();
    test_sealed_table_mints_no_mutable_view();
    test_clear_resets_seal();
    test_write_after_seal_through_an_older_view_is_refused();
    test_typed_register_with_mutable_view();
    test_sanitized_boundary_name_registers();
    test_lookup_works_post_seal();
    test_entry_has_no_padding_byte();
    ::fixy::check::pass(kTest, "test_schema_table: 20 groups, all passed\n");
    return 0;
}

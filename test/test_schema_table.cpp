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
template <SchemaNameSource NameType>
static void reg(SchemaTable& t, SchemaTable::MutableView const& view, SchemaHash hash, NameType const& name) {
    const bool was_registered =
        t.register_name(kTest, ::foundation::effects::testing::test().alloc, view, hash, name).is_ok();
    CRUCIBLE_TEST_ASSERT(kTest, was_registered);
}

// The Sanitized name of a name from outside, through the one door that
// gives it.  Each name of the cases passes the validator.
static SchemaTable::SanitizedName sanitize_outside_name(SchemaHash hash, ::fixy::TextView name) {
    return sanitize_schema_name(hash, ::fixy::mint_tagged<source::External>(name))
        .expect("each name of the case passes the validator");
}

// The table admits exactly two types of a name, and a looked-up name claims
// neither of them: it is the table's interned copy.  A Sanitized tag that a
// retag gave, with no run of sanitize_schema_name, is no Sanitized name.
template <typename Tag>
using TaggedName = ::fixy::Tagged<::fixy::TextView, Tag>;
static_assert(SchemaNameSource<SchemaTable::SanitizedName>);
static_assert(SchemaNameSource<SchemaTable::InternalName>);
static_assert(!SchemaNameSource<TaggedName<source::Sanitized>>);
static_assert(!SchemaNameSource<TaggedName<source::External>>);
static_assert(!SchemaNameSource<TaggedName<source::ABIBoundary>>);
static_assert(!SchemaNameSource<SchemaTable::LookupName>);
static_assert(std::is_same_v<SchemaTable::LookupName::tag_type, source::Interned>);
static_assert(std::is_same_v<decltype(sanitize_schema_name(
                                 SchemaHash{1}, ::fixy::mint_tagged<source::External>(::fixy::TextView{"aten::mm"}))),
                             ::fixy::Result<SchemaTable::SanitizedName, SchemaNameError>>);

template <typename NameType, typename Ctx = ::fixy::TestRunnerCtx>
concept registers_name = requires(SchemaTable& t, Ctx const& ctx, SchemaTable::MutableView const& v, NameType const& n,
                                  ::foundation::effects::Alloc a) { t.register_name(ctx, a, v, SchemaHash{1}, n); };
template <typename Tag, typename Ctx = ::fixy::TestRunnerCtx>
concept registers_as = registers_name<TaggedName<Tag>, Ctx>;
static_assert(registers_name<SchemaTable::SanitizedName>);
static_assert(registers_as<source::FromInternal>);
static_assert(!registers_as<source::Sanitized>, "a retag to Sanitized skips sanitize_schema_name");
static_assert(!registers_as<source::External>, "a raw external name must not reach the table");
static_assert(!registers_as<source::ABIBoundary>, "a C ABI name must pass its boundary check first");

// The writer of a registration is the producer claim of a Vigil, or a
// context that owns Div.  The cold init context and the foreground context
// with no brand are neither.
static_assert(registers_as<source::FromInternal, crucible::VigilFgCtx>);
static_assert(registers_as<source::FromInternal, ::fixy::BgLoadCtx>);
static_assert(!registers_as<source::FromInternal, ::fixy::ColdInitCtx>);
static_assert(!registers_as<source::FromInternal, ::fixy::HotFgCtx>);

// The seal waits with no exit, so its context owns Div.
template <typename Ctx>
concept seals_with = requires(SchemaTable& t, Ctx const& ctx) { t.seal(ctx); };
static_assert(seals_with<::fixy::TestRunnerCtx> && seals_with<::fixy::InitLoadCtx>);
static_assert(!seals_with<::fixy::ColdInitCtx> && !seals_with<crucible::VigilFgCtx> && !seals_with<::fixy::HotFgCtx>);

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
    const SchemaTable::SanitizedName other = sanitize_outside_name(H(0x42), "second");
    CRUCIBLE_TEST_ASSERT(
        kTest, is_collision(t.register_name(kTest, ::foundation::effects::testing::test().alloc, mv, H(0x42), other)));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 1 && eq(t.lookup(H(0x42)), "first"));
    const SchemaTable::SanitizedName same = sanitize_outside_name(H(0x42), "first");
    CRUCIBLE_TEST_ASSERT(
        kTest, t.register_name(kTest, ::foundation::effects::testing::test().alloc, mv, H(0x42), same).is_ok());
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
    const SchemaTable::SanitizedName outside = sanitize_outside_name(H(1), "aten::outside");
    CRUCIBLE_TEST_ASSERT(
        kTest, is_full(t.register_name(kTest, ::foundation::effects::testing::test().alloc, mv, H(1), outside)));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == SCHEMA_TABLE_CAP);
    CRUCIBLE_TEST_ASSERT(kTest, missing(t.lookup(H(1))));
    const ::fixy::FixedText<16> first = op_name(0);
    const ::fixy::FixedText<16> last = op_name(SCHEMA_TABLE_CAP - 1);
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(filler_hash(0)), first.view()));
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(filler_hash(SCHEMA_TABLE_CAP - 1)), last.view()));
    const SchemaTable::SanitizedName same = sanitize_outside_name(filler_hash(0), first.view());
    CRUCIBLE_TEST_ASSERT(
        kTest, t.register_name(kTest, ::foundation::effects::testing::test().alloc, mv, filler_hash(0), same).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, is_collision(t.register_name(kTest, ::foundation::effects::testing::test().alloc, mv,
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
        register_schema_name(kTest, ::foundation::effects::testing::test().alloc, gv, H(0xAA), S("aten::relu")).is_ok();
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

// Registers an internal name that starts with a zero, so it has no character
// before its first zero.
static void register_name_with_a_leading_zero() noexcept {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    ::fixy::FixedText<16> name{};
    name.append('\0').expect("the name fits");
    name.append("aten::mm").expect("the name fits");
    reg(t, mv, H(0x79), S(name.view()));
}

// Registers an internal name on the hash zero, the sentinel of no schema.
static void register_name_of_the_hash_zero() noexcept {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    reg(t, mv, H(0), S("aten::mm"));
}

// An internal name that starts with a zero would be kept as an empty name,
// and a name on the hash zero would stand for no schema.  Each is a defect of
// the program, and the registration ends the process with the reason, in a
// child process of expect_dies.
static void test_refused_internal_name_ends_the_process() {
    const ::fixy::TestRunnerCtx test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(test, register_name_with_a_leading_zero,
                               ::fixy::TextView{"crucible: the internal schema name of the hash 121 has no character "
                                                "before its first zero"});
    ::fixy::check::expect_dies(test, register_name_of_the_hash_zero,
                               ::fixy::TextView{"crucible: an internal schema name has the hash zero"});
    ::fixy::check::pass(kTest, "  test_refused_internal_name:     PASSED\n");
}

static void test_default_is_mutable_and_seal_flips() {
    SchemaTable t;
    CRUCIBLE_TEST_ASSERT(kTest, !t.is_sealed());
    t.seal(kTest);
    CRUCIBLE_TEST_ASSERT(kTest, t.is_sealed());
    // Idempotent: re-seal keeps the state sealed.
    t.seal(kTest);
    CRUCIBLE_TEST_ASSERT(kTest, t.is_sealed());
    ::fixy::check::pass(kTest, "  test_seal_flips:                PASSED\n");
}

static void test_sealed_table_mints_no_mutable_view() {
    // The check is a plain branch, so it holds in a build with NDEBUG as
    // well: a sealed table hands out no view that could write to it.
    SchemaTable t;
    t.seal(kTest);
    CRUCIBLE_TEST_ASSERT(kTest, t.mint_mutable_view(kVigilForeground).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 0);
    ::fixy::check::pass(kTest, "  test_sealed_no_mutable_view:    PASSED\n");
}

static void test_clear_resets_seal() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    reg(t, mv, H(0xAB), S("aten::matmul"));
    t.seal(kTest);
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
    t.seal(kTest);
    CRUCIBLE_TEST_ASSERT(kTest, is_sealed_refusal(t.register_name(kTest, ::foundation::effects::testing::test().alloc,
                                                                  mv, H(0x20), S("aten::after"))));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, missing(t.lookup(H(0x20))));
    // A write to an existing hash is refused too, and keeps the old name.
    CRUCIBLE_TEST_ASSERT(kTest, is_sealed_refusal(t.register_name(kTest, ::foundation::effects::testing::test().alloc,
                                                                  mv, H(0x10), S("aten::renamed"))));
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
    // sanitize_schema_name runs, and which its Sanitized name records.
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    const SchemaTable::SanitizedName sanitized =
        sanitize_schema_name(H(0xAB1), ::fixy::mint_tagged<source::ABIBoundary>(::fixy::TextView{"aten::from_abi"}))
            .expect("a valid name of the C interface passes");
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
    t.seal(kTest);

    const auto sv = t.mint_sealed_view();
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(sv, H(0x111)), "aten::sum"));
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.short_name(H(0x222)), "mean"));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 2);
    (void)sv;
    ::fixy::check::pass(kTest, "  test_lookup_post_seal:          PASSED\n");
}

// The writer of a Vigil's producer claim takes the section with one
// compare-exchange.  It registers into an open table, and it gets the refusal
// sealed from a table that a seal closed after the mint of its view.
static void test_sole_writer_registers_with_one_exchange() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    CRUCIBLE_TEST_ASSERT(kTest, t.register_name(kVigilForeground, ::foundation::effects::testing::test().alloc, mv,
                                                H(0x51), S("aten::sole"))
                                    .is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, eq(t.lookup(H(0x51)), "aten::sole"));
    t.seal(kTest);
    CRUCIBLE_TEST_ASSERT(
        kTest, is_sealed_refusal(t.register_name(kVigilForeground, ::foundation::effects::testing::test().alloc, mv,
                                                 H(0x52), S("aten::late"))));
    ::fixy::check::pass(kTest, "  test_sole_writer:               PASSED\n");
}

// A second writer in the section breaks the claim of one writer.  The inner
// entry of the writer of the producer claim finds the section held, and it
// ends the process, in a child process of expect_dies.
static void enter_the_held_section_as_the_sole_writer() noexcept {
    crucible::RegistrationSeal seal;
    (void)seal.with_write_section(
        kTest, [&seal] noexcept { (void)seal.with_write_section(kVigilForeground, [] noexcept {}); });
}

static void test_second_writer_beside_the_sole_writer_ends_the_process() {
    ::fixy::check::expect_dies(kTest, enter_the_held_section_as_the_sole_writer,
                               ::fixy::TextView{"two writers entered the write section of a table"});
    ::fixy::check::pass(kTest, "  test_second_writer_ends:        PASSED\n");
}

// The validator of a name from outside takes a name of a boundary tag only:
// a name that crossed no boundary, or that passed it, does not go through it.
static_assert(SchemaNameBoundary<source::External> && SchemaNameBoundary<source::ABIBoundary>);
static_assert(!SchemaNameBoundary<source::Sanitized> && !SchemaNameBoundary<source::FromInternal>);

// The refusal of sanitize_schema_name for a name of the tag External, or no
// value when it gives the Sanitized name.
static ::fixy::Option<SchemaNameError> refusal_of(SchemaHash hash, ::fixy::TextView name) {
    return sanitize_schema_name(hash, ::fixy::mint_tagged<source::External>(name)).err();
}

// sanitize_schema_name refuses a hash of zero, an empty name, a name past
// SCHEMA_NAME_MAX characters and a name that holds a zero, and it gives each
// other name with its characters.  A name of the C interface passes the same
// checks.
static void test_sanitize_schema_name_refuses_each_bad_name() {
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of(H(0), "aten::x") == ::fixy::some(SchemaNameError::zero_hash));
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of(H(0x7), ::fixy::TextView{}) == ::fixy::some(SchemaNameError::empty));
    ::fixy::FixedText<SCHEMA_NAME_MAX + 1> longest{};
    for (std::size_t character = 0; character < SCHEMA_NAME_MAX; ++character) {
        CRUCIBLE_TEST_ASSERT(kTest, longest.append('n').is_ok());
    }
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of(H(0x7), longest.view()).is_none());
    CRUCIBLE_TEST_ASSERT(kTest, longest.append('n').is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of(H(0x7), longest.view()) == ::fixy::some(SchemaNameError::too_long));
    ::fixy::FixedText<8> leading_zero{};
    ::fixy::FixedText<8> inner_zero{};
    CRUCIBLE_TEST_ASSERT(kTest, leading_zero.append('\0').is_ok() && leading_zero.append('x').is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, inner_zero.append('y').is_ok() && inner_zero.append('\0').is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of(H(0x7), leading_zero.view()) == ::fixy::some(SchemaNameError::holds_zero));
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of(H(0x7), inner_zero.view()) == ::fixy::some(SchemaNameError::holds_zero));
    const SchemaTable::SanitizedName from_abi =
        sanitize_schema_name(H(0x7), ::fixy::mint_tagged<source::ABIBoundary>(::fixy::TextView{"aten::abi"}))
            .expect("a valid name of the C interface passes");
    CRUCIBLE_TEST_ASSERT(kTest, from_abi.value() == ::fixy::TextView{"aten::abi"});
    ::fixy::check::pass(kTest, "  test_sanitize_schema_name:      PASSED\n");
}

// One name of a walk of register_names.
struct WalkName {
    SchemaHash hash{};
    ::fixy::TextView name{};
};

// Registers the names of a walk through register_names.  Each name goes
// through sanitize_schema_name, as a name from outside does.
template <std::size_t Count>
static SchemaRegistration register_walk(SchemaTable& t, SchemaTable::MutableView const& view,
                                        ::fixy::FixedArray<WalkName, Count> const& names) {
    return t.register_names(kTest, ::foundation::effects::testing::test().alloc, view,
                            [&names](SchemaTable::AddName const& add_name) noexcept {
                                for (const WalkName& entry : names) {
                                    add_name(entry.hash, sanitize_outside_name(entry.hash, entry.name));
                                }
                            });
}

// register_names writes each name of a walk or none.  A walk that gives a
// held hash a different name, a walk that gives one hash two times, and a walk
// of more new hashes than the table has places for write nothing, also not
// the names before the refused one.  A walk of held names and new names
// writes each new name, also into the last place.  A sealed table refuses the
// walk.
static void test_register_names_writes_each_name_or_none() {
    SchemaTable t;
    const auto mv = t.mint_mutable_view(kVigilForeground).expect("an open table mints a mutable view");
    reg(t, mv, H(0x42), S("first"));
    const ::fixy::FixedArray<WalkName, 2> held_hash_renamed{WalkName{.hash = H(0x41), .name = "aten::a"},
                                                            WalkName{.hash = H(0x42), .name = "second"}};
    CRUCIBLE_TEST_ASSERT(kTest, is_collision(register_walk(t, mv, held_hash_renamed)));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 1 && missing(t.lookup(H(0x41))) && eq(t.lookup(H(0x42)), "first"));
    const ::fixy::FixedArray<WalkName, 2> hash_two_times{WalkName{.hash = H(0x43), .name = "aten::b"},
                                                         WalkName{.hash = H(0x43), .name = "aten::b"}};
    CRUCIBLE_TEST_ASSERT(kTest, is_collision(register_walk(t, mv, hash_two_times)));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 1 && missing(t.lookup(H(0x43))));
    const ::fixy::FixedArray<WalkName, 2> held_and_new{WalkName{.hash = H(0x42), .name = "first"},
                                                       WalkName{.hash = H(0x44), .name = "aten::c"}};
    CRUCIBLE_TEST_ASSERT(kTest, register_walk(t, mv, held_and_new).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == 2 && eq(t.lookup(H(0x44)), "aten::c"));

    // The table takes fillers until it has one place.
    for (uint32_t i = 0; t.count() < SCHEMA_TABLE_CAP - 1; ++i) {
        const ::fixy::FixedText<16> name = op_name(i);
        reg(t, mv, filler_hash(i), S(name.view()));
    }
    const ::fixy::FixedArray<WalkName, 2> two_new{WalkName{.hash = H(0x45), .name = "aten::d"},
                                                  WalkName{.hash = H(0x46), .name = "aten::e"}};
    CRUCIBLE_TEST_ASSERT(kTest, is_full(register_walk(t, mv, two_new)));
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == SCHEMA_TABLE_CAP - 1 && missing(t.lookup(H(0x45))));
    const ::fixy::FixedArray<WalkName, 2> held_and_last{WalkName{.hash = H(0x44), .name = "aten::c"},
                                                        WalkName{.hash = H(0x45), .name = "aten::d"}};
    CRUCIBLE_TEST_ASSERT(kTest, register_walk(t, mv, held_and_last).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, t.count() == SCHEMA_TABLE_CAP && eq(t.lookup(H(0x45)), "aten::d"));

    t.seal(kTest);
    const ::fixy::FixedArray<WalkName, 1> held_only{WalkName{.hash = H(0x42), .name = "first"}};
    CRUCIBLE_TEST_ASSERT(kTest, is_sealed_refusal(register_walk(t, mv, held_only)));
    ::fixy::check::pass(kTest, "  test_register_names_or_none:    PASSED\n");
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
    test_refused_internal_name_ends_the_process();
    test_default_is_mutable_and_seal_flips();
    test_sealed_table_mints_no_mutable_view();
    test_clear_resets_seal();
    test_write_after_seal_through_an_older_view_is_refused();
    test_typed_register_with_mutable_view();
    test_sanitized_boundary_name_registers();
    test_lookup_works_post_seal();
    test_sole_writer_registers_with_one_exchange();
    test_second_writer_beside_the_sole_writer_ends_the_process();
    test_sanitize_schema_name_refuses_each_bad_name();
    test_register_names_writes_each_name_or_none();
    test_entry_has_no_padding_byte();
    ::fixy::check::pass(kTest, "test_schema_table: 24 groups, all passed\n");
    return 0;
}

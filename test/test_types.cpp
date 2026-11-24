#include <crucible/TensorMeta.h>
#include <crucible/Types.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/contracts/Decide.h>
#include <foundation/effects/Effect.h>

#include <bit>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;

static void test_layout_and_trivial_relocatability() {
    static_assert(sizeof(OpIndex) == 4);
    static_assert(sizeof(SlotId) == 4);
    static_assert(sizeof(NodeId) == 4);
    // A count of metadata records, which a 32-bit word wraps in hours.
    static_assert(sizeof(MetaIndex) == 8);
    static_assert(MetaIndex{uint64_t{UINT32_MAX}}.is_valid() && MetaIndex{uint64_t{1} << 32}.is_valid());
    static_assert(MetaIndex::none().raw() == UINT64_MAX);
    static_assert(sizeof(SchemaHash) == 8);
    static_assert(sizeof(ShapeHash) == 8);
    static_assert(sizeof(ScopeHash) == 8);
    static_assert(sizeof(CallsiteHash) == 8);
    static_assert(sizeof(ContentHash) == 8);
    static_assert(sizeof(MerkleHash) == 8);

    static_assert(std::is_trivially_copyable_v<OpIndex>);
    static_assert(std::is_trivially_copyable_v<SchemaHash>);
    static_assert(std::is_standard_layout_v<OpIndex>);
    static_assert(std::is_standard_layout_v<SchemaHash>);
    ::fixy::check::pass(kTest, "  test_layout:                    PASSED\n");
}

static void test_default_ctor_is_sentinel() {
    OpIndex o{};
    CRUCIBLE_TEST_ASSERT(kTest, !o.is_valid());
    CRUCIBLE_TEST_ASSERT(kTest, o.raw() == UINT32_MAX);
    CRUCIBLE_TEST_ASSERT(kTest, !static_cast<bool>(o));
    CRUCIBLE_TEST_ASSERT(kTest, o == OpIndex::none());

    SchemaHash h{};
    CRUCIBLE_TEST_ASSERT(kTest, !static_cast<bool>(h));
    CRUCIBLE_TEST_ASSERT(kTest, h.raw() == 0);

    ::fixy::check::pass(kTest, "  test_default_sentinel:          PASSED\n");
}

static void test_explicit_construction() {
    OpIndex o{42};
    CRUCIBLE_TEST_ASSERT(kTest, o.raw() == 42);
    CRUCIBLE_TEST_ASSERT(kTest, o.is_valid());
    CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(o));

    static_assert(!std::is_convertible_v<uint32_t, OpIndex>);
    static_assert(!std::is_convertible_v<OpIndex, uint32_t>);
    static_assert(std::is_constructible_v<OpIndex, uint32_t>);

    static_assert(!std::is_convertible_v<OpIndex, SlotId>);
    static_assert(!std::is_convertible_v<SlotId, OpIndex>);
    static_assert(!std::is_convertible_v<SchemaHash, ShapeHash>);

    ::fixy::check::pass(kTest, "  test_explicit_construction:     PASSED\n");
}

static void test_three_way_compare() {
    OpIndex a{1}, b{2}, a2{1};
    CRUCIBLE_TEST_ASSERT(kTest, a < b);
    CRUCIBLE_TEST_ASSERT(kTest, b > a);
    CRUCIBLE_TEST_ASSERT(kTest, a == a2);
    CRUCIBLE_TEST_ASSERT(kTest, a != b);
    CRUCIBLE_TEST_ASSERT(kTest, (a <=> b) == std::strong_ordering::less);
    CRUCIBLE_TEST_ASSERT(kTest, (a <=> a2) == std::strong_ordering::equal);
    ::fixy::check::pass(kTest, "  test_compare:                   PASSED\n");
}

static void test_hash_sentinel_distinct_from_default() {
    SchemaHash def{};
    SchemaHash sent = SchemaHash::sentinel();
    CRUCIBLE_TEST_ASSERT(kTest, def != sent);
    CRUCIBLE_TEST_ASSERT(kTest, !def.is_sentinel());
    CRUCIBLE_TEST_ASSERT(kTest, sent.is_sentinel());
    CRUCIBLE_TEST_ASSERT(kTest, def.raw() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, sent.raw() == UINT64_MAX);
    ::fixy::check::pass(kTest, "  test_hash_sentinel:             PASSED\n");
}

// A usable cache key is not zero and not the sentinel.  Each of the two
// catalog predicates rejects only one of the two reserved values.
[[nodiscard]] static constexpr bool is_admissible_cache_key(ContentHash const& hash) noexcept {
    return ::foundation::decide::is_non_zero(hash) && ::foundation::decide::not_sentinel_hash(hash);
}

static void test_hash_sentinel_under_decide() {
    namespace dc = ::foundation::decide;

    // The catalog predicate reads is_sentinel() of each production strong
    // hash.  The default value is zero, and zero is not the sentinel.
    static_assert(!dc::not_sentinel_hash(SchemaHash::sentinel()));
    static_assert(dc::not_sentinel_hash(SchemaHash{}));
    static_assert(dc::not_sentinel_hash(SchemaHash{0xDEADBEEFULL}));

    static_assert(!dc::not_sentinel_hash(ContentHash::sentinel()));
    static_assert(dc::not_sentinel_hash(ContentHash{}));
    static_assert(dc::not_sentinel_hash(ContentHash{1ULL}));
    static_assert(dc::not_sentinel_hash(ContentHash{0xCAFEBABEULL}));

    static_assert(!dc::not_sentinel_hash(RowHash::sentinel()));
    static_assert(dc::not_sentinel_hash(RowHash{}));
    static_assert(dc::not_sentinel_hash(RowHash{0xFEEDFACEULL}));

    static_assert(!dc::not_sentinel_hash(RecipeHash::sentinel()));
    static_assert(dc::not_sentinel_hash(RecipeHash{0x123ULL}));
    static_assert(!dc::not_sentinel_hash(MerkleHash::sentinel()));
    static_assert(dc::not_sentinel_hash(MerkleHash{0x456ULL}));
    static_assert(!dc::not_sentinel_hash(ShapeHash::sentinel()));
    static_assert(dc::not_sentinel_hash(ShapeHash{0x789ULL}));

    // The non-zero test rejects zero, and the sentinel test rejects the sentinel.
    static_assert(!is_admissible_cache_key(ContentHash{}));
    static_assert(!is_admissible_cache_key(ContentHash::sentinel()));
    static_assert(is_admissible_cache_key(ContentHash{1ULL}));
    static_assert(is_admissible_cache_key(ContentHash{0xCAFEBABEULL}));
    static_assert(is_admissible_cache_key(ContentHash{ContentHash::sentinel().raw() - 1}));

    ::fixy::check::pass(kTest, "  test_hash_sentinel_decide:      PASSED\n");
}

static void test_noexcept_ctors_propagate() {
    // Both constructors must be noexcept, or every containing type loses
    // its own noexcept guarantee through the default-construction chain.
    static_assert(std::is_nothrow_default_constructible_v<OpIndex>);
    static_assert(std::is_nothrow_default_constructible_v<SchemaHash>);
    static_assert(noexcept(OpIndex{}));
    static_assert(noexcept(OpIndex{42u}));
    static_assert(noexcept(SchemaHash{}));
    static_assert(noexcept(SchemaHash{0xDEADBEEFULL}));
    static_assert(noexcept(OpIndex::none()));
    static_assert(noexcept(SchemaHash::sentinel()));
    ::fixy::check::pass(kTest, "  test_noexcept:                  PASSED\n");
}

// Each value of the underlying type that no enumerator names reaches the
// default arm of element_size, and the arm ends the process in each build.
// Each such value runs in a child through expect_dies of the Check family.
// Each enumerator gives its size in the test process.  enum_from, the door of
// the deserializer, is the oracle for the enumerators.  Complexity: one
// child process for each value that no enumerator names.
static void expect_element_size_ends_the_process_outside_the_enumerators(::fixy::TestRunnerCtx const& test) {
    constexpr ::fixy::TextView kUnreachable{
        "foundation: fatal: control reached a point that the code marks as unreachable"};
    for (int raw = -128; raw <= 127; ++raw) {
        ScalarType const type = static_cast<ScalarType>(static_cast<int8_t>(raw));
        if (::fixy::enum_from<ScalarType>(static_cast<int8_t>(raw)).is_some()) {
            CRUCIBLE_TEST_ASSERT(test, element_size(type).raw() <= 16);
        } else {
            ::fixy::check::expect_dies(
                test, [type] noexcept { CRUCIBLE_FATAL_INVARIANT(element_size(type).raw() <= 16); }, kUnreachable);
        }
    }
}

static void test_scalar_type_element_sizes(::fixy::TestRunnerCtx const& test) {
    // element_size returns a strong type, so a bare integer literal does not
    // compare against it. The expected sizes are written as ElementBytes.
    CRUCIBLE_TEST_ASSERT(kTest, element_size(ScalarType::Bool) == ElementBytes{1});
    CRUCIBLE_TEST_ASSERT(kTest, element_size(ScalarType::Byte) == ElementBytes{1});
    CRUCIBLE_TEST_ASSERT(kTest, element_size(ScalarType::Half) == ElementBytes{2});
    CRUCIBLE_TEST_ASSERT(kTest, element_size(ScalarType::BFloat16) == ElementBytes{2});
    CRUCIBLE_TEST_ASSERT(kTest, element_size(ScalarType::Int) == ElementBytes{4});
    CRUCIBLE_TEST_ASSERT(kTest, element_size(ScalarType::Float) == ElementBytes{4});
    CRUCIBLE_TEST_ASSERT(kTest, element_size(ScalarType::Long) == ElementBytes{8});
    CRUCIBLE_TEST_ASSERT(kTest, element_size(ScalarType::Double) == ElementBytes{8});
    CRUCIBLE_TEST_ASSERT(kTest, element_size(ScalarType::ComplexFloat) == ElementBytes{8});
    CRUCIBLE_TEST_ASSERT(kTest, element_size(ScalarType::ComplexDouble) == ElementBytes{16});
    CRUCIBLE_TEST_ASSERT(kTest, element_size(ScalarType::Float8_e4m3fn) == ElementBytes{1});
    CRUCIBLE_TEST_ASSERT(kTest, element_size(ScalarType::Undefined).is_zero());
    expect_element_size_ends_the_process_outside_the_enumerators(test);
    ::fixy::check::pass(test, "  test_element_size:              PASSED\n");
}

static void test_bit_cast_round_trip() {
    // The representation is exactly the wrapped integer, so a bit_cast to
    // that integer and back is the identity.
    OpIndex in{0x12345678u};
    auto raw = ::fixy::bit_cast<uint32_t>(in);
    CRUCIBLE_TEST_ASSERT(kTest, raw == 0x12345678u);
    auto back = ::fixy::bit_cast<OpIndex>(raw);
    CRUCIBLE_TEST_ASSERT(kTest, back == in);

    SchemaHash h_in{0xDEADBEEFCAFEBABEULL};
    auto h_raw = ::fixy::bit_cast<uint64_t>(h_in);
    CRUCIBLE_TEST_ASSERT(kTest, h_raw == 0xDEADBEEFCAFEBABEULL);
    ::fixy::check::pass(kTest, "  test_bit_cast:                  PASSED\n");
}

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_layout_and_trivial_relocatability();
    test_default_ctor_is_sentinel();
    test_explicit_construction();
    test_three_way_compare();
    test_hash_sentinel_distinct_from_default();
    test_hash_sentinel_under_decide();
    test_noexcept_ctors_propagate();
    test_scalar_type_element_sizes(test);
    test_bit_cast_round_trip();
    ::fixy::check::pass(test, "test_types: 9 groups, all passed\n");
    return 0;
}

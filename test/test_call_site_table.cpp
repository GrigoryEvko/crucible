#include <crucible/CallSiteTable.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using namespace crucible;

namespace source = ::fixy::tags::source;

static CallsiteHash H(uint64_t v) { return CallsiteHash{v}; }
static CallSiteTable::NonZeroHash NZ(uint64_t v) { return ::fixy::mint_refined<::fixy::non_zero>(H(v)); }
static ::foundation::effects::Alloc alloc_cap() { return ::foundation::effects::testing::test().alloc; }

// A stored name claims Interned.  It claims neither tag of its caller, and it
// never claims Sanitized, because the table runs no sanitizer.  No retag
// leaves Interned, so a stored name cannot go back to an ingestion API.
static_assert(std::is_same_v<CallSiteTable::InternedName::tag_type, source::Interned>);
static_assert(std::is_same_v<decltype(CallSiteTable::Entry::filename), CallSiteTable::InternedName>);
static_assert(std::is_same_v<decltype(CallSiteTable::Entry::funcname), CallSiteTable::InternedName>);
static_assert(!::fixy::RetagAllowed<source::Interned, source::Sanitized>);
static_assert(!::fixy::RetagAllowed<source::Interned, source::External>);
static_assert(!::fixy::RetagAllowed<source::Interned, source::FromInternal>);
static_assert(!std::is_constructible_v<CallSiteTable::NonZeroHash, CallsiteHash>);

// The name of an entry as a TextView.
static ::fixy::TextView name_of(CallSiteTable::InternedName const& name) { return name.value().view(); }

// The names and the line number that a test expects of an entry.
struct ExpectedEntry {
    ::fixy::TextView filename{};
    ::fixy::TextView funcname{};
    int32_t lineno = 0;
};

// True when the table holds the entry at a position.  The entries own their
// names, so the test reads them in a frame of the entries.
static bool holds_entry(CallSiteTable const& table, std::size_t position, ExpectedEntry const& expected) {
    return table.entries.with_view([position, &expected](::fixy::View<CallSiteTable::Entry const> held) noexcept {
        return held.get(position).is_some_and([&expected](auto entry) noexcept {
            return name_of(entry->filename) == expected.filename && name_of(entry->funcname) == expected.funcname
                && entry->lineno.value() == expected.lineno;
        });
    });
}

static void test_empty_table_has_nothing() {
    CallSiteTable t;
    CRUCIBLE_TEST_ASSERT(kTest, t.size() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, !t.has(H(1)));
    CRUCIBLE_TEST_ASSERT(kTest, !t.has(H(0x12345678ABCDEF00ULL)));
    ::fixy::check::pass(kTest, "  test_empty:                     PASSED\n");
}

static void test_single_insert_then_has() {
    CallSiteTable t;
    CRUCIBLE_TEST_ASSERT(kTest, t.insert(alloc_cap(), NZ(42), "foo.py", "main", 10));
    CRUCIBLE_TEST_ASSERT(kTest, t.has(H(42)));
    CRUCIBLE_TEST_ASSERT(kTest, t.size() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, holds_entry(t, 0, {.filename = "foo.py", .funcname = "main", .lineno = 10}));
    ::fixy::check::pass(kTest, "  test_single_insert:             PASSED\n");
}

static void test_duplicate_insert_is_noop() {
    CallSiteTable t;
    CRUCIBLE_TEST_ASSERT(kTest, t.insert(alloc_cap(), NZ(42), "foo.py", "main", 10));
    CRUCIBLE_TEST_ASSERT(kTest, t.insert(alloc_cap(), NZ(42), "foo.py", "main", 10));
    CRUCIBLE_TEST_ASSERT(kTest, t.insert(alloc_cap(), NZ(42), "different.py", "other", 99));
    CRUCIBLE_TEST_ASSERT(kTest, t.size() == 1);
    CRUCIBLE_TEST_ASSERT(kTest, holds_entry(t, 0, {.filename = "foo.py", .funcname = "main", .lineno = 10}));
    ::fixy::check::pass(kTest, "  test_duplicate_noop:            PASSED\n");
}

static void test_many_distinct_inserts() {
    CallSiteTable t;
    constexpr uint32_t N = 200;
    for (uint32_t i = 1; i <= N; ++i) {
        CRUCIBLE_TEST_ASSERT(kTest, t.insert(alloc_cap(), NZ(i), "file.py", "func", static_cast<int32_t>(i)));
    }
    CRUCIBLE_TEST_ASSERT(kTest, t.size() == N);
    for (uint32_t i = 1; i <= N; ++i)
        CRUCIBLE_TEST_ASSERT(kTest, t.has(H(i)));
    CRUCIBLE_TEST_ASSERT(kTest, !t.has(H(0)));  // zero is the empty-slot sentinel
    CRUCIBLE_TEST_ASSERT(kTest, !t.has(H(9999)));
    ::fixy::check::pass(kTest, "  test_many_inserts:              PASSED\n");
}

static void test_probe_does_not_confuse_hash_collision() {
    // The two hashes differ by SET_CAP, so they land in the same bucket and
    // only linear probing can find both.
    CallSiteTable t;
    const auto h_a = H(100);
    const auto h_b = H(100 + CallSiteTable::SET_CAP);
    CRUCIBLE_TEST_ASSERT(kTest, (h_a.raw() & CallSiteTable::SET_MASK) == (h_b.raw() & CallSiteTable::SET_MASK));
    CRUCIBLE_TEST_ASSERT(kTest, t.insert(alloc_cap(), ::fixy::mint_refined<::fixy::non_zero>(h_a), "a.py", "fa", 1));
    CRUCIBLE_TEST_ASSERT(kTest, t.insert(alloc_cap(), ::fixy::mint_refined<::fixy::non_zero>(h_b), "b.py", "fb", 2));
    CRUCIBLE_TEST_ASSERT(kTest, t.has(h_a));
    CRUCIBLE_TEST_ASSERT(kTest, t.has(h_b));
    CRUCIBLE_TEST_ASSERT(kTest, t.size() == 2);
    ::fixy::check::pass(kTest, "  test_collision_probe:           PASSED\n");
}

static void test_sentinel_zero_is_not_a_callsite() {
    // A raw hash of zero is the empty-slot marker.  It is not constructible as
    // NonZeroHash, so it cannot reach the table through the public mutation
    // surface.
    CallSiteTable t;
    CRUCIBLE_TEST_ASSERT(kTest, !t.has(H(0)));
    CRUCIBLE_TEST_ASSERT(kTest, t.insert(alloc_cap(), NZ(5), "five.py", "f5", 5));
    CRUCIBLE_TEST_ASSERT(kTest, t.has(H(5)));
    ::fixy::check::pass(kTest, "  test_sentinel_zero:             PASSED\n");
}

static void test_tagged_overloads_store_interned() {
    // Each tagged overload strips its tag, and the entry stores the bytes
    // under Interned.  The first write for a hash wins over the other tag.
    CallSiteTable t;
    CRUCIBLE_TEST_ASSERT(kTest,
                         t.insert(alloc_cap(), NZ(7), ::fixy::mint_tagged<source::External>(::fixy::TextView{"ext.py"}),
                                  ::fixy::mint_tagged<source::External>(::fixy::TextView{"ext_fn"}), 70));
    CRUCIBLE_TEST_ASSERT(kTest, t.insert(alloc_cap(), NZ(8),
                                         ::fixy::mint_tagged<source::FromInternal>(::fixy::TextView{"int.py"}),
                                         ::fixy::mint_tagged<source::FromInternal>(::fixy::TextView{"int_fn"}), 80));
    CRUCIBLE_TEST_ASSERT(kTest, t.insert(alloc_cap(), NZ(7),
                                         ::fixy::mint_tagged<source::FromInternal>(::fixy::TextView{"later.py"}),
                                         ::fixy::mint_tagged<source::FromInternal>(::fixy::TextView{"later_fn"}), 71));
    CRUCIBLE_TEST_ASSERT(kTest, t.size() == 2);
    CRUCIBLE_TEST_ASSERT(kTest, holds_entry(t, 0, {.filename = "ext.py", .funcname = "ext_fn", .lineno = 70}));
    CRUCIBLE_TEST_ASSERT(kTest, holds_entry(t, 1, {.filename = "int.py", .funcname = "int_fn", .lineno = 80}));
    CRUCIBLE_TEST_ASSERT(kTest, !holds_entry(t, 2, {.filename = "int.py", .funcname = "int_fn", .lineno = 80}));
    ::fixy::check::pass(kTest, "  test_tagged_overloads:          PASSED\n");
}

static void test_name_past_the_bound_is_refused() {
    // A name of MAX_NAME_CHARS characters fits, and one character more does
    // not.  A refused insert keeps neither the hash nor an entry.
    CallSiteTable t;
    ::fixy::FixedText<CallSiteTable::MAX_NAME_CHARS + 1> longest{};
    for (std::size_t index = 0; index < CallSiteTable::MAX_NAME_CHARS; ++index) {
        CRUCIBLE_TEST_ASSERT(kTest, longest.append('a').is_ok());
    }
    CRUCIBLE_TEST_ASSERT(kTest, t.insert(alloc_cap(), NZ(11), longest.view(), "f", 1));
    CRUCIBLE_TEST_ASSERT(kTest, longest.append('a').is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, !t.insert(alloc_cap(), NZ(12), longest.view(), "f", 2));
    CRUCIBLE_TEST_ASSERT(kTest, !t.insert(alloc_cap(), NZ(13), "f.py", longest.view(), 3));
    CRUCIBLE_TEST_ASSERT(kTest, t.size() == 1 && t.has(H(11)) && !t.has(H(12)) && !t.has(H(13)));
    ::fixy::check::pass(kTest, "  test_name_bound:                PASSED\n");
}

int main() {
    test_empty_table_has_nothing();
    test_single_insert_then_has();
    test_duplicate_insert_is_noop();
    test_many_distinct_inserts();
    test_probe_does_not_confuse_hash_collision();
    test_sentinel_zero_is_not_a_callsite();
    test_tagged_overloads_store_interned();
    test_name_past_the_bound_is_refused();
    ::fixy::check::pass(kTest, "test_call_site_table: 8 groups, all passed\n");
    return 0;
}

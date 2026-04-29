// Tests for CallSiteTable — open-addressing dedup set + entry store.

#include <crucible/CallSiteTable.h>

#include "test_assert.h"
#include <cstdint>
#include <cstdio>

using namespace crucible;

static CallsiteHash H(uint64_t v) { return CallsiteHash{v}; }

static void test_empty_table_has_nothing() {
    CallSiteTable t;
    assert(t.size() == 0);
    assert(!t.has(H(1)));
    assert(!t.has(H(0x1234'5678'ABCD'EF00ULL)));
    std::printf("  test_empty:                     PASSED\n");
}

static void test_single_insert_then_has() {
    CallSiteTable t;
    t.insert(H(42), "foo.py", "main", 10);
    assert(t.has(H(42)));
    assert(t.size() == 1);
    assert(t.entries[0].lineno == 10);
    assert(t.entries[0].filename == "foo.py");
    assert(t.entries[0].funcname == "main");
    std::printf("  test_single_insert:             PASSED\n");
}

static void test_duplicate_insert_is_noop() {
    CallSiteTable t;
    t.insert(H(42), "foo.py", "main", 10);
    t.insert(H(42), "foo.py", "main", 10);
    t.insert(H(42), "different.py", "other", 99);
    assert(t.size() == 1);  // second + third calls were deduped
    assert(t.entries[0].lineno == 10);
    assert(t.entries[0].filename == "foo.py");
    std::printf("  test_duplicate_noop:            PASSED\n");
}

static void test_many_distinct_inserts() {
    CallSiteTable t;
    constexpr uint32_t N = 200;
    for (uint32_t i = 1; i <= N; ++i) {
        t.insert(H(i), "file.py", "func", static_cast<int32_t>(i));
    }
    assert(t.size() == N);
    for (uint32_t i = 1; i <= N; ++i) assert(t.has(H(i)));
    assert(!t.has(H(0)));       // CallsiteHash{} is sentinel, not a real entry
    assert(!t.has(H(9999)));    // never inserted
    std::printf("  test_many_inserts:              PASSED\n");
}

static void test_probe_does_not_confuse_hash_collision() {
    // Two hashes sharing the same bucket (differ by SET_CAP multiples)
    // must both be findable via linear probing.
    CallSiteTable t;
    const auto h_a = H(100);
    const auto h_b = H(100 + CallSiteTable::SET_CAP);
    assert((h_a.raw() & CallSiteTable::SET_MASK)
        == (h_b.raw() & CallSiteTable::SET_MASK));
    t.insert(h_a, "a.py", "fa", 1);
    t.insert(h_b, "b.py", "fb", 2);
    assert(t.has(h_a));
    assert(t.has(h_b));
    assert(t.size() == 2);
    std::printf("  test_collision_probe:           PASSED\n");
}

static void test_sentinel_zero_never_inserted() {
    // The sentinel (hash raw == 0) is the "empty slot" marker.  has()
    // stops probing on it.  Inserting it is meaningless — the code
    // would store an entry but has() couldn't find it.  Ensure at
    // minimum that the table doesn't corrupt on the zero path.
    CallSiteTable t;
    t.insert(H(0), "zero.py", "z", 0);
    // Whether or not it stored, has() returns false for sentinel.
    assert(!t.has(H(0)));
    // Non-zero inserts still work.
    t.insert(H(5), "five.py", "f5", 5);
    assert(t.has(H(5)));
    std::printf("  test_sentinel_zero:             PASSED\n");
}

int main() {
    test_empty_table_has_nothing();
    test_single_insert_then_has();
    test_duplicate_insert_is_noop();
    test_many_distinct_inserts();
    test_probe_does_not_confuse_hash_collision();
    test_sentinel_zero_never_inserted();
    std::printf("test_call_site_table: 6 groups, all passed\n");
    return 0;
}

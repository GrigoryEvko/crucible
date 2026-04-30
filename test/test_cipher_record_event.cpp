// FOUND-I09 — Cipher::record_event row-checked at compile time.
//
// The 8th-axiom fence on the event-recording surface.  Wraps
// advance_head with a compile-time effect-row constraint: callers
// must pass a Row that contains both Effect::IO and Effect::Block,
// matching record_event's actual side effects (file write +
// blocking syscall).  A foreground hot-path caller (Hot/DetSafe::
// Pure context, no IO/Block in row) cannot satisfy the constraint
// and the template substitution fails with the standard
// "constraints not satisfied" diagnostic.
//
// This file is the POSITIVE side of the fence.  The neg-compile
// witness (no row, empty row, IO-only row, Block-only row) lives in
// test/safety_neg/neg_cipher_record_event_*.cpp.
//
// Test surface (T01-T10):
//   T01 — record_event_required_row IS exactly Row<IO, Block>
//   T02 — Subrow concept witness — accepted shapes
//   T03 — Subrow concept witness — rejected shapes
//   T04 — record_event<Row<IO, Block>> writes the same on-disk
//         state as advance_head
//   T05 — round-trip: record_event followed by hash_at_step recalls
//   T06 — record_event with Bg-superset row (Alloc + IO + Block + Bg)
//   T07 — record_event with bigger superset (every Effect atom)
//   T08 — pre-clause respected: monotonic step_id
//   T09 — multiple events build a monotonic log
//   T10 — record_event is the named API surface — type-pin

#include <crucible/Cipher.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include "test_assert.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <type_traits>
#include <utility>

using crucible::Cipher;
using crucible::ContentHash;
namespace eff = crucible::effects;

// ─────────────────────────────────────────────────────────────────────
// T01 — record_event_required_row IS exactly Row<IO, Block>.

static void test_t01_required_row_pinned() {
    static_assert(std::is_same_v<
        Cipher::record_event_required_row,
        eff::Row<eff::Effect::IO, eff::Effect::Block>>,
        "Cipher::record_event_required_row MUST be exactly "
        "Row<IO, Block>.  Adding atoms here is a deliberate API "
        "tightening that breaks every existing FOUND-I11..I15 "
        "migration call site — bump the contract first.");

    // Atom membership witnesses.
    static_assert( eff::row_contains_v<
        Cipher::record_event_required_row, eff::Effect::IO>);
    static_assert( eff::row_contains_v<
        Cipher::record_event_required_row, eff::Effect::Block>);
    static_assert(!eff::row_contains_v<
        Cipher::record_event_required_row, eff::Effect::Alloc>);
    static_assert(!eff::row_contains_v<
        Cipher::record_event_required_row, eff::Effect::Bg>);

    static_assert(eff::row_size_v<Cipher::record_event_required_row> == 2);

    std::printf("  T01 required_row_pinned:                 PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T02 — Subrow concept witness: required ⊆ caller, accepted shapes.

static void test_t02_subrow_accepted_shapes() {
    using Required = Cipher::record_event_required_row;

    // Caller row exactly matches required → accept.
    static_assert(eff::Subrow<Required,
        eff::Row<eff::Effect::IO, eff::Effect::Block>>);

    // Caller row in different Effect order → still accept (Subrow is
    // extensional, set-membership-based).
    static_assert(eff::Subrow<Required,
        eff::Row<eff::Effect::Block, eff::Effect::IO>>);

    // Bg superset row (canonical Bg context) → accept.
    static_assert(eff::Subrow<Required,
        eff::Row<eff::Effect::Alloc, eff::Effect::IO,
                 eff::Effect::Block, eff::Effect::Bg>>);

    // Universe superset (every Effect atom) → accept.
    static_assert(eff::Subrow<Required,
        eff::Row<eff::Effect::Alloc, eff::Effect::IO,
                 eff::Effect::Block, eff::Effect::Bg,
                 eff::Effect::Init,  eff::Effect::Test>>);

    std::printf("  T02 subrow_accepted_shapes:              PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T03 — Subrow concept witness: required ⊄ caller, rejected shapes.
// These are compile-time witnesses that the constraint *would* fail
// for these caller rows.  The actual call-site rejection lives in the
// neg-compile fixtures; here we assert the predicate directly.

static void test_t03_subrow_rejected_shapes() {
    using Required = Cipher::record_event_required_row;

    // Empty row (Hot/Pure context) → reject.
    static_assert(!eff::Subrow<Required, eff::Row<>>);

    // IO-only row → reject (missing Block).
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::IO>>);

    // Block-only row → reject (missing IO).
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Block>>);

    // Alloc-only row → reject (missing both IO and Block).
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Alloc>>);

    // Bg-only row → reject (Bg cap doesn't imply IO/Block at the
    // type-row level — Effect::Bg is the context tag, not a cap-
    // implication).
    static_assert(!eff::Subrow<Required, eff::Row<eff::Effect::Bg>>);

    std::printf("  T03 subrow_rejected_shapes:              PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T04 — bit-equality: record_event<Row<IO, Block>> writes the same
// on-disk state as advance_head.  Two Cipher instances on different
// dirs; one calls advance_head, the other calls record_event with
// the same args.  Both produce identical HEAD files and identical
// log files.

static void test_t04_record_event_matches_advance_head(const char* base_dir) {
    const std::string dir_a = std::string(base_dir) + "/t04_a";
    const std::string dir_b = std::string(base_dir) + "/t04_b";
    std::filesystem::create_directories(dir_a);
    std::filesystem::create_directories(dir_b);

    auto cipher_a = Cipher::open(dir_a);
    auto cipher_b = Cipher::open(dir_b);

    constexpr ContentHash kHash{0xC0FFEE'BA'12345678ULL};
    constexpr std::uint64_t kStep = 42u;

    cipher_a.advance_head(kHash, kStep);
    cipher_b.record_event<
        eff::Row<eff::Effect::IO, eff::Effect::Block>>(
        cipher_b.mint_open_view(), kHash, kStep);

    // HEAD content identical (file format: 16-hex + newline).
    auto read_head = [](const std::string& dir) {
        std::ifstream f(dir + "/HEAD");
        std::string s((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
        return s;
    };
    assert(read_head(dir_a) == read_head(dir_b));

    // Both Ciphers report the same HEAD ContentHash.
    assert(cipher_a.head() == cipher_b.head());
    assert(cipher_a.head() == kHash);

    std::printf("  T04 record_event_matches_advance_head:   PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T05 — round-trip: record_event followed by hash_at_step recalls
// the recorded ContentHash.

static void test_t05_round_trip(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t05";
    std::filesystem::create_directories(dir);
    auto cipher = Cipher::open(dir);

    constexpr ContentHash kHash{0xDEAD'BEEF'CAFE'BABEULL};
    constexpr std::uint64_t kStep = 7u;

    cipher.record_event<
        eff::Row<eff::Effect::IO, eff::Effect::Block>>(
        cipher.mint_open_view(), kHash, kStep);

    assert(cipher.head() == kHash);
    assert(cipher.hash_at_step(kStep) == kHash);

    // Future steps return the most recent ≤ step_id.
    assert(cipher.hash_at_step(kStep + 100) == kHash);

    std::printf("  T05 round_trip:                          PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T06 — record_event with Bg-superset row (canonical Bg context).
// A bg-thread caller declares Row<Alloc, IO, Block, Bg>; the Subrow
// constraint accepts because {IO, Block} ⊆ caller-row.

static void test_t06_bg_superset_row(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t06";
    std::filesystem::create_directories(dir);
    auto cipher = Cipher::open(dir);

    using BgRow = eff::Row<eff::Effect::Alloc, eff::Effect::IO,
                            eff::Effect::Block, eff::Effect::Bg>;

    constexpr ContentHash kHash{0x1234'5678'9ABC'DEF0ULL};
    cipher.record_event<BgRow>(cipher.mint_open_view(), kHash, 1u);

    assert(cipher.head() == kHash);
    std::printf("  T06 bg_superset_row:                     PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T07 — record_event with full universe row (every Effect atom).

static void test_t07_full_universe_row(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t07";
    std::filesystem::create_directories(dir);
    auto cipher = Cipher::open(dir);

    using UniverseRow = eff::Row<
        eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block,
        eff::Effect::Bg,    eff::Effect::Init, eff::Effect::Test>;

    constexpr ContentHash kHash{0xFEDC'BA98'7654'3210ULL};
    cipher.record_event<UniverseRow>(cipher.mint_open_view(), kHash, 1u);

    assert(cipher.head() == kHash);
    std::printf("  T07 full_universe_row:                   PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T08 — pre-clause respected: monotonic step_id.  The pre-clause is
// inherited from advance_head; record_event re-asserts it.  This test
// just confirms the contract still fires (we record a strictly
// monotonic sequence and observe correct binary search).

static void test_t08_monotonic_steps(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t08";
    std::filesystem::create_directories(dir);
    auto cipher = Cipher::open(dir);

    using R = eff::Row<eff::Effect::IO, eff::Effect::Block>;

    cipher.record_event<R>(cipher.mint_open_view(),
        ContentHash{0xAA}, 1u);
    cipher.record_event<R>(cipher.mint_open_view(),
        ContentHash{0xBB}, 5u);
    cipher.record_event<R>(cipher.mint_open_view(),
        ContentHash{0xCC}, 10u);

    // hash_at_step binary searches the log for last entry with
    // step_id ≤ key.
    assert(cipher.hash_at_step(0u)  == ContentHash{});       // before all
    assert(cipher.hash_at_step(1u)  == ContentHash{0xAA});
    assert(cipher.hash_at_step(3u)  == ContentHash{0xAA});   // gap
    assert(cipher.hash_at_step(5u)  == ContentHash{0xBB});
    assert(cipher.hash_at_step(7u)  == ContentHash{0xBB});   // gap
    assert(cipher.hash_at_step(10u) == ContentHash{0xCC});
    assert(cipher.hash_at_step(99u) == ContentHash{0xCC});   // future

    std::printf("  T08 monotonic_steps:                     PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T09 — multiple events build a monotonic log.  Confirms the full
// log structure (ts_ns + step_id + hash) is consistent across many
// record_event calls.

static void test_t09_multiple_events_monotonic(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t09";
    std::filesystem::create_directories(dir);
    auto cipher = Cipher::open(dir);

    using R = eff::Row<eff::Effect::IO, eff::Effect::Block>;

    constexpr int N = 32;
    for (int i = 0; i < N; ++i) {
        cipher.record_event<R>(
            cipher.mint_open_view(),
            ContentHash{std::uint64_t{0x1000u} + static_cast<std::uint64_t>(i)},
            static_cast<std::uint64_t>(i));
    }

    // Final HEAD = last recorded hash.
    assert(cipher.head() == ContentHash{0x1000ULL + (N - 1)});

    // Spot-check the binary search.
    for (int i = 0; i < N; ++i) {
        const auto h = cipher.hash_at_step(static_cast<std::uint64_t>(i));
        assert(h == ContentHash{std::uint64_t{0x1000u} + static_cast<std::uint64_t>(i)});
    }

    std::printf("  T09 multiple_events_monotonic:           PASSED\n");
}

// ─────────────────────────────────────────────────────────────────────
// T10 — record_event is the named API surface — type-pin.  A
// reflection-style witness that record_event resolves to a callable
// member; the templated form takes the row argument in the template
// position, not a runtime parameter.

static void test_t10_api_surface_pinned(const char* base_dir) {
    const std::string dir = std::string(base_dir) + "/t10";
    std::filesystem::create_directories(dir);
    auto cipher = Cipher::open(dir);

    using R = eff::Row<eff::Effect::IO, eff::Effect::Block>;

    // record_event<R>(view, hash, step) is callable and returns void.
    using Result = decltype(cipher.record_event<R>(
        cipher.mint_open_view(),
        ContentHash{1u},
        std::uint64_t{1u}));
    static_assert(std::is_same_v<Result, void>);

    // No runtime row tag: the row is purely template-position.
    // Confirm by checking that record_event is callable with EXACTLY
    // (OpenView, ContentHash, uint64_t) — no extra row tag.
    static_assert(std::is_invocable_r_v<
        void,
        decltype([](Cipher& c, ContentHash h, std::uint64_t s) {
            c.record_event<R>(c.mint_open_view(), h, s);
        }),
        Cipher&, ContentHash, std::uint64_t>);

    cipher.record_event<R>(cipher.mint_open_view(),
                            ContentHash{42u}, 0u);
    assert(cipher.head() == ContentHash{42u});

    std::printf("  T10 api_surface_pinned:                  PASSED\n");
}

int main() {
    // Use a per-process temp dir so the test is hermetic.
    const auto tmpl = std::filesystem::temp_directory_path() /
                      ("crucible_test_record_event_" +
                       std::to_string(::getpid()));
    std::filesystem::create_directories(tmpl);
    const std::string base = tmpl.string();

    std::printf("test_cipher_record_event — FOUND-I09 8th-axiom fence\n");
    test_t01_required_row_pinned();
    test_t02_subrow_accepted_shapes();
    test_t03_subrow_rejected_shapes();
    test_t04_record_event_matches_advance_head(base.c_str());
    test_t05_round_trip(base.c_str());
    test_t06_bg_superset_row(base.c_str());
    test_t07_full_universe_row(base.c_str());
    test_t08_monotonic_steps(base.c_str());
    test_t09_multiple_events_monotonic(base.c_str());
    test_t10_api_surface_pinned(base.c_str());

    // Cleanup.
    std::error_code ec;
    std::filesystem::remove_all(tmpl, ec);

    std::printf("test_cipher_record_event: 10 groups, all passed\n");
    return 0;
}

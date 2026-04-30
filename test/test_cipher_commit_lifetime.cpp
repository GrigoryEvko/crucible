// FOUND-G12 — Cipher OpaqueLifetime-pinned commit surface.
//
// Verifies the commit_per_{request,program,fleet} methods added to
// Cipher.  These are the production call sites for the OpaqueLifetime
// wrapper (FOUND-G09 substrate, FOUND-G11 negative-compile fixtures)
// at the persistence boundary that fences the cross-request leak bug
// class documented in OpaqueLifetime.h docblock + 25_04_2026.md §16.
//
// API surface:
//   Cipher::commit_per_request(OpaqueLifetime<PER_REQUEST or wider, *>, ...)
//       → CipherTier<Hot,  ContentHash>
//   Cipher::commit_per_program(OpaqueLifetime<PER_PROGRAM or wider, *>, ...)
//       → CipherTier<Warm, ContentHash>
//   Cipher::commit_per_fleet  (OpaqueLifetime<PER_FLEET, *>, ...)
//       → CipherTier<Cold, ContentHash>
//
// Lattice rule (LifetimeLattice.h):
//   PER_REQUEST(narrowest) ⊑ PER_PROGRAM ⊑ PER_FLEET(widest)
//   satisfies<Required> = leq(Required, Self)
//   Wider scope subsumes narrower requirement.
//
// Test surface coverage:
//   T01 — commit_per_fleet PER_FLEET → CipherTier<Cold> type-identity
//   T02 — commit_per_program PER_PROGRAM → CipherTier<Warm>
//   T03 — commit_per_request PER_REQUEST → CipherTier<Hot>
//   T04 — wider satisfies narrower (PER_FLEET passes commit_per_request)
//   T05 — wider satisfies narrower (PER_FLEET passes commit_per_program)
//   T06 — wider satisfies narrower (PER_PROGRAM passes commit_per_request)
//   T07 — same-scope match (PER_PROGRAM passes commit_per_program)
//   T08 — legacy-shape (mints view) returns same tier types
//   T09 — round-trip: commit_per_program writes, load reads back
//   T10 — null-region pass-through: commit_per_program with null returns
//         valid empty Warm{} (no UB — mirrors publish_warm semantics)
//   T11 — concept-level rejection: PER_REQUEST cannot satisfy<PER_FLEET>
//   T12 — concept-level rejection: PER_REQUEST cannot satisfy<PER_PROGRAM>
//   T13 — non-OpaqueLifetime W rejected (concept gates is_opaque_lifetime_v)
//   T14 — sizeof(OpaqueLifetime<S, T*>) preserved (zero-cost wrap)
//   T15 — content-hash round-trip equality across the lifetime overlay

#include <crucible/Arena.h>
#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/IsOpaqueLifetime.h>
#include <crucible/safety/OpaqueLifetime.h>
#include "test_assert.h"

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <type_traits>
#include <utility>

using crucible::Arena;
using crucible::Cipher;
using crucible::ContentHash;
using crucible::MetaLog;
using crucible::RegionNode;
using crucible::safety::OpaqueLifetime;
using crucible::safety::Lifetime_v;
using crucible::safety::cipher_tier::Cold;
using crucible::safety::cipher_tier::Hot;
using crucible::safety::cipher_tier::Warm;
using crucible::safety::CipherTierTag_v;

static crucible::effects::Test g_test;

// ── Helpers ─────────────────────────────────────────────────────

// Each test uses a fresh subdir under /tmp; tear down on exit so
// repeated runs don't see stale state from prior iterations.
static std::filesystem::path tmp_root_for(const char* tag) {
    static std::random_device rd;
    static std::mt19937_64 gen{rd()};
    auto p = std::filesystem::temp_directory_path()
             / ("crucible_commit_lifetime_" + std::string{tag} + "_"
                + std::to_string(gen()));
    std::filesystem::remove_all(p);
    return p;
}

// Mint a properly-constructed RegionNode via make_region (which
// placement-news + sets all NSDMI fields).  Different hash_seed
// yields different schema_hashes via the ops[] payload, which in
// turn yields different content_hashes — necessary so each test
// stores a unique object file.
static RegionNode* mint_region(Arena& arena, uint64_t hash_seed) {
    auto* ops = arena.alloc_array<crucible::TraceEntry>(g_test.alloc, 1);
    new (&ops[0]) crucible::TraceEntry{};
    ops[0].schema_hash = crucible::SchemaHash{hash_seed * 0x9E3779B97F4A7C15ULL};
    return crucible::make_region(g_test.alloc, arena, ops, 1);
}

// ── T01 — commit_per_fleet PER_FLEET → Cold ─────────────────────
static void test_commit_per_fleet_type_identity() {
    auto tmp = tmp_root_for("t01");
    Cipher c = Cipher::open(tmp.string());
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 1);

    OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*> wrapped{region};

    using Got  = decltype(c.commit_per_fleet(c.mint_open_view(),
                                             std::move(wrapped), &log));
    using Want = Cold<ContentHash>;
    static_assert(std::is_same_v<Got, Want>,
        "commit_per_fleet must return CipherTier<Cold, ContentHash>");
    static_assert(Got::tier == CipherTierTag_v::Cold);

    auto pinned = c.commit_per_fleet(c.mint_open_view(),
                                     OpaqueLifetime<Lifetime_v::PER_FLEET,
                                                    const RegionNode*>{region},
                                     &log);
    (void)std::move(pinned).consume();
    std::filesystem::remove_all(tmp);
}

// ── T02 — commit_per_program PER_PROGRAM → Warm ─────────────────
static void test_commit_per_program_type_identity() {
    auto tmp = tmp_root_for("t02");
    Cipher c = Cipher::open(tmp.string());
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 2);

    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> wrapped{region};

    using Got  = decltype(c.commit_per_program(c.mint_open_view(),
                                               std::move(wrapped), &log));
    using Want = Warm<ContentHash>;
    static_assert(std::is_same_v<Got, Want>,
        "commit_per_program must return CipherTier<Warm, ContentHash>");
    static_assert(Got::tier == CipherTierTag_v::Warm);

    auto pinned = c.commit_per_program(c.mint_open_view(),
                                       OpaqueLifetime<Lifetime_v::PER_PROGRAM,
                                                      const RegionNode*>{region},
                                       &log);
    ContentHash hash = std::move(pinned).consume();
    assert(static_cast<bool>(hash));     // Real Warm path: hash is set.
    std::filesystem::remove_all(tmp);
}

// ── T03 — commit_per_request PER_REQUEST → Hot ─────────────────
static void test_commit_per_request_type_identity() {
    auto tmp = tmp_root_for("t03");
    Cipher c = Cipher::open(tmp.string());
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 3);

    OpaqueLifetime<Lifetime_v::PER_REQUEST, const RegionNode*> wrapped{region};

    using Got  = decltype(c.commit_per_request(c.mint_open_view(),
                                               std::move(wrapped), &log));
    using Want = Hot<ContentHash>;
    static_assert(std::is_same_v<Got, Want>,
        "commit_per_request must return CipherTier<Hot, ContentHash>");
    static_assert(Got::tier == CipherTierTag_v::Hot);
    std::filesystem::remove_all(tmp);
}

// ── T04 — PER_FLEET satisfies PER_REQUEST (wider serves narrower) ─
static void test_fleet_satisfies_request() {
    auto tmp = tmp_root_for("t04");
    Cipher c = Cipher::open(tmp.string());
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 4);

    OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*> fleet_wrapped{region};

    auto pinned = c.commit_per_request(c.mint_open_view(),
                                       std::move(fleet_wrapped), &log);
    using Got = decltype(pinned);
    static_assert(std::is_same_v<Got, Hot<ContentHash>>,
        "commit_per_request always returns Hot, regardless of input scope");
    (void)std::move(pinned).consume();
    std::filesystem::remove_all(tmp);
}

// ── T05 — PER_FLEET satisfies PER_PROGRAM ───────────────────────
static void test_fleet_satisfies_program() {
    auto tmp = tmp_root_for("t05");
    Cipher c = Cipher::open(tmp.string());
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 5);

    OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*> fleet_wrapped{region};

    auto pinned = c.commit_per_program(c.mint_open_view(),
                                       std::move(fleet_wrapped), &log);
    static_assert(std::is_same_v<decltype(pinned), Warm<ContentHash>>);
    ContentHash hash = std::move(pinned).consume();
    assert(static_cast<bool>(hash));
    std::filesystem::remove_all(tmp);
}

// ── T06 — PER_PROGRAM satisfies PER_REQUEST ─────────────────────
static void test_program_satisfies_request() {
    auto tmp = tmp_root_for("t06");
    Cipher c = Cipher::open(tmp.string());
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 6);

    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> program_wrapped{region};

    auto pinned = c.commit_per_request(c.mint_open_view(),
                                       std::move(program_wrapped), &log);
    static_assert(std::is_same_v<decltype(pinned), Hot<ContentHash>>);
    (void)std::move(pinned).consume();
    std::filesystem::remove_all(tmp);
}

// ── T07 — PER_PROGRAM satisfies PER_PROGRAM (self-match) ────────
static void test_program_self_match() {
    auto tmp = tmp_root_for("t07");
    Cipher c = Cipher::open(tmp.string());
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 7);

    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> wrapped{region};
    auto pinned = c.commit_per_program(c.mint_open_view(),
                                       std::move(wrapped), &log);
    static_assert(std::is_same_v<decltype(pinned), Warm<ContentHash>>);
    ContentHash hash = std::move(pinned).consume();
    assert(static_cast<bool>(hash));
    std::filesystem::remove_all(tmp);
}

// ── T08 — Legacy-shape (no view) returns same tier types ────────
static void test_legacy_shape_type_identity() {
    auto tmp = tmp_root_for("t08");
    Cipher c = Cipher::open(tmp.string());
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 8);

    OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*> w_fleet{region};
    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> w_program{region};
    OpaqueLifetime<Lifetime_v::PER_REQUEST, const RegionNode*> w_request{region};

    static_assert(std::is_same_v<
        decltype(c.commit_per_fleet  (std::move(w_fleet),   &log)),
        Cold<ContentHash>>);
    static_assert(std::is_same_v<
        decltype(c.commit_per_program(std::move(w_program), &log)),
        Warm<ContentHash>>);
    static_assert(std::is_same_v<
        decltype(c.commit_per_request(std::move(w_request), &log)),
        Hot<ContentHash>>);
    std::filesystem::remove_all(tmp);
}

// ── T09 — Round-trip: commit_per_program writes, load reads back ─
static void test_round_trip_via_program_commit() {
    auto tmp = tmp_root_for("t09");
    Cipher c = Cipher::open(tmp.string());
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 9);
    ContentHash original_hash = region->content_hash;

    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> wrapped{region};
    auto pinned = c.commit_per_program(c.mint_open_view(),
                                       std::move(wrapped), &log);
    ContentHash written = std::move(pinned).consume();
    assert(written == original_hash);

    // The Warm path is the real implementation — load() should find
    // the file we just wrote.
    Arena loader_arena;
    auto* loaded = c.load(g_test.alloc, written, loader_arena);
    assert(loaded != nullptr);
    assert(loaded->content_hash == original_hash);
    std::filesystem::remove_all(tmp);
}

// ── T10 — Null-region pass-through ──────────────────────────────
static void test_null_region_pass_through() {
    auto tmp = tmp_root_for("t10");
    Cipher c = Cipher::open(tmp.string());
    MetaLog log;

    // PER_PROGRAM is the real Warm path; null mirrors publish_warm
    // semantics — store() returns ContentHash{} for null region.
    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> wrapped{nullptr};
    auto pinned = c.commit_per_program(c.mint_open_view(),
                                       std::move(wrapped), &log);
    ContentHash result = std::move(pinned).consume();
    // ContentHash{} is the "none" sentinel; static_cast<bool> is false.
    assert(!static_cast<bool>(result));
    std::filesystem::remove_all(tmp);
}

// ── T11 — Concept-level: PER_REQUEST does NOT satisfy PER_FLEET ─
static void test_per_request_cannot_satisfy_per_fleet() {
    using PR = OpaqueLifetime<Lifetime_v::PER_REQUEST, int>;
    using PP = OpaqueLifetime<Lifetime_v::PER_PROGRAM, int>;
    using PF = OpaqueLifetime<Lifetime_v::PER_FLEET,   int>;

    // PER_REQUEST satisfies only PER_REQUEST.
    static_assert( PR::satisfies<Lifetime_v::PER_REQUEST>);
    static_assert(!PR::satisfies<Lifetime_v::PER_PROGRAM>);
    static_assert(!PR::satisfies<Lifetime_v::PER_FLEET>);

    // PER_PROGRAM satisfies PER_PROGRAM and PER_REQUEST.
    static_assert( PP::satisfies<Lifetime_v::PER_REQUEST>);
    static_assert( PP::satisfies<Lifetime_v::PER_PROGRAM>);
    static_assert(!PP::satisfies<Lifetime_v::PER_FLEET>);

    // PER_FLEET satisfies all three (the widest).
    static_assert( PF::satisfies<Lifetime_v::PER_REQUEST>);
    static_assert( PF::satisfies<Lifetime_v::PER_PROGRAM>);
    static_assert( PF::satisfies<Lifetime_v::PER_FLEET>);
}

// ── T12 — PER_REQUEST also rejected at PER_PROGRAM fence ────────
static void test_per_request_cannot_satisfy_per_program() {
    using PR = OpaqueLifetime<Lifetime_v::PER_REQUEST, int>;
    static_assert(!PR::satisfies<Lifetime_v::PER_PROGRAM>,
        "PER_REQUEST → PER_PROGRAM is the secondary leak fence "
        "(PER_REQUEST data cannot promise program-long persistence)");
}

// ── T13 — Concept-level: non-OpaqueLifetime W rejected ──────────
//
// commit_per_* requires is_opaque_lifetime_v<W> AND satisfies<>.
// A bare ContentHash or a CipherTier<>-wrapped value is_opaque_lifetime_v
// is FALSE, so the concept gates the call.  The cross-wrapper
// rejection is structural — non-OpaqueLifetime types lack the
// W::template satisfies<...> ::value at all, so the requires-clause
// short-circuits at the is_opaque_lifetime_v check.
static void test_non_opaque_lifetime_rejected() {
    static_assert(!crucible::safety::extract::is_opaque_lifetime_v<int>);
    static_assert(!crucible::safety::extract::is_opaque_lifetime_v<ContentHash>);
    static_assert(!crucible::safety::extract::is_opaque_lifetime_v<
        Cold<ContentHash>>);
    // Positive control:
    static_assert(crucible::safety::extract::is_opaque_lifetime_v<
        OpaqueLifetime<Lifetime_v::PER_FLEET, ContentHash>>);
}

// ── T14 — Layout invariant: zero-cost wrap ─────────────────────
static void test_layout_invariant() {
    static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_FLEET,
                                        const RegionNode*>)
                  == sizeof(const RegionNode*));
    static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_PROGRAM,
                                        ContentHash>)
                  == sizeof(ContentHash));
    static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_REQUEST, int>)
                  == sizeof(int));
}

// ── T15 — Content-hash equality across the lifetime overlay ────
static void test_content_hash_equality_across_overlay() {
    auto tmp = tmp_root_for("t15");
    Cipher c = Cipher::open(tmp.string());
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 0xCAFEBABEULL);
    ContentHash bare_hash = c.store(c.mint_open_view(), region, &log);

    // Fresh Cipher in a different dir — committing the same region via
    // commit_per_program produces the same ContentHash.  Mirrors the
    // "additive overlay" claim: the lifetime axis is type-only, the
    // value-bytes (ContentHash) round-trip identically.
    auto tmp2 = tmp_root_for("t15b");
    Cipher c2 = Cipher::open(tmp2.string());

    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> wrapped{region};
    auto pinned = c2.commit_per_program(c2.mint_open_view(),
                                        std::move(wrapped), &log);
    ContentHash via_overlay = std::move(pinned).consume();

    assert(bare_hash == via_overlay);
    std::filesystem::remove_all(tmp);
    std::filesystem::remove_all(tmp2);
}

int main() {
    test_commit_per_fleet_type_identity();
    test_commit_per_program_type_identity();
    test_commit_per_request_type_identity();
    test_fleet_satisfies_request();
    test_fleet_satisfies_program();
    test_program_satisfies_request();
    test_program_self_match();
    test_legacy_shape_type_identity();
    test_round_trip_via_program_commit();
    test_null_region_pass_through();
    test_per_request_cannot_satisfy_per_fleet();
    test_per_request_cannot_satisfy_per_program();
    test_non_opaque_lifetime_rejected();
    test_layout_invariant();
    test_content_hash_equality_across_overlay();

    std::puts("ok");
    return 0;
}

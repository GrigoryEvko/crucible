// Each Cipher commit entry point demands a minimum lifetime scope on the
// region it persists.  That is what stops per-request state from being
// written into a tier that outlives the request.
//
// Scopes order from narrowest to widest:
//
//   PER_REQUEST ⊑ PER_PROGRAM ⊑ PER_FLEET
//
// A wrapper satisfies a requirement when its own scope is at least as
// wide as the requirement.  A PER_FLEET region may therefore commit
// through any of the three entry points, while a PER_REQUEST region may
// commit only through the per-request one.  Reading that subsumption
// backwards is the defect this file exists to catch, so the accepted and
// the rejected directions are both pinned.

#include <crucible/Arena.h>
#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <crucible/effects/_Capabilities.h>
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

using CipherRoot = crucible::fixy::wrap::Path<crucible::fixy::tags::source::External>;

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

static auto g_test = crucible::effects::testing::test();

// A random suffix and an up-front remove_all, so that a rerun on a
// machine that still holds a previous run's files starts clean.
static std::filesystem::path tmp_root_for(const char* tag) {
    static std::random_device rd;
    static std::mt19937_64 gen{rd()};
    auto p = std::filesystem::temp_directory_path()
           / ("crucible_commit_lifetime_" + std::string{tag} + "_" + std::to_string(gen()));
    std::filesystem::remove_all(p);
    return p;
}

// Distinct hash_seed values give distinct schema hashes, and therefore
// distinct content hashes, so each test writes its own object file
// instead of colliding with a sibling's.
static RegionNode* mint_region(Arena& arena, uint64_t hash_seed) {
    auto* ops = arena.alloc_array<crucible::TraceEntry>(g_test.alloc, 1);
    new(&ops[0]) crucible::TraceEntry{};
    ops[0].schema_hash = crucible::SchemaHash{hash_seed * 0x9E3779B97F4A7C15ULL};
    return crucible::make_region(g_test.alloc, arena, ops, 1);
}

static void test_commit_per_fleet_type_identity() {
    auto tmp = tmp_root_for("t01");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 1);

    OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*> wrapped{region};

    using Got = decltype(c.commit_per_fleet(c.mint_open_view(), std::move(wrapped), &log));
    using Want = Cold<ContentHash>;
    static_assert(std::is_same_v<Got, Want>, "commit_per_fleet must return CipherTier<Cold, ContentHash>");
    static_assert(Got::tier == CipherTierTag_v::Cold);

    auto pinned =
        c.commit_per_fleet(c.mint_open_view(), OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*>{region}, &log);
    (void)std::move(pinned).consume();
    std::filesystem::remove_all(tmp);
}

static void test_commit_per_program_type_identity() {
    auto tmp = tmp_root_for("t02");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 2);

    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> wrapped{region};

    using Got = decltype(c.commit_per_program(c.mint_open_view(), std::move(wrapped), &log));
    using Want = Warm<ContentHash>;
    static_assert(std::is_same_v<Got, Want>, "commit_per_program must return CipherTier<Warm, ContentHash>");
    static_assert(Got::tier == CipherTierTag_v::Warm);

    auto pinned = c.commit_per_program(c.mint_open_view(),
                                       OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*>{region}, &log);
    ContentHash hash = std::move(pinned).consume();
    // Warm is the tier with a real store behind it, so a hash comes back.
    assert(static_cast<bool>(hash));
    std::filesystem::remove_all(tmp);
}

static void test_commit_per_request_type_identity() {
    auto tmp = tmp_root_for("t03");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 3);

    OpaqueLifetime<Lifetime_v::PER_REQUEST, const RegionNode*> wrapped{region};

    using Got = decltype(c.commit_per_request(c.mint_open_view(), std::move(wrapped), &log));
    using Want = Hot<ContentHash>;
    static_assert(std::is_same_v<Got, Want>, "commit_per_request must return CipherTier<Hot, ContentHash>");
    static_assert(Got::tier == CipherTierTag_v::Hot);
    std::filesystem::remove_all(tmp);
}

static void test_fleet_satisfies_request() {
    auto tmp = tmp_root_for("t04");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 4);

    OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*> fleet_wrapped{region};

    auto pinned = c.commit_per_request(c.mint_open_view(), std::move(fleet_wrapped), &log);
    using Got = decltype(pinned);
    static_assert(std::is_same_v<Got, Hot<ContentHash>>,
                  "commit_per_request always returns Hot, regardless of input scope");
    (void)std::move(pinned).consume();
    std::filesystem::remove_all(tmp);
}

static void test_fleet_satisfies_program() {
    auto tmp = tmp_root_for("t05");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 5);

    OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*> fleet_wrapped{region};

    auto pinned = c.commit_per_program(c.mint_open_view(), std::move(fleet_wrapped), &log);
    static_assert(std::is_same_v<decltype(pinned), Warm<ContentHash>>);
    ContentHash hash = std::move(pinned).consume();
    assert(static_cast<bool>(hash));
    std::filesystem::remove_all(tmp);
}

static void test_program_satisfies_request() {
    auto tmp = tmp_root_for("t06");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 6);

    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> program_wrapped{region};

    auto pinned = c.commit_per_request(c.mint_open_view(), std::move(program_wrapped), &log);
    static_assert(std::is_same_v<decltype(pinned), Hot<ContentHash>>);
    (void)std::move(pinned).consume();
    std::filesystem::remove_all(tmp);
}

static void test_program_self_match() {
    auto tmp = tmp_root_for("t07");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 7);

    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> wrapped{region};
    auto pinned = c.commit_per_program(c.mint_open_view(), std::move(wrapped), &log);
    static_assert(std::is_same_v<decltype(pinned), Warm<ContentHash>>);
    ContentHash hash = std::move(pinned).consume();
    assert(static_cast<bool>(hash));
    std::filesystem::remove_all(tmp);
}

static void test_single_open_view_type_identity() {
    auto tmp = tmp_root_for("t08");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    auto view = c.mint_open_view();
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 8);

    OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*> w_fleet{region};
    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> w_program{region};
    OpaqueLifetime<Lifetime_v::PER_REQUEST, const RegionNode*> w_request{region};

    static_assert(std::is_same_v<decltype(c.commit_per_fleet(view, std::move(w_fleet), &log)), Cold<ContentHash>>);
    static_assert(std::is_same_v<decltype(c.commit_per_program(view, std::move(w_program), &log)), Warm<ContentHash>>);
    static_assert(std::is_same_v<decltype(c.commit_per_request(view, std::move(w_request), &log)), Hot<ContentHash>>);
    std::filesystem::remove_all(tmp);
}

static void test_round_trip_via_program_commit() {
    auto tmp = tmp_root_for("t09");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 9);
    ContentHash original_hash = region->content_hash;
    auto view = c.mint_open_view();

    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> wrapped{region};
    auto pinned = c.commit_per_program(view, std::move(wrapped), &log);
    ContentHash written = std::move(pinned).consume();
    assert(written == original_hash);

    Arena loader_arena;
    auto loaded_payload = c.load_content_addressed(view, g_test.alloc, written, loader_arena);
    auto* loaded = loaded_payload.get();
    assert(loaded != nullptr);
    assert(loaded->content_hash == original_hash);
    std::filesystem::remove_all(tmp);
}

// A null region is not an error at the commit boundary.  The store
// returns a default ContentHash, which is the "none" sentinel and
// converts to false.
static void test_null_region_pass_through() {
    auto tmp = tmp_root_for("t10");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    MetaLog log;

    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> wrapped{nullptr};
    auto pinned = c.commit_per_program(c.mint_open_view(), std::move(wrapped), &log);
    ContentHash result = std::move(pinned).consume();
    assert(!static_cast<bool>(result));
    std::filesystem::remove_all(tmp);
}

static void test_per_request_cannot_satisfy_per_fleet() {
    using PR = OpaqueLifetime<Lifetime_v::PER_REQUEST, int>;
    using PP = OpaqueLifetime<Lifetime_v::PER_PROGRAM, int>;
    using PF = OpaqueLifetime<Lifetime_v::PER_FLEET, int>;

    static_assert(PR::satisfies<Lifetime_v::PER_REQUEST>);
    static_assert(!PR::satisfies<Lifetime_v::PER_PROGRAM>);
    static_assert(!PR::satisfies<Lifetime_v::PER_FLEET>);

    static_assert(PP::satisfies<Lifetime_v::PER_REQUEST>);
    static_assert(PP::satisfies<Lifetime_v::PER_PROGRAM>);
    static_assert(!PP::satisfies<Lifetime_v::PER_FLEET>);

    static_assert(PF::satisfies<Lifetime_v::PER_REQUEST>);
    static_assert(PF::satisfies<Lifetime_v::PER_PROGRAM>);
    static_assert(PF::satisfies<Lifetime_v::PER_FLEET>);
}

static void test_per_request_cannot_satisfy_per_program() {
    using PR = OpaqueLifetime<Lifetime_v::PER_REQUEST, int>;
    static_assert(!PR::satisfies<Lifetime_v::PER_PROGRAM>,
                  "PER_REQUEST data cannot promise program-long persistence, so it "
                  "must not pass the per-program commit fence");
}

// A type that is not an OpaqueLifetime has no satisfies member at all,
// so the requires-clause rejects it at the wrapper check rather than at
// the scope comparison.
static void test_non_opaque_lifetime_rejected() {
    static_assert(!crucible::safety::extract::is_opaque_lifetime_v<int>);
    static_assert(!crucible::safety::extract::is_opaque_lifetime_v<ContentHash>);
    static_assert(!crucible::safety::extract::is_opaque_lifetime_v<Cold<ContentHash>>);
    static_assert(crucible::safety::extract::is_opaque_lifetime_v<OpaqueLifetime<Lifetime_v::PER_FLEET, ContentHash>>);
}

static void test_layout_invariant() {
    static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*>) == sizeof(const RegionNode*));
    static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_PROGRAM, ContentHash>) == sizeof(ContentHash));
    static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_REQUEST, int>) == sizeof(int));
}

// Relaxing a wrapper step by step down the lattice keeps it usable at
// the commit boundary.  The wrapper has to survive narrowing without
// losing the property that makes it acceptable to the fence.
static void test_relax_down_then_commit_per_request() {
    auto tmp = tmp_root_for("t16");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 16);

    OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*> fleet{region};
    auto program = std::move(fleet).relax<Lifetime_v::PER_PROGRAM>();
    auto request = std::move(program).relax<Lifetime_v::PER_REQUEST>();

    static_assert(std::is_same_v<decltype(request), OpaqueLifetime<Lifetime_v::PER_REQUEST, const RegionNode*>>);

    auto pinned = c.commit_per_request(c.mint_open_view(), std::move(request), &log);
    static_assert(std::is_same_v<decltype(pinned), Hot<ContentHash>>);
    (void)std::move(pinned).consume();
    std::filesystem::remove_all(tmp);
}

// Production sites read the scope either through the reflective trait
// or through the wrapper's own member.  Drift between the two shows up
// as wrong diagnostics and wrong hash folds rather than as a build
// failure, so the two readings are pinned equal.
static void test_reflective_trait_agreement() {
    using crucible::safety::extract::opaque_lifetime_scope_v;

    using PR = OpaqueLifetime<Lifetime_v::PER_REQUEST, const RegionNode*>;
    using PP = OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*>;
    using PF = OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*>;

    static_assert(opaque_lifetime_scope_v<PR> == PR::scope);
    static_assert(opaque_lifetime_scope_v<PP> == PP::scope);
    static_assert(opaque_lifetime_scope_v<PF> == PF::scope);

    // Wrapper detection strips cv and reference qualifiers.
    static_assert(opaque_lifetime_scope_v<PR&> == PR::scope);
    static_assert(opaque_lifetime_scope_v<PR const&> == PR::scope);
    static_assert(opaque_lifetime_scope_v<PR&&> == PR::scope);
}

// Persistence is content-addressed, so committing the same region twice
// has to produce the same hash and one file.  The lifetime overlay must
// contribute no salt of its own, or the persistence boundary stops being
// deterministic.
static void test_idempotent_commit_per_program() {
    auto tmp = tmp_root_for("t18");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 18);

    auto first = c.commit_per_program(c.mint_open_view(),
                                      OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*>{region}, &log);
    ContentHash first_hash = std::move(first).consume();

    auto second = c.commit_per_program(c.mint_open_view(),
                                       OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*>{region}, &log);
    ContentHash second_hash = std::move(second).consume();

    assert(first_hash == second_hash);
    assert(static_cast<bool>(first_hash));
    std::filesystem::remove_all(tmp);
}

// The wrapper is move-only, so copying it at a commit call site must not
// compile.  A positive test cannot witness that failure, so it witnesses
// the trait shape the by-value parameter depends on instead.
static void test_move_only_witness() {
    using PF = OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*>;

    static_assert(std::is_move_constructible_v<PF>);

    // Trivial, because the substrate is an empty-collapsed wrap over a
    // bare pointer.  A non-trivial move here would mean the wrapper had
    // stopped being zero-cost.
    static_assert(std::is_trivially_move_constructible_v<PF>);
}

// Dropping one of the three overloads would push callers around the
// lifetime fence entirely, which re-opens the leak class the fence
// exists to close.
static void test_api_completeness_matrix() {
    auto tmp = tmp_root_for("t20");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 20);

    using PR = OpaqueLifetime<Lifetime_v::PER_REQUEST, const RegionNode*>;
    using PP = OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*>;
    using PF = OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*>;
    auto view = c.mint_open_view();

    static_assert(std::is_same_v<decltype(c.commit_per_request(view, std::declval<PR&&>(), &log)), Hot<ContentHash>>);
    static_assert(std::is_same_v<decltype(c.commit_per_program(view, std::declval<PP&&>(), &log)), Warm<ContentHash>>);
    static_assert(std::is_same_v<decltype(c.commit_per_fleet(view, std::declval<PF&&>(), &log)), Cold<ContentHash>>);

    auto a = c.commit_per_request(view, PR{region}, &log);
    auto b = c.commit_per_program(view, PP{region}, &log);
    auto d = c.commit_per_fleet(view, PF{region}, &log);
    (void)std::move(a).consume();
    (void)std::move(b).consume();
    (void)std::move(d).consume();
    std::filesystem::remove_all(tmp);
}

static void test_content_hash_equality_across_overlay() {
    auto tmp = tmp_root_for("t15");
    Cipher c = Cipher::open(CipherRoot{tmp.string()});
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 0xCAFEBABEULL);
    auto view = c.mint_open_view();
    ContentHash bare_hash = c.store(view, Cipher::content_addressed(region), &log);

    // A second Cipher in a different directory, to show the hash does
    // not depend on the store it was written to.  The lifetime axis
    // lives only in the type, so the bytes are identical either way.
    auto tmp2 = tmp_root_for("t15b");
    Cipher c2 = Cipher::open(CipherRoot{tmp2.string()});

    OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*> wrapped{region};
    auto pinned = c2.commit_per_program(c2.mint_open_view(), std::move(wrapped), &log);
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
    test_single_open_view_type_identity();
    test_round_trip_via_program_commit();
    test_null_region_pass_through();
    test_per_request_cannot_satisfy_per_fleet();
    test_per_request_cannot_satisfy_per_program();
    test_non_opaque_lifetime_rejected();
    test_layout_invariant();
    test_content_hash_equality_across_overlay();

    test_relax_down_then_commit_per_request();
    test_reflective_trait_agreement();
    test_idempotent_commit_per_program();
    test_move_only_witness();
    test_api_completeness_matrix();

    std::puts("ok");
    return 0;
}

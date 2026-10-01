// Each Cipher commit entry point demands a minimum lifetime scope on the
// region it persists.  That is what stops per-request state from being
// written into a tier that outlives the request.
//
// Scopes order from narrowest to widest:
//
//   PER_REQUEST ⊑ PER_PROGRAM ⊑ PER_FLEET
//
// A band satisfies a requirement when its own scope is at least as wide
// as the requirement.  A PER_FLEET region can commit through any of the
// three entry points.  A PER_REQUEST region can commit only through the
// per-request one.  Reading that subsumption backwards
// is the defect this file exists to catch, so the accepted and the
// rejected directions are both pinned.

#include <crucible/Arena.h>
#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <fixy/Bands.h>
#include <fixy/Ctx.h>
#include "test_assert.h"

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <type_traits>
#include <utility>

// Opening the store and its open view need a context whose row admits IO
// and Block.
[[nodiscard]] inline ::fixy::TestRunnerCtx store_ctx() {
    return ::fixy::TestRunnerCtx{::foundation::effects::testing::test()};
}

using crucible::Arena;
using crucible::Cipher;
using crucible::ContentHash;
using crucible::MetaLog;
using crucible::RegionNode;
using ::fixy::CipherTierTag_v;
using ::fixy::Lifetime_v;
using ::fixy::OpaqueLifetime;
using ::fixy::cipher_tier::Cold;
using ::fixy::cipher_tier::Hot;
using ::fixy::cipher_tier::Warm;

static auto g_test = ::foundation::effects::testing::test();

[[nodiscard]] static Cipher open_cipher(const std::filesystem::path& dir) {
    return Cipher::open(store_ctx(), ::fixy::mint_tagged<::fixy::tags::source::External>(dir));
}

// The band's door is the one site that asserts a lifetime.
template <Lifetime_v Scope>
[[nodiscard]] static OpaqueLifetime<Scope, const RegionNode*> pinned(const RegionNode* region) {
    return ::fixy::mint_band<OpaqueLifetime<Scope, const RegionNode*>>(region);
}

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
    Cipher c = open_cipher(tmp);
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 1);

    auto wrapped = pinned<Lifetime_v::PER_FLEET>(region);

    using Got = decltype(c.commit_per_fleet(c.mint_open_view(store_ctx()), std::move(wrapped), &log));
    using Want = Cold<ContentHash>;
    static_assert(std::is_same_v<Got, Want>, "commit_per_fleet must return CipherTier<Cold, ContentHash>");
    static_assert(::fixy::band_tier_v<Got> == CipherTierTag_v::Cold);

    auto result = c.commit_per_fleet(c.mint_open_view(store_ctx()), pinned<Lifetime_v::PER_FLEET>(region), &log);
    (void)std::move(result).consume();
    std::filesystem::remove_all(tmp);
}

static void test_commit_per_program_type_identity() {
    auto tmp = tmp_root_for("t02");
    Cipher c = open_cipher(tmp);
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 2);

    auto wrapped = pinned<Lifetime_v::PER_PROGRAM>(region);

    using Got = decltype(c.commit_per_program(c.mint_open_view(store_ctx()), std::move(wrapped), &log));
    using Want = Warm<ContentHash>;
    static_assert(std::is_same_v<Got, Want>, "commit_per_program must return CipherTier<Warm, ContentHash>");
    static_assert(::fixy::band_tier_v<Got> == CipherTierTag_v::Warm);

    auto result = c.commit_per_program(c.mint_open_view(store_ctx()), pinned<Lifetime_v::PER_PROGRAM>(region), &log);
    ContentHash hash = std::move(result).consume();
    // Warm is the tier with a real store behind it, so a hash comes back.
    assert(static_cast<bool>(hash));
    std::filesystem::remove_all(tmp);
}

static void test_commit_per_request_type_identity() {
    auto tmp = tmp_root_for("t03");
    Cipher c = open_cipher(tmp);
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 3);

    auto wrapped = pinned<Lifetime_v::PER_REQUEST>(region);

    using Got = decltype(c.commit_per_request(c.mint_open_view(store_ctx()), std::move(wrapped), &log));
    using Want = Hot<ContentHash>;
    static_assert(std::is_same_v<Got, Want>, "commit_per_request must return CipherTier<Hot, ContentHash>");
    static_assert(::fixy::band_tier_v<Got> == CipherTierTag_v::Hot);
    std::filesystem::remove_all(tmp);
}

static void test_fleet_satisfies_request() {
    auto tmp = tmp_root_for("t04");
    Cipher c = open_cipher(tmp);
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 4);

    auto fleet_wrapped = pinned<Lifetime_v::PER_FLEET>(region);

    auto result = c.commit_per_request(c.mint_open_view(store_ctx()), std::move(fleet_wrapped), &log);
    using Got = decltype(result);
    static_assert(std::is_same_v<Got, Hot<ContentHash>>,
                  "commit_per_request always returns Hot, regardless of input scope");
    (void)std::move(result).consume();
    std::filesystem::remove_all(tmp);
}

static void test_fleet_satisfies_program() {
    auto tmp = tmp_root_for("t05");
    Cipher c = open_cipher(tmp);
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 5);

    auto fleet_wrapped = pinned<Lifetime_v::PER_FLEET>(region);

    auto result = c.commit_per_program(c.mint_open_view(store_ctx()), std::move(fleet_wrapped), &log);
    static_assert(std::is_same_v<decltype(result), Warm<ContentHash>>);
    ContentHash hash = std::move(result).consume();
    assert(static_cast<bool>(hash));
    std::filesystem::remove_all(tmp);
}

static void test_program_satisfies_request() {
    auto tmp = tmp_root_for("t06");
    Cipher c = open_cipher(tmp);
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 6);

    auto program_wrapped = pinned<Lifetime_v::PER_PROGRAM>(region);

    auto result = c.commit_per_request(c.mint_open_view(store_ctx()), std::move(program_wrapped), &log);
    static_assert(std::is_same_v<decltype(result), Hot<ContentHash>>);
    (void)std::move(result).consume();
    std::filesystem::remove_all(tmp);
}

static void test_program_self_match() {
    auto tmp = tmp_root_for("t07");
    Cipher c = open_cipher(tmp);
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 7);

    auto wrapped = pinned<Lifetime_v::PER_PROGRAM>(region);
    auto result = c.commit_per_program(c.mint_open_view(store_ctx()), std::move(wrapped), &log);
    static_assert(std::is_same_v<decltype(result), Warm<ContentHash>>);
    ContentHash hash = std::move(result).consume();
    assert(static_cast<bool>(hash));
    std::filesystem::remove_all(tmp);
}

static void test_single_open_view_type_identity() {
    auto tmp = tmp_root_for("t08");
    Cipher c = open_cipher(tmp);
    auto view = c.mint_open_view(store_ctx());
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 8);

    auto w_fleet = pinned<Lifetime_v::PER_FLEET>(region);
    auto w_program = pinned<Lifetime_v::PER_PROGRAM>(region);
    auto w_request = pinned<Lifetime_v::PER_REQUEST>(region);

    static_assert(std::is_same_v<decltype(c.commit_per_fleet(view, std::move(w_fleet), &log)), Cold<ContentHash>>);
    static_assert(std::is_same_v<decltype(c.commit_per_program(view, std::move(w_program), &log)), Warm<ContentHash>>);
    static_assert(std::is_same_v<decltype(c.commit_per_request(view, std::move(w_request), &log)), Hot<ContentHash>>);
    std::filesystem::remove_all(tmp);
}

static void test_round_trip_via_program_commit() {
    auto tmp = tmp_root_for("t09");
    Cipher c = open_cipher(tmp);
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 9);
    ContentHash original_hash = region->content_hash;
    auto view = c.mint_open_view(store_ctx());

    auto wrapped = pinned<Lifetime_v::PER_PROGRAM>(region);
    auto result = c.commit_per_program(view, std::move(wrapped), &log);
    ContentHash written = std::move(result).consume();
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
    Cipher c = open_cipher(tmp);
    MetaLog log;

    auto wrapped = pinned<Lifetime_v::PER_PROGRAM>(nullptr);
    auto result = c.commit_per_program(c.mint_open_view(store_ctx()), std::move(wrapped), &log);
    ContentHash hash = std::move(result).consume();
    assert(!static_cast<bool>(hash));
    std::filesystem::remove_all(tmp);
}

static void test_per_request_cannot_satisfy_per_fleet() {
    using PR = OpaqueLifetime<Lifetime_v::PER_REQUEST, int>;
    using PP = OpaqueLifetime<Lifetime_v::PER_PROGRAM, int>;
    using PF = OpaqueLifetime<Lifetime_v::PER_FLEET, int>;

    static_assert(::fixy::satisfies_v<PR, Lifetime_v::PER_REQUEST>);
    static_assert(!::fixy::satisfies_v<PR, Lifetime_v::PER_PROGRAM>);
    static_assert(!::fixy::satisfies_v<PR, Lifetime_v::PER_FLEET>);

    static_assert(::fixy::satisfies_v<PP, Lifetime_v::PER_REQUEST>);
    static_assert(::fixy::satisfies_v<PP, Lifetime_v::PER_PROGRAM>);
    static_assert(!::fixy::satisfies_v<PP, Lifetime_v::PER_FLEET>);

    static_assert(::fixy::satisfies_v<PF, Lifetime_v::PER_REQUEST>);
    static_assert(::fixy::satisfies_v<PF, Lifetime_v::PER_PROGRAM>);
    static_assert(::fixy::satisfies_v<PF, Lifetime_v::PER_FLEET>);
}

static void test_per_request_cannot_satisfy_per_program() {
    using PR = OpaqueLifetime<Lifetime_v::PER_REQUEST, const RegionNode*>;
    static_assert(!crucible::cipher::LifetimePinnedRegion<PR, Lifetime_v::PER_PROGRAM>,
                  "PER_REQUEST data cannot promise program-long persistence, so it "
                  "must not pass the per-program commit fence");
}

// A type that is not a lifetime band is refused at the band check
// rather than at the scope comparison.
static void test_non_opaque_lifetime_rejected() {
    static_assert(!::fixy::is_band_of_v<::fixy::LifetimeLattice, int>);
    static_assert(!::fixy::is_band_of_v<::fixy::LifetimeLattice, ContentHash>);
    static_assert(!::fixy::is_band_of_v<::fixy::LifetimeLattice, Cold<ContentHash>>);
    static_assert(::fixy::is_band_of_v<::fixy::LifetimeLattice, OpaqueLifetime<Lifetime_v::PER_FLEET, ContentHash>>);
    static_assert(!crucible::cipher::LifetimePinnedRegion<const RegionNode*, Lifetime_v::PER_REQUEST>);
    static_assert(!crucible::cipher::LifetimePinnedRegion<Cold<const RegionNode*>, Lifetime_v::PER_REQUEST>);
    static_assert(
        !crucible::cipher::LifetimePinnedRegion<OpaqueLifetime<Lifetime_v::PER_FLEET, int>, Lifetime_v::PER_REQUEST>,
        "a band over a value that is no region pointer is refused too");
}

static void test_layout_invariant() {
    static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*>) == sizeof(const RegionNode*));
    static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_PROGRAM, ContentHash>) == sizeof(ContentHash));
    static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_REQUEST, int>) == sizeof(int));
}

// Relaxing a band step by step down the lattice keeps it usable at the
// commit boundary.  The band has to survive narrowing without losing the
// property that makes it acceptable to the fence.
static void test_relax_down_then_commit_per_request() {
    auto tmp = tmp_root_for("t16");
    Cipher c = open_cipher(tmp);
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 16);

    auto fleet = pinned<Lifetime_v::PER_FLEET>(region);
    auto program = ::fixy::relax<Lifetime_v::PER_PROGRAM>(std::move(fleet));
    auto request = ::fixy::relax<Lifetime_v::PER_REQUEST>(std::move(program));

    static_assert(std::is_same_v<decltype(request), OpaqueLifetime<Lifetime_v::PER_REQUEST, const RegionNode*>>);

    auto result = c.commit_per_request(c.mint_open_view(store_ctx()), std::move(request), &log);
    static_assert(std::is_same_v<decltype(result), Hot<ContentHash>>);
    (void)std::move(result).consume();
    std::filesystem::remove_all(tmp);
}

// The scope is read off the type, whether the site names the band or a
// reference to it.  Drift between the readings shows up as wrong
// diagnostics and wrong hash folds rather than as a build failure, so the
// readings are pinned equal.
static void test_reflective_trait_agreement() {
    using PR = OpaqueLifetime<Lifetime_v::PER_REQUEST, const RegionNode*>;
    using PP = OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*>;
    using PF = OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*>;

    static_assert(::fixy::band_tier_v<PR> == Lifetime_v::PER_REQUEST);
    static_assert(::fixy::band_tier_v<PP> == Lifetime_v::PER_PROGRAM);
    static_assert(::fixy::band_tier_v<PF> == Lifetime_v::PER_FLEET);

    // Band detection strips cv and reference qualifiers.
    static_assert(::fixy::band_tier_v<PR&> == Lifetime_v::PER_REQUEST);
    static_assert(::fixy::band_tier_v<PR const&> == Lifetime_v::PER_REQUEST);
    static_assert(::fixy::band_tier_v<PR&&> == Lifetime_v::PER_REQUEST);
}

// Persistence is content-addressed, so committing the same region twice
// has to produce the same hash and one file.  The lifetime overlay must
// contribute no salt of its own, or the persistence boundary stops being
// deterministic.
static void test_idempotent_commit_per_program() {
    auto tmp = tmp_root_for("t18");
    Cipher c = open_cipher(tmp);
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 18);

    auto first = c.commit_per_program(c.mint_open_view(store_ctx()), pinned<Lifetime_v::PER_PROGRAM>(region), &log);
    ContentHash first_hash = std::move(first).consume();

    auto second = c.commit_per_program(c.mint_open_view(store_ctx()), pinned<Lifetime_v::PER_PROGRAM>(region), &log);
    ContentHash second_hash = std::move(second).consume();

    assert(first_hash == second_hash);
    assert(static_cast<bool>(first_hash));
    std::filesystem::remove_all(tmp);
}

// A commit consumes the band by value, so the band moves into the call.
// A positive test cannot witness a refused copy, so it witnesses the
// trait shape the by-value parameter depends on instead.
static void test_move_only_witness() {
    using PF = OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*>;

    static_assert(std::is_move_constructible_v<PF>);

    // Trivial, because the substrate is an empty-collapsed wrap over a
    // bare pointer.  A non-trivial move here would mean the band had
    // stopped being zero-cost.
    static_assert(std::is_trivially_move_constructible_v<PF>);
}

// Dropping one of the three overloads would push callers around the
// lifetime fence entirely, which re-opens the leak class the fence
// exists to close.
static void test_api_completeness_matrix() {
    auto tmp = tmp_root_for("t20");
    Cipher c = open_cipher(tmp);
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 20);

    using PR = OpaqueLifetime<Lifetime_v::PER_REQUEST, const RegionNode*>;
    using PP = OpaqueLifetime<Lifetime_v::PER_PROGRAM, const RegionNode*>;
    using PF = OpaqueLifetime<Lifetime_v::PER_FLEET, const RegionNode*>;
    auto view = c.mint_open_view(store_ctx());

    static_assert(std::is_same_v<decltype(c.commit_per_request(view, std::declval<PR&&>(), &log)), Hot<ContentHash>>);
    static_assert(std::is_same_v<decltype(c.commit_per_program(view, std::declval<PP&&>(), &log)), Warm<ContentHash>>);
    static_assert(std::is_same_v<decltype(c.commit_per_fleet(view, std::declval<PF&&>(), &log)), Cold<ContentHash>>);

    auto a = c.commit_per_request(view, pinned<Lifetime_v::PER_REQUEST>(region), &log);
    auto b = c.commit_per_program(view, pinned<Lifetime_v::PER_PROGRAM>(region), &log);
    auto d = c.commit_per_fleet(view, pinned<Lifetime_v::PER_FLEET>(region), &log);
    (void)std::move(a).consume();
    (void)std::move(b).consume();
    (void)std::move(d).consume();
    std::filesystem::remove_all(tmp);
}

static void test_content_hash_equality_across_overlay() {
    auto tmp = tmp_root_for("t15");
    Cipher c = open_cipher(tmp);
    Arena arena;
    MetaLog log;
    auto* region = mint_region(arena, 0xCAFEBABEULL);
    auto view = c.mint_open_view(store_ctx());
    ContentHash bare_hash = c.store(view, Cipher::content_addressed(region), &log);

    // A second Cipher in a different directory, to show the hash does
    // not depend on the store it was written to.  The lifetime axis
    // lives only in the type, so the bytes are identical either way.
    auto tmp2 = tmp_root_for("t15b");
    Cipher c2 = open_cipher(tmp2);

    auto wrapped = pinned<Lifetime_v::PER_PROGRAM>(region);
    auto result = c2.commit_per_program(c2.mint_open_view(store_ctx()), std::move(wrapped), &log);
    ContentHash via_overlay = std::move(result).consume();

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

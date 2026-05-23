// FIXY-V-203 sentinel TU: fixy/spawn/JoinPolicy.h — the phantom-tag
// tree for the spawn-MECHANISM axis (orthogonal to V-078/V-079
// strictness lattice).  Forces every header-embedded static_assert
// through project warnings-as-errors per
// feedback_header_only_static_assert_blind_spot.
//
// V-203 ships NO new mint factory; the surface is six phantom tag
// structs + one diagnostic enum + one identity-allowlist concept.
// The two HS14 negative-compile fixtures live at
//   test/fixy_neg/neg_fixy_v_203_join_concept_rejects_non_tag.cpp
//   test/fixy_neg/neg_fixy_v_203_join_imposter_struct.cpp
// — one per distinct mismatch class (no-mechanism-member vs
// well-formed-mechanism-member-but-wrong-identity).

#include <crucible/fixy/spawn/JoinPolicy.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace join = ::crucible::fixy::spawn::join;

// ── Surface admission — every tag is recognized by the concept ─────
static_assert(join::IsJoinMechanismTag<join::AutoJoin>);
static_assert(join::IsJoinMechanismTag<join::ManualJoin>);
static_assert(join::IsJoinMechanismTag<join::Detached>);
static_assert(join::IsJoinMechanismTag<join::Cloned>);
static_assert(join::IsJoinMechanismTag<join::Forked>);
static_assert(join::IsJoinMechanismTag<join::PosixSpawn>);

// ── Surface rejection — non-tag types fail the concept ─────────────
static_assert(!join::IsJoinMechanismTag<int>);
static_assert(!join::IsJoinMechanismTag<unsigned char>);
static_assert(!join::IsJoinMechanismTag<void*>);
static_assert(!join::IsJoinMechanismTag<join::JoinMechanism>);

// ── Reverse-lookup metafunction recovers the enum value ────────────
static_assert(join::mechanism_of_v<join::AutoJoin>
              == join::JoinMechanism::AutoJoin);
static_assert(join::mechanism_of_v<join::ManualJoin>
              == join::JoinMechanism::ManualJoin);
static_assert(join::mechanism_of_v<join::Detached>
              == join::JoinMechanism::Detached);
static_assert(join::mechanism_of_v<join::Cloned>
              == join::JoinMechanism::Cloned);
static_assert(join::mechanism_of_v<join::Forked>
              == join::JoinMechanism::Forked);
static_assert(join::mechanism_of_v<join::PosixSpawn>
              == join::JoinMechanism::PosixSpawn);

// ── EBO size — each tag is sizeof 1 byte, empty-class-collapsible ──
static_assert(sizeof(join::AutoJoin)   == 1);
static_assert(sizeof(join::ManualJoin) == 1);
static_assert(sizeof(join::Detached)   == 1);
static_assert(sizeof(join::Cloned)     == 1);
static_assert(sizeof(join::Forked)     == 1);
static_assert(sizeof(join::PosixSpawn) == 1);

// ── Tags are pairwise distinct — no implicit substitution ──────────
static_assert(!std::is_same_v<join::AutoJoin,   join::ManualJoin>);
static_assert(!std::is_same_v<join::AutoJoin,   join::Detached>);
static_assert(!std::is_same_v<join::AutoJoin,   join::Cloned>);
static_assert(!std::is_same_v<join::AutoJoin,   join::Forked>);
static_assert(!std::is_same_v<join::AutoJoin,   join::PosixSpawn>);
static_assert(!std::is_same_v<join::ManualJoin, join::Detached>);
static_assert(!std::is_same_v<join::ManualJoin, join::Cloned>);
static_assert(!std::is_same_v<join::ManualJoin, join::Forked>);
static_assert(!std::is_same_v<join::ManualJoin, join::PosixSpawn>);
static_assert(!std::is_same_v<join::Detached,   join::Cloned>);
static_assert(!std::is_same_v<join::Detached,   join::Forked>);
static_assert(!std::is_same_v<join::Detached,   join::PosixSpawn>);
static_assert(!std::is_same_v<join::Cloned,     join::Forked>);
static_assert(!std::is_same_v<join::Cloned,     join::PosixSpawn>);
static_assert(!std::is_same_v<join::Forked,     join::PosixSpawn>);

// ── Default points to AutoJoin (the structured-concurrency choice) ─
static_assert(std::is_same_v<join::Default, join::AutoJoin>);

// ── `final` qualifier prevents inheritance-laundering ──────────────
static_assert(std::is_final_v<join::AutoJoin>);
static_assert(std::is_final_v<join::ManualJoin>);
static_assert(std::is_final_v<join::Detached>);
static_assert(std::is_final_v<join::Cloned>);
static_assert(std::is_final_v<join::Forked>);
static_assert(std::is_final_v<join::PosixSpawn>);

// ── Cardinality witness — six mechanisms today; append-only ────────
static_assert(join::join_mechanism_count == 6);

// ── Append-only enumerator ordinals — federation cache stability ──
//
// The V-203 invariant: each enumerator's underlying ordinal is FROZEN
// at ship.  Any future mechanism (e.g. IoUringSqpoll) lands at the
// next free position; existing positions never renumber.  A consumer
// (V-204 grant tree, federation cache, mint_spawn dispatch) that
// indexes a per-mechanism slot by underlying value would silently
// alias if these ordinals drifted.
static_assert(std::to_underlying(join::JoinMechanism::AutoJoin)   == 0);
static_assert(std::to_underlying(join::JoinMechanism::ManualJoin) == 1);
static_assert(std::to_underlying(join::JoinMechanism::Detached)   == 2);
static_assert(std::to_underlying(join::JoinMechanism::Cloned)     == 3);
static_assert(std::to_underlying(join::JoinMechanism::Forked)     == 4);
static_assert(std::to_underlying(join::JoinMechanism::PosixSpawn) == 5);

// ── Diagnostic names — full per-mechanism switch coverage ──────────
static_assert(join::name_of(join::JoinMechanism::AutoJoin)   == "AutoJoin");
static_assert(join::name_of(join::JoinMechanism::ManualJoin) == "ManualJoin");
static_assert(join::name_of(join::JoinMechanism::Detached)   == "Detached");
static_assert(join::name_of(join::JoinMechanism::Cloned)     == "Cloned");
static_assert(join::name_of(join::JoinMechanism::Forked)     == "Forked");
static_assert(join::name_of(join::JoinMechanism::PosixSpawn) == "PosixSpawn");

}  // namespace

int main() {
    // Compile-time-only surface; the sentinel TU verifies header-
    // embedded static_asserts under project warnings.  No runtime
    // observable behavior.
    return 0;
}

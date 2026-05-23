// FIXY-V-204 sentinel TU: fixy/spawn/SpawnGrant.h — the engagement-
// grant tree (detach_with / syscall_only / subprocess / fork_parent /
// exec_ctx) + rationale_nonempty_v discipline + JoinPolicyGrantsCoherent
// bridge.  Forces every header-embedded static_assert through project
// warnings-as-errors per feedback_header_only_static_assert_blind_spot.
//
// V-204 ships NO new mint factory; the soundness gate is in-class
// `static_assert(rationale_nonempty_v<Rationale>)` per grant (fires at
// instantiation, BEFORE any consumer sees the grant) + the
// JoinPolicyGrantsCoherent concept the V-203×V-204 bridge exposes.
//
// HS14 floor: 3 fixtures (test/fixy_neg/):
//   1. neg_fixy_v_204_grant_empty_rationale — in-class static_assert
//      fires on `detach_with<"">` BEFORE any consumer sees it.
//   2. neg_fixy_v_204_grant_detached_missing_detach_with — the
//      coherence concept rejects join::Detached with no detach_with<>
//      in the grant pack.
//   3. neg_fixy_v_204_grant_forked_missing_subprocess — the
//      coherence concept rejects join::Forked with no subprocess<>
//      in the grant pack.  Distinct from #2: process-spawn axis
//      vs detach-spawn axis (different syscall family + script-side
//      CRUCIBLE_SPAWN_ALLOW_PROCESS guard from V-210).

#include <crucible/fixy/spawn/SpawnGrant.h>

#include <type_traits>

namespace {

namespace grant = ::crucible::fixy::spawn::grant;
namespace join  = ::crucible::fixy::spawn::join;
namespace dim   = ::crucible::fixy::dim;
namespace fg    = ::crucible::fixy::grant;
namespace cf_ns = ::crucible::fixy::spawn;
using cf_ns::JoinPolicyGrantsCoherent;
using fg::ctrl::rationale;

// ── Rationale-nonempty witness ─────────────────────────────────────
static_assert( grant::rationale_nonempty_v<rationale{"x"}>);
static_assert( grant::rationale_nonempty_v<rationale{"this is a valid reason"}>);
static_assert(!grant::rationale_nonempty_v<rationale{""}>);

// ── Grant instantiation with valid rationale compiles ─────────────
static_assert(std::is_class_v<grant::detach_with<rationale{"logger drain"}>>);
static_assert(std::is_class_v<grant::syscall_only<rationale{"perf bpf"}>>);
static_assert(std::is_class_v<grant::subprocess<rationale{"CLI launcher"}>>);

// ── Reason field round-trips ──────────────────────────────────────
static_assert(grant::detach_with<rationale{"audit"}>::reason.size() == 6);
static_assert(grant::syscall_only<rationale{"clone3"}>::reason.size() == 7);
static_assert(grant::subprocess<rationale{"exec"}>::reason.size() == 5);

// ── EBO size: every grant is sizeof 1 (empty-class-collapsible) ────
static_assert(sizeof(grant::detach_with<rationale{"x"}>)  == 1);
static_assert(sizeof(grant::syscall_only<rationale{"x"}>) == 1);
static_assert(sizeof(grant::subprocess<rationale{"x"}>)   == 1);
struct test_parent_tag {};
struct test_ctx {};
static_assert(sizeof(grant::fork_parent<test_parent_tag>) == 1);
static_assert(sizeof(grant::exec_ctx<test_ctx>)           == 1);

// ── grant_base inheritance — recognized by the fixy::grant substrate ──
static_assert(std::is_base_of_v<fg::grant_base, grant::detach_with<rationale{"x"}>>);
static_assert(std::is_base_of_v<fg::grant_base, grant::syscall_only<rationale{"x"}>>);
static_assert(std::is_base_of_v<fg::grant_base, grant::subprocess<rationale{"x"}>>);
static_assert(std::is_base_of_v<fg::grant_base, grant::fork_parent<test_parent_tag>>);
static_assert(std::is_base_of_v<fg::grant_base, grant::exec_ctx<test_ctx>>);

// ── which_dim routes every grant to DimensionAxis::Protocol ────────
static_assert(fg::which_dim_v<grant::detach_with<rationale{"x"}>>  == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<grant::syscall_only<rationale{"x"}>> == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<grant::subprocess<rationale{"x"}>>   == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<grant::fork_parent<test_parent_tag>> == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<grant::exec_ctx<test_ctx>>           == dim::DimensionAxis::Protocol);

// ── Per-family probe ──────────────────────────────────────────────
static_assert( grant::has_detach_with_v<grant::detach_with<rationale{"r"}>>);
static_assert(!grant::has_detach_with_v<grant::subprocess<rationale{"r"}>>);
static_assert(!grant::has_detach_with_v<>);  // empty pack

static_assert( grant::has_syscall_only_v<grant::syscall_only<rationale{"r"}>>);
static_assert(!grant::has_syscall_only_v<grant::detach_with<rationale{"r"}>>);

static_assert( grant::has_subprocess_v<grant::subprocess<rationale{"r"}>>);
static_assert(!grant::has_subprocess_v<grant::syscall_only<rationale{"r"}>>);

// Mixed packs: probe still finds the right family.
static_assert( grant::has_detach_with_v<
    grant::fork_parent<test_parent_tag>,
    grant::detach_with<rationale{"r"}>,
    grant::exec_ctx<test_ctx>>);
static_assert(!grant::has_subprocess_v<
    grant::fork_parent<test_parent_tag>,
    grant::detach_with<rationale{"r"}>,
    grant::exec_ctx<test_ctx>>);

// ── JoinPolicyGrantsCoherent — V-203×V-204 bridge truth table ─────
//
// AutoJoin & ManualJoin: pass with ANY grant set (incl. empty).
static_assert(JoinPolicyGrantsCoherent<join::AutoJoin>);
static_assert(JoinPolicyGrantsCoherent<join::ManualJoin>);
static_assert(JoinPolicyGrantsCoherent<join::AutoJoin,
    grant::fork_parent<test_parent_tag>>);

// Detached: needs detach_with<>.
static_assert(!JoinPolicyGrantsCoherent<join::Detached>);
static_assert( JoinPolicyGrantsCoherent<join::Detached,
    grant::detach_with<rationale{"reason"}>>);
static_assert(!JoinPolicyGrantsCoherent<join::Detached,
    grant::syscall_only<rationale{"wrong"}>>);
static_assert(!JoinPolicyGrantsCoherent<join::Detached,
    grant::subprocess<rationale{"wrong"}>>);

// Cloned: needs syscall_only<>.
static_assert(!JoinPolicyGrantsCoherent<join::Cloned>);
static_assert( JoinPolicyGrantsCoherent<join::Cloned,
    grant::syscall_only<rationale{"perf"}>>);
static_assert(!JoinPolicyGrantsCoherent<join::Cloned,
    grant::detach_with<rationale{"wrong"}>>);

// Forked: needs subprocess<>.
static_assert(!JoinPolicyGrantsCoherent<join::Forked>);
static_assert( JoinPolicyGrantsCoherent<join::Forked,
    grant::subprocess<rationale{"launcher"}>>);
static_assert(!JoinPolicyGrantsCoherent<join::Forked,
    grant::detach_with<rationale{"wrong"}>>);

// PosixSpawn: same subprocess<> family.
static_assert(!JoinPolicyGrantsCoherent<join::PosixSpawn>);
static_assert( JoinPolicyGrantsCoherent<join::PosixSpawn,
    grant::subprocess<rationale{"test harness"}>>);

// Mixed-grant-pack with right family + bystanders passes.
static_assert(JoinPolicyGrantsCoherent<join::Detached,
    grant::fork_parent<test_parent_tag>,
    grant::detach_with<rationale{"justified"}>,
    grant::exec_ctx<test_ctx>>);

// Non-mechanism first arg fails (IsJoinMechanismTag predicate fires).
static_assert(!JoinPolicyGrantsCoherent<int>);
static_assert(!JoinPolicyGrantsCoherent<void>);

// ── Distinct rationales produce distinct types — federation cache ─
static_assert(!std::is_same_v<
    grant::detach_with<rationale{"reason_one"}>,
    grant::detach_with<rationale{"reason_two"}>>);
static_assert(!std::is_same_v<
    grant::subprocess<rationale{"forked"}>,
    grant::subprocess<rationale{"posix_spawned"}>>);

// ── ParentTag / Ctx identity carried through fork_parent / exec_ctx ──
static_assert(std::is_same_v<
    grant::fork_parent<test_parent_tag>::parent_tag,
    test_parent_tag>);
static_assert(std::is_same_v<
    grant::exec_ctx<test_ctx>::ctx_type,
    test_ctx>);

}  // namespace

int main() {
    // Compile-time-only surface; the sentinel TU verifies header-
    // embedded static_asserts under project warnings.  No runtime
    // observable behavior.
    return 0;
}

// These grants have no mint factory.  The soundness gate is an in-class
// assertion on each grant, which fires at instantiation, before any
// consumer can see the grant, plus the coherence concept that pairs a
// join mechanism with the grant family it requires.

#include <crucible/fixy/spawn/_SpawnGrant.h>

#include <type_traits>

namespace {

namespace grant = ::crucible::fixy::spawn::grant;
namespace join = ::crucible::fixy::spawn::join;
namespace dim = ::crucible::fixy::dim;
namespace fg = ::crucible::fixy::grant;
namespace cf_ns = ::crucible::fixy::spawn;
using cf_ns::JoinPolicyGrantsCoherent;
using fg::ctrl::rationale;

static_assert(grant::rationale_nonempty_v<rationale{"x"}>);
static_assert(grant::rationale_nonempty_v<rationale{"this is a valid reason"}>);
static_assert(!grant::rationale_nonempty_v<rationale{""}>);

static_assert(std::is_class_v<grant::detach_with<rationale{"logger drain"}>>);
static_assert(std::is_class_v<grant::syscall_only<rationale{"perf bpf"}>>);
static_assert(std::is_class_v<grant::subprocess<rationale{"CLI launcher"}>>);

static_assert(grant::detach_with<rationale{"audit"}>::reason.size() == 6);
static_assert(grant::syscall_only<rationale{"clone3"}>::reason.size() == 7);
static_assert(grant::subprocess<rationale{"exec"}>::reason.size() == 5);

static_assert(sizeof(grant::detach_with<rationale{"x"}>) == 1);
static_assert(sizeof(grant::syscall_only<rationale{"x"}>) == 1);
static_assert(sizeof(grant::subprocess<rationale{"x"}>) == 1);
struct test_parent_tag {};
struct test_ctx {};
static_assert(sizeof(grant::fork_parent<test_parent_tag>) == 1);
static_assert(sizeof(grant::exec_ctx<test_ctx>) == 1);

// Inheriting the grant base is what makes the substrate treat these as
// grants at all.
static_assert(std::is_base_of_v<fg::grant_base, grant::detach_with<rationale{"x"}>>);
static_assert(std::is_base_of_v<fg::grant_base, grant::syscall_only<rationale{"x"}>>);
static_assert(std::is_base_of_v<fg::grant_base, grant::subprocess<rationale{"x"}>>);
static_assert(std::is_base_of_v<fg::grant_base, grant::fork_parent<test_parent_tag>>);
static_assert(std::is_base_of_v<fg::grant_base, grant::exec_ctx<test_ctx>>);

static_assert(fg::which_dim_v<grant::detach_with<rationale{"x"}>> == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<grant::syscall_only<rationale{"x"}>> == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<grant::subprocess<rationale{"x"}>> == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<grant::fork_parent<test_parent_tag>> == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<grant::exec_ctx<test_ctx>> == dim::DimensionAxis::Protocol);

static_assert(grant::has_detach_with_v<grant::detach_with<rationale{"r"}>>);
static_assert(!grant::has_detach_with_v<grant::subprocess<rationale{"r"}>>);
static_assert(!grant::has_detach_with_v<>);  // empty pack

static_assert(grant::has_syscall_only_v<grant::syscall_only<rationale{"r"}>>);
static_assert(!grant::has_syscall_only_v<grant::detach_with<rationale{"r"}>>);

static_assert(grant::has_subprocess_v<grant::subprocess<rationale{"r"}>>);
static_assert(!grant::has_subprocess_v<grant::syscall_only<rationale{"r"}>>);

// Mixed packs: probe still finds the right family.
static_assert(grant::has_detach_with_v<grant::fork_parent<test_parent_tag>, grant::detach_with<rationale{"r"}>,
                                       grant::exec_ctx<test_ctx>>);
static_assert(!grant::has_subprocess_v<grant::fork_parent<test_parent_tag>, grant::detach_with<rationale{"r"}>,
                                       grant::exec_ctx<test_ctx>>);

// AutoJoin and ManualJoin pass with any grant set, including an empty one.
static_assert(JoinPolicyGrantsCoherent<join::AutoJoin>);
static_assert(JoinPolicyGrantsCoherent<join::ManualJoin>);
static_assert(JoinPolicyGrantsCoherent<join::AutoJoin, grant::fork_parent<test_parent_tag>>);

// Detached: needs detach_with<>.
static_assert(!JoinPolicyGrantsCoherent<join::Detached>);
static_assert(JoinPolicyGrantsCoherent<join::Detached, grant::detach_with<rationale{"reason"}>>);
static_assert(!JoinPolicyGrantsCoherent<join::Detached, grant::syscall_only<rationale{"wrong"}>>);
static_assert(!JoinPolicyGrantsCoherent<join::Detached, grant::subprocess<rationale{"wrong"}>>);

// Cloned: needs syscall_only<>.
static_assert(!JoinPolicyGrantsCoherent<join::Cloned>);
static_assert(JoinPolicyGrantsCoherent<join::Cloned, grant::syscall_only<rationale{"perf"}>>);
static_assert(!JoinPolicyGrantsCoherent<join::Cloned, grant::detach_with<rationale{"wrong"}>>);

// Forked: needs subprocess<>.
static_assert(!JoinPolicyGrantsCoherent<join::Forked>);
static_assert(JoinPolicyGrantsCoherent<join::Forked, grant::subprocess<rationale{"launcher"}>>);
static_assert(!JoinPolicyGrantsCoherent<join::Forked, grant::detach_with<rationale{"wrong"}>>);

// PosixSpawn: same subprocess<> family.
static_assert(!JoinPolicyGrantsCoherent<join::PosixSpawn>);
static_assert(JoinPolicyGrantsCoherent<join::PosixSpawn, grant::subprocess<rationale{"test harness"}>>);

// Mixed-grant-pack with right family + bystanders passes.
static_assert(JoinPolicyGrantsCoherent<join::Detached, grant::fork_parent<test_parent_tag>,
                                       grant::detach_with<rationale{"justified"}>, grant::exec_ctx<test_ctx>>);

// A first argument that is not a join mechanism fails the concept outright.
static_assert(!JoinPolicyGrantsCoherent<int>);
static_assert(!JoinPolicyGrantsCoherent<void>);

// The rationale is part of the type, so two grants written for different
// reasons land in different federation cache slots.
static_assert(
    !std::is_same_v<grant::detach_with<rationale{"reason_one"}>, grant::detach_with<rationale{"reason_two"}>>);
static_assert(!std::is_same_v<grant::subprocess<rationale{"forked"}>, grant::subprocess<rationale{"posix_spawned"}>>);

static_assert(std::is_same_v<grant::fork_parent<test_parent_tag>::parent_tag, test_parent_tag>);
static_assert(std::is_same_v<grant::exec_ctx<test_ctx>::ctx_type, test_ctx>);

}  // namespace

int main() {
    // Every claim here is a compile-time one.
    return 0;
}

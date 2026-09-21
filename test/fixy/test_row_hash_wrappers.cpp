// The row hash is one fold in foundation/diag/RowHash.h, and this is the
// cell that proves the fold reaches the wrappers it claims to cover.
//
// Half the wrappers in fixy are aliases for Graded and half are classes
// that inherit graded_facade over it. The old row hash needed a partial
// specialisation per wrapper and got both halves by naming each type.
// The fold names none of them, so the property it owes this tree is that
// every wrapper still lands in a slot of its own. A wrapper that fell
// through would contribute zero and share one slot with every bare type,
// which is how a secret value and a plain one would come to share a
// cache entry.
//
// The coverage question extends to the one carrier that deliberately does
// not reach that fold. A multi-axis binding publishes a grade per axis
// rather than one lattice, so it carries its own fold in fixy/Fn.h, and
// it falls through to the same primary template if that fold goes
// missing. The last cells below are its share of the question.
//
// test/foundation/test_row_hash.cpp owns the algebra of the fold and the
// published wire format. This file owns only the coverage question, and
// it is here rather than there because foundation must not depend on
// fixy.

#include <fixy/Bands.h>
#include <fixy/Fn.h>
#include <fixy/Mutation.h>
#include <fixy/Qtt.h>
#include <fixy/Role.h>
#include <fixy/Secret.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/diag/RowHash.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

namespace fd = ::foundation::diag;

using fd::row_hash_contribution_v;

// One wrapper per shape the tree uses. Tagged, Secret, Qtt, Stale and
// Monotonic are classes over graded_facade. DetSafe and HotPath are
// aliases for Graded. Both halves must land somewhere other than zero.

using TaggedVerified = ::fixy::Tagged<int, ::fixy::tags::trust::Verified>;
using TaggedUnverified = ::fixy::Tagged<int, ::fixy::tags::trust::Unverified>;
using SecretInt = ::fixy::Secret<int>;
using LinearInt = ::fixy::Linear<int>;
using AffineInt = ::fixy::Affine<int>;
using StaleInt = ::fixy::Stale<int>;
using PureInt = ::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, int>;
using HotInt = ::fixy::HotPath<::fixy::HotPathTier_v::Hot, int>;

// Nothing falls through. This is the assertion the old header needed
// fifty-two specialisations to be able to make.
static_assert(row_hash_contribution_v<TaggedVerified> != 0, "Tagged falls through the fold and shares a slot with "
                                                            "every bare type");
static_assert(row_hash_contribution_v<SecretInt> != 0, "Secret falls through the fold and shares a slot with every "
                                                       "bare type");
static_assert(row_hash_contribution_v<LinearInt> != 0, "Linear falls through the fold and shares a slot with every "
                                                       "bare type");
static_assert(row_hash_contribution_v<StaleInt> != 0, "Stale falls through the fold and shares a slot with every bare "
                                                      "type");
static_assert(row_hash_contribution_v<PureInt> != 0);
static_assert(row_hash_contribution_v<HotInt> != 0);

// Two tags of one axis separate, which is the discrimination a trust
// boundary depends on.
static_assert(row_hash_contribution_v<TaggedVerified> != row_hash_contribution_v<TaggedUnverified>);

// Two grades of the usage axis separate.
static_assert(row_hash_contribution_v<LinearInt> != row_hash_contribution_v<AffineInt>);

// Wrappers of different axes separate, across both shapes.
static_assert(row_hash_contribution_v<SecretInt> != row_hash_contribution_v<LinearInt>);
static_assert(row_hash_contribution_v<SecretInt> != row_hash_contribution_v<StaleInt>);
static_assert(row_hash_contribution_v<SecretInt> != row_hash_contribution_v<PureInt>);
static_assert(row_hash_contribution_v<PureInt> != row_hash_contribution_v<HotInt>);
static_assert(row_hash_contribution_v<TaggedVerified> != row_hash_contribution_v<SecretInt>);

// A stack is a fold, so the outer layer does not erase the inner one and
// the two orders are different keys.
static_assert(row_hash_contribution_v<::fixy::Secret<LinearInt>> != row_hash_contribution_v<SecretInt>);
static_assert(row_hash_contribution_v<::fixy::Secret<LinearInt>> != row_hash_contribution_v<LinearInt>);
static_assert(row_hash_contribution_v<::fixy::Secret<LinearInt>>
              != row_hash_contribution_v<::fixy::Linear<SecretInt>>);

// A class-shaped wrapper nests inside an alias-shaped one and stays
// visible, which is the property that makes the two shapes one fold.
static_assert(row_hash_contribution_v<::fixy::DetSafe<::fixy::DetSafeTier_v::Pure, SecretInt>>
              != row_hash_contribution_v<PureInt>);

// The payload is the content hash's business, not this one's.
static_assert(row_hash_contribution_v<::fixy::Secret<int>> == row_hash_contribution_v<::fixy::Secret<double>>);

// ---------------------------------------------------------------------
// The one carrier that does not reach the fold above, and must not.
//
// Every wrapper named so far publishes one lattice, one modality and one
// payload, which is what the graded fold reads. A binding publishes a
// grade per axis instead: it IS the resolver across the axes, so there is
// no single lattice for it to name. It carries its own fold in
// fixy/Fn.h, and the coverage question this file owns therefore extends
// to it — with no fold of its own a binding falls through to the primary
// template just as an unrecognised wrapper would, and every binding in
// the tree shares one slot with every bare type.
//
// The two roles below carry the same payload and differ on the Effect and
// Security axes. A pure copy is safe to share between installations; a
// binding that performs IO and emits publicly is not. One slot for both
// serves the first's kernel to the second's caller.

using PureCopyInt = ::fixy::role::PureCopy<int>;
using IoFunctionInt = ::fixy::role::IoFunction<int>;
using BgWorkerInt = ::fixy::role::BgWorker<int>;

static_assert(!::foundation::diag::GradedShaped<PureCopyInt>,
              "a binding must not be made to satisfy the graded shape; it has no single lattice");
static_assert(row_hash_contribution_v<PureCopyInt> != 0, "a binding falls through the fold and shares a slot with "
                                                         "every bare type");
static_assert(row_hash_contribution_v<PureCopyInt> != row_hash_contribution_v<IoFunctionInt>);
static_assert(row_hash_contribution_v<BgWorkerInt> != row_hash_contribution_v<IoFunctionInt>);

// A binding nests a row-bearing payload without erasing it, which is what
// makes the Type axis a recursion rather than a drop.
static_assert(row_hash_contribution_v<::fixy::fn<SecretInt>> != row_hash_contribution_v<::fixy::fn<int>>);
static_assert(row_hash_contribution_v<::fixy::fn<SecretInt>> != row_hash_contribution_v<SecretInt>);

struct TestFailure {};
int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "\n    %s\n", what);
        throw TestFailure{};
    }
}

void test_every_wrapper_reaches_runtime() {
    std::uint64_t const secret = row_hash_contribution_v<SecretInt>;
    std::uint64_t const linear = row_hash_contribution_v<LinearInt>;
    std::uint64_t const tagged = row_hash_contribution_v<TaggedVerified>;
    check(secret != 0, "Secret contributes nothing at run time");
    check(linear != 0, "Linear contributes nothing at run time");
    check(tagged != 0, "Tagged contributes nothing at run time");
    check(secret != linear && secret != tagged && linear != tagged,
          "two wrappers of different axes share one slot at run time");
}

void test_every_binding_reaches_runtime() {
    std::uint64_t const pure_copy = row_hash_contribution_v<PureCopyInt>;
    std::uint64_t const io_function = row_hash_contribution_v<IoFunctionInt>;
    std::uint64_t const bg_worker = row_hash_contribution_v<BgWorkerInt>;
    check(pure_copy != 0, "a pure-copy binding contributes nothing at run time");
    check(io_function != 0, "an IO binding contributes nothing at run time");
    check(bg_worker != 0, "a background-worker binding contributes nothing at run time");
    check(pure_copy != io_function, "a pure-copy binding and an IO binding share one slot at run time");
    check(bg_worker != io_function, "two bindings declaring different effect rows share one slot at run time");
}

// The roster is read out of the role namespace, so a role added there is
// covered here without an edit. A role at zero shares a slot with every
// bare payload in the tree.
void test_every_role_is_off_the_zero_slot() {
    check(::fixy::detail::role_self_test::roles_declared() > 0, "the reflected role roster is empty, so this check "
                                                                "proves nothing");
    check(::fixy::detail::role_self_test::roles_off_the_zero_slot() == ::fixy::detail::role_self_test::roles_declared(),
          "a role reaches the zero slot, or has an arity this check cannot instantiate");
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_row_hash_wrappers:\n");
    run_test("test_every_wrapper_reaches_runtime", test_every_wrapper_reaches_runtime);
    run_test("test_every_binding_reaches_runtime", test_every_binding_reaches_runtime);
    run_test("test_every_role_is_off_the_zero_slot", test_every_role_is_off_the_zero_slot);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}

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
// test/foundation/test_row_hash.cpp owns the algebra of the fold and the
// published wire format. This file owns only the coverage question, and
// it is here rather than there because foundation must not depend on
// fixy.

#include <fixy/Bands.h>
#include <fixy/Mutation.h>
#include <fixy/Qtt.h>
#include <fixy/Secret.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/diag/RowHash.h>

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

}  // namespace

int main() {
    std::fprintf(stderr, "test_row_hash_wrappers:\n");
    run_test("test_every_wrapper_reaches_runtime", test_every_wrapper_reaches_runtime);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}

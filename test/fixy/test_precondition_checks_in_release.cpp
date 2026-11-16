// A precondition and a postcondition do their checks in a Release build.
//
// A Release library compiles with NDEBUG and the observe semantic, and this
// translation unit compiles with the same two.  Each test gets -UNDEBUG from
// its factory, and the registration of this test puts -DNDEBUG after it.
// The test proves that NDEBUG does not change CRUCIBLE_PRE or CRUCIBLE_POST.
// A violated condition must stop the process through the violation handler,
// and it must not become an assumption for the optimizer.
//
// fixy::mint_refined is the door that runs the predicate, and its check is
// a CRUCIBLE_PRE.  If that check is only an assumption, a value that fails
// the predicate goes through the checked door as if it were trusted.
//
// Each attack runs in a child process through expect_dies of the Check
// family.  The test passes only when the child stops by SIGABRT and its
// standard error names the violated condition.  The control runs a good
// value in the test process, and it must not abort.

#if !defined(NDEBUG)
#error "Compile this test with NDEBUG.  It proves the Release form of the contract macros."
#endif
#if defined(CRUCIBLE_CONTRACT_SEMANTIC_IGNORE)
#error "Compile this test on a semantic that checks.  The ignore semantic removes the checks by design."
#endif

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/check/Check.h>
#include <foundation/contracts/Post.h>
#include <foundation/effects/Effect.h>

namespace {

// Volatile, so the optimizer cannot know the value that an attack uses.
int volatile opaque_negative = -1;
int volatile opaque_positive = 7;
int volatile opaque_one = 1;
int volatile sink = 0;

void mint_refined_of_a_negative_value() {
    auto const refined = ::fixy::mint_refined<::fixy::positive>(static_cast<int>(opaque_negative));
    sink = refined.value();
}

void mint_refined_of_a_positive_value() {
    auto const refined = ::fixy::mint_refined<::fixy::positive>(static_cast<int>(opaque_positive));
    sink = refined.value();
}

// The postcondition promises a positive result, and an input of one breaks
// that promise.
[[gnu::noinline]] int decrement_to_positive(int const x) noexcept {
    int const result = x - 1;
    CRUCIBLE_POST(result, result > 0);
    return result;
}

void decrement_one() { sink = decrement_to_positive(opaque_one); }

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::check::expect_dies(test, mint_refined_of_a_negative_value,
                               ::fixy::TextView{"foundation: contract violation: Pred(v)"});
    ::fixy::check::expect_dies(test, decrement_one, ::fixy::TextView{"foundation: contract violation: result > 0"});
    mint_refined_of_a_positive_value();
    CRUCIBLE_TEST_ASSERT(test, sink == 7);
    ::fixy::check::pass(test, "test_precondition_checks_in_release: each violation aborts, and a good value passes\n");
    return 0;
}

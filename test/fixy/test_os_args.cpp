// The argument door of fixy/os/Args.h.  ctest runs the test two times: with
// no argument, and with the three arguments "one", "" and "three four".  Each
// run reads the arguments through Args: the count, each text, the empty
// value past the count, and the loop.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Args.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main(int argc, char** argv) {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::os::Args const arguments = ::fixy::os::args(argc, argv);

    CRUCIBLE_TEST_ASSERT(test, arguments.size() == 1 || arguments.size() == 4);
    ::fixy::TextView const program = arguments.get(0).expect("the C runtime gives main the name of the program").view();
    CRUCIBLE_TEST_ASSERT(test, ::fixy::ends_with(program, ::fixy::TextView{"test_os_args"}));
    CRUCIBLE_TEST_ASSERT(test, arguments.get(arguments.size()).is_none());

    if (arguments.size() == 4) {
        CRUCIBLE_TEST_ASSERT(test, arguments.get(1).expect("the first argument").view() == ::fixy::TextView{"one"});
        CRUCIBLE_TEST_ASSERT(test, arguments.get(2).expect("the second argument").size() == 0);
        CRUCIBLE_TEST_ASSERT(test,
                             arguments.get(3).expect("the third argument").view() == ::fixy::TextView{"three four"});
    }

    std::size_t count = 0;
    std::size_t characters = 0;
    for (::fixy::CText const argument : arguments) {
        ++count;
        characters += argument.size();
    }
    CRUCIBLE_TEST_ASSERT(test, count == arguments.size());
    CRUCIBLE_TEST_ASSERT(test, characters >= program.size());

    ::fixy::check::pass(test, "test_os_args: PASSED with {} arguments\n", arguments.size());
    return 0;
}

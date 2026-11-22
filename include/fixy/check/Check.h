#pragma once

// The Check family: the language of the tests.
//
//   pass(test, text, args...)          writes the pass line of a test to
//                                      standard output, through report
//   CRUCIBLE_TEST_ASSERT(test, cond)   ends the test with the text of cond
//                                      and the site, through fatal, when
//                                      cond is false.  cond can hold commas
//   expect_dies(test, body, text)      runs body in a child process, and
//                                      ends the test when the child does
//                                      not end by SIGABRT with text on its
//                                      standard error
//   expect_dies_by(test, body, signal) runs body in a child process with
//                                      the default action of signal, and
//                                      ends the test when the child does
//                                      not end by that signal
//   Tally checks{test, name}           counts the failed checks of a test
//   CRUCIBLE_TEST_EXPECT(checks, cond) writes the text of cond and the site,
//                                      and counts a failure, when cond is
//                                      false.  The test continues
//   std::move(checks).close()          ends the test through fatal when a
//                                      check of the Tally failed
//
// fixy/check/Property.h gives the property tests and the oracle of the
// family.
//
// Test context
//   Each operation takes a context that owns the Test capability
//   (TestContext).  Only the testing door of foundation/effects/Effect.h
//   mints the Test source, and the rule owners refuses that door in a unit
//   that ships.  So a unit that ships calls no operation of the family.  A
//   test holds one context, such as
//     ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
//
// Death test
//   expect_dies starts the child through the child door of fixy/os/Process.h.
//   The body runs only in the child, after a fork, so it may use only what is
//   safe after a fork (the child door of that header).  The parent reads the
//   standard error of the child to its end and waits for the child.  A
//   child that ends by SIGABRT, with the expected text in the last
//   fixy::process::kChildOutputCapacity bytes of its standard error, passes.
//   On each other end, the test writes the end and the output of the child,
//   and ends with the site of the call.
//
// Death by a signal
//   expect_dies_by tests a body that ends by a signal of the kernel, such as
//   a read of a page with no access.  The child first puts back the default
//   action of the signal (the child reset of fixy/os/Signal.h).  Then no
//   handler of the process or of a sanitizer turns the end into an exit or
//   writes a report.  A child that ends by that signal passes, and the test
//   reads no text.  On each other end, the test writes the end and the
//   output of the child, and ends with the site of the call.
//
// Counting check
//   A test that reads many facts in one run can count its failures in place
//   of an end at the first one.  Then one run of the test gives each failed
//   check.  A Tally counts the checks of one test.  CRUCIBLE_TEST_EXPECT
//   writes "check failed: CONDITION" with the name of the test and the site
//   of the check to standard error, and the test continues.  The close of
//   the Tally ends the test through fatal when one check failed, so the test
//   fails at its end.  A Tally is linear, and close is its drop door, so the
//   rule must-consume refuses a path that does not close it.  The destructor
//   of a Tally that was not closed ends the process with the name of its
//   test, also in a build with no plugin.  A Tally cannot be copied, so no
//   failure is counted two times or lost.
//
// The header is in the layer X7_check of utils/rule-table.txt, above the
// process door of the layer X3_os.  src/fixy/check/Check.cpp holds the cold
// reports.

#include <fixy/os/Process.h>
#include <fixy/os/Signal.h>
#include <foundation/Linear.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Fatal.h>
#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Abort.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::check {

namespace eff = ::foundation::effects;

// A context of a test: an execution context that owns the Test capability.
template <typename Ctx>
concept TestContext = eff::IsExecCtx<Ctx> && eff::CtxOwnsCapability<Ctx, eff::Effect::Test>;

namespace detail {

// Writes the end and the output of a child that did not end as expect_dies
// expects, and ends the process with the site of the call of expect_dies.
[[noreturn, gnu::cold]] void report_unexpected_end_(::fixy::process::ChildReport const& report,
                                                    ::foundation::core::TextView expected,
                                                    ::foundation::core::Site site) noexcept;

// Writes the end and the output of a child that did not end by the signal
// that expect_dies_by expects, and ends the process with the site of the
// call of expect_dies_by.
[[noreturn, gnu::cold]] void report_unexpected_signal_(::fixy::process::ChildReport const& report,
                                                       ::fixy::signal::FatalSignal expected,
                                                       ::foundation::core::Site site) noexcept;

// Writes the error of the child door, and ends the process with the site of
// the call of expect_dies.
[[noreturn, gnu::cold]] void report_child_error_(::foundation::os::Errno error, ::foundation::core::Site site) noexcept;

// Writes "foundation: fatal: assert: CONDITION" and the site of the assert
// to standard error, and ends the process through the fatal arm of
// foundation/core/Report.h, with SIGABRT.  CONDITION is the text of size
// characters at condition.  The body stays in this header, so an assert needs
// only the library foundation, as the fatal arm does.
[[noreturn, gnu::cold, gnu::noinline]] inline void report_failed_assertion_(char const* condition, std::size_t size,
                                                                            char const* file, std::uint32_t line,
                                                                            char const* function) noexcept {
    ::foundation::core::detail::FmtArg const argument{::foundation::core::detail::FmtArgKind::text, 0, condition, size};
    ::foundation::core::detail::report_fatal_with_("assert: {}", file, line, function, &argument, 1);
}

}  // namespace detail

// Writes the text of the pass line to standard output.  The text holds its
// own line break.  The text is written before the call returns, so a pass
// line before a later failure stays in the log.
template <TestContext Ctx>
[[gnu::cold, gnu::noinline]] void pass(Ctx const& /*test*/, ::foundation::core::Fmt<> text) noexcept {
    ::foundation::core::report(::foundation::core::Sink::Out, text);
}

// The same, with arguments.
template <TestContext Ctx, typename First, typename... Rest>
    requires(::foundation::core::Formattable<First> && (::foundation::core::Formattable<Rest> && ...))
[[gnu::cold, gnu::noinline]] void
pass(Ctx const& /*test*/, ::foundation::core::Fmt<std::type_identity_t<First>, std::type_identity_t<Rest>...> text,
     First first, Rest... rest) noexcept {
    ::foundation::core::report(::foundation::core::Sink::Out, text, first, rest...);
}

// The failure arm of CRUCIBLE_TEST_ASSERT: writes "assert: CONDITION" and the
// site of the assert to standard error, and ends the process with SIGABRT.
// The condition is the string literal of the macro, which the arm takes by
// reference.  The scalar default arguments read the site of the call, as the
// constructor of Fmt does, and the call is the assert.
template <TestContext Ctx, std::size_t Length>
[[noreturn, gnu::always_inline]] inline void
fail_assertion(Ctx const& /*test*/, char const (&condition)[Length], char const* file = __builtin_FILE(),
               int line = __builtin_LINE(), char const* function = __builtin_FUNCTION()) noexcept {
    detail::report_failed_assertion_(condition, Length - 1, file, static_cast<std::uint32_t>(line), function);
}

// Runs body in a child process (the death test).  The test continues when
// the child ends by SIGABRT and its standard error holds expected.  Each
// other end, and an error of the child door, ends the test with the site of
// the call.  The body goes on to the child door as the caller gave it, so a
// closure that the caller gives up reaches the once door in the child.
// Complexity: one child process, and O(n) in the bytes of its output.
template <TestContext Ctx, typename Body>
    requires std::is_invocable_v<Body&>
void expect_dies(Ctx const& test, Body&& body, ::foundation::core::TextView expected,
                 ::foundation::core::Site site = ::foundation::core::Site{}) noexcept {
    namespace core = ::foundation::core;
    namespace os = ::foundation::os;
    namespace process = ::fixy::process;
    using Collected = core::Result<process::ChildReport, os::Errno>;
    Collected collected =
        process::spawn_child(test, static_cast<Body&&>(body))
            .match([](process::Child child) noexcept -> Collected { return process::wait(std::move(child)); },
                   [](os::Errno error) noexcept -> Collected { return core::err(error); });
    if (collected.is_err()) [[unlikely]] {
        detail::report_child_error_(collected.err().value_or(os::Errno::eio), site);
    }
    collected.match(
        [&expected, &site](process::ChildReport const& report) noexcept {
            bool const is_abort = report.is_signaled_by(os::kAbortSignal);
            bool const has_text = core::find(report.output(), expected).is_some();
            if (!is_abort || !has_text) [[unlikely]] {
                detail::report_unexpected_end_(report, expected, site);
            }
        },
        [](os::Errno const&) noexcept {});
}

// Runs body in a child process with the default action of FATAL_SIGNAL (the
// death by a signal).  The test continues when the child ends by that
// signal.  Each other end, and an error of the child door, ends the test with
// the site of the call.  A closure that the caller gives up goes to the once
// door in the child, and a lent body or a function gets one call in place.
// Complexity: one child process, and O(n) in the bytes of its output.
template <TestContext Ctx, typename Body>
    requires std::is_invocable_v<Body&>
void expect_dies_by(Ctx const& test, Body&& body, ::fixy::signal::FatalSignal fatal_signal,
                    ::foundation::core::Site site = ::foundation::core::Site{}) noexcept {
    namespace core = ::foundation::core;
    namespace os = ::foundation::os;
    namespace process = ::fixy::process;
    using Collected = core::Result<process::ChildReport, os::Errno>;
    Collected collected =
        process::spawn_child(test,
                             [&test, &body, fatal_signal](process::ForkedChild const& child) noexcept {
                                 ::fixy::signal::reset_to_default(test, child, fatal_signal);
                                 if constexpr (::foundation::linear::OnceClosure<Body>) {
                                     ::foundation::linear::call_once(static_cast<Body&&>(body));
                                 } else {
                                     body();
                                 }
                             })
            .match([](process::Child child) noexcept -> Collected { return process::wait(std::move(child)); },
                   [](os::Errno error) noexcept -> Collected { return core::err(error); });
    if (collected.is_err()) [[unlikely]] {
        detail::report_child_error_(collected.err().value_or(os::Errno::eio), site);
    }
    collected.match(
        [fatal_signal, &site](process::ChildReport const& report) noexcept {
            if (!report.is_signaled_by(::fixy::signal::number_of(fatal_signal))) [[unlikely]] {
                detail::report_unexpected_signal_(report, fatal_signal, site);
            }
        },
        [](os::Errno const&) noexcept {});
}

// The outcome of one check of a Tally.
enum class CheckOutcome : std::uint8_t {
    held,
    failed
};

class Tally;

namespace detail {

// Writes "check failed: CONDITION", the name of the test and the site of the
// check to standard error.
[[gnu::cold]] void report_failed_check_(::foundation::core::TextView test_name, ::foundation::core::TextView condition,
                                        ::foundation::core::Site site) noexcept;

// Writes the count of the failed checks and the count of all checks of the
// Tally, and ends the process through fatal.
[[noreturn, gnu::cold]] void report_failed_tally_(Tally const& tally) noexcept;

// Ends the process through fatal for a Tally that ended with no close, with
// the name of its test.
[[noreturn, gnu::cold]] void report_unclosed_tally_(::foundation::core::TextView test_name) noexcept;

}  // namespace detail

// A count of the failed checks of one test, which continues after a failure
// (the counting check).  The test owns the Tally, and it passes the Tally by
// reference to each part of the test.  A check costs one increment and one
// branch, and a failure is a cold call.
class [[nodiscard]][[= ::foundation::linear::linear{}]] Tally final {
    // The state is as wide as a count, so the class has no padding byte.
    enum class State : std::uint64_t {
        open,
        closed
    };

    ::foundation::core::TextView name_{};
    std::uint64_t checks_ = 0;
    std::uint64_t failures_ = 0;
    State state_ = State::open;

    friend void detail::report_failed_tally_(Tally const& tally) noexcept;

public:
    // A Tally of the test with the name.  Only a context that owns the Test
    // capability builds one, so a unit that ships builds none.  The
    // constructor is nodiscard, so an expression that builds a Tally and
    // drops it with no close is a warning.
    template <TestContext Ctx>
    [[nodiscard]] Tally(Ctx const& /*test*/, ::foundation::core::TextView name) noexcept : name_{name} {}

    // A move gives the counts to the new Tally.  The old Tally is then closed
    // with no check, so its destructor ends nothing.
    Tally(Tally&& other) noexcept
        : name_{other.name_}, checks_{other.checks_}, failures_{other.failures_}, state_{other.state_} {
        other.checks_ = 0;
        other.failures_ = 0;
        other.state_ = State::closed;
    }

    Tally(Tally const&) =
        delete("a Tally counts the checks of one test: a copy would count a failure two times or lose it");
    Tally& operator=(Tally const&) =
        delete("a Tally counts the checks of one test: a copy would count a failure two times or lose it");
    Tally& operator=(Tally&&) =
        delete("a Tally counts the checks of one test: an assignment would drop the counts of the target");

    // A Tally that was not closed ends the process with the name of its test,
    // because its failures would be lost.
    ~Tally() {
        if (state_ == State::open) [[unlikely]] {
            detail::report_unclosed_tally_(name_);
        }
    }

    // Counts one check.  A failed check writes its condition and its site.
    // CRUCIBLE_TEST_EXPECT calls it with the text of the condition.
    void count(CheckOutcome outcome, ::foundation::core::TextView condition,
               ::foundation::core::Site site = ::foundation::core::Site{}) noexcept {
        CRUCIBLE_PRE(state_ == State::open);
        ++checks_;
        if (outcome == CheckOutcome::failed) [[unlikely]] {
            ++failures_;
            detail::report_failed_check_(name_, condition, site);
        }
    }

    // Closes the Tally.  When a check failed, writes the counts and ends the
    // process through fatal, so the test fails at its end.
    [[= ::foundation::linear::drop_door{}]] void close() && noexcept {
        CRUCIBLE_PRE(state_ == State::open);
        state_ = State::closed;
        if (failures_ != 0) [[unlikely]] {
            detail::report_failed_tally_(*this);
        }
    }
};

}  // namespace fixy::check

// Ends the test when the condition is false (the test context).  The report
// names the condition and the site of the assert: its file, its line and its
// function.  The process ends through fatal, so a death test of an assert
// sees SIGABRT.  A passing assert costs one branch, and the failure is a cold
// call.
//
// The condition is the variadic part of the macro, so it can hold the commas
// of a template argument list, such as lookup<&f, int, double>().is_none().
// The report writes the full condition.
//
// The failure arm takes the condition as a string literal by reference, and
// its default arguments read the site.  So the failure passes an address and
// constants, it builds no object on the stack of the test, and it declares
// no name in the scope of the assert.  An assert in a function of a header of
// any namespace then declares nothing in that namespace.
#define CRUCIBLE_TEST_ASSERT(test, ...)                        \
    do {                                                       \
        if (!(__VA_ARGS__)) [[unlikely]] {                     \
            ::fixy::check::fail_assertion(test, #__VA_ARGS__); \
        }                                                      \
    } while (0)

// Counts one check of the Tally (the counting check).  A false condition
// writes its text, the name of the test and the site of the check, and the
// test continues.  The condition can hold the commas of a template argument
// list, as in CRUCIBLE_TEST_ASSERT.  The text of the condition is a string
// literal, so its view is a constant and the call builds no text.
#define CRUCIBLE_TEST_EXPECT(tally, ...) \
    (tally).count((__VA_ARGS__) ? ::fixy::check::CheckOutcome::held : ::fixy::check::CheckOutcome::failed, #__VA_ARGS__)

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
//
// fixy/check/Property.h gives the property tests and the oracle of the
// family.
//
// THE TEST CONTEXT
//   Each operation takes a context that owns the Test capability
//   (TestContext).  Only the testing door of foundation/effects/Effect.h
//   mints the Test source, and the rule owners refuses that door in a unit
//   that ships.  So a unit that ships calls no operation of the family.  A
//   test holds one context, such as
//     ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
//
// THE DEATH TEST
//   expect_dies starts the child through the child door of fixy/os/Process.h.
//   The body runs only in the child, after a fork, so it may use only what is
//   safe after a fork (THE CHILD DOOR of that header).  The parent reads the
//   standard error of the child to its end and waits for the child.  A
//   child that ends by SIGABRT, with the expected text in the last
//   fixy::process::kChildOutputCapacity bytes of its standard error, passes.
//   On each other end, the test writes the end and the output of the child,
//   and ends with the site of the call.
//
// The header is in the layer X7_check of utils/rule-table.txt, above the
// process door of the layer X3_os.  src/fixy/check/Check.cpp holds the cold
// reports.

#include <fixy/os/Process.h>
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

// Runs body in a child process (THE DEATH TEST).  The test continues when
// the child ends by SIGABRT and its standard error holds expected.  Each
// other end, and an error of the child door, ends the test with the site of
// the call.  Complexity: one child process, and O(n) in the bytes of its
// output.
template <TestContext Ctx, typename Body>
    requires std::is_invocable_v<Body&>
void expect_dies(Ctx const& test, Body&& body, ::foundation::core::TextView expected,
                 ::foundation::core::Site site = ::foundation::core::Site{}) noexcept {
    namespace core = ::foundation::core;
    namespace os = ::foundation::os;
    namespace process = ::fixy::process;
    using Collected = core::Result<process::ChildReport, os::Errno>;
    Collected collected =
        process::spawn_child(test, body)
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

}  // namespace fixy::check

// Ends the test when the condition is false (THE TEST CONTEXT).  The report
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

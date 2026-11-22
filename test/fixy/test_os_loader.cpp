// The loader door of fixy/os/Loader.h (the loader door, the diagnostic).
//
// The cases load the C library, which each process holds, and call its
// getpid through the door in the scope of the library, and the diagnostic
// stays empty.  The door gives the result of the body, and Unit for a body
// that gives void.  A path with no library gives refused_open and calls no
// body, and a name with no function gives refused_symbol.  Each puts the text
// of the loader into the diagnostic, and the next call that succeeds empties
// it.  A text longer than the diagnostic holds keeps its first characters.
// Two scopes of one library nest, and each gives its own functions.  No
// function leaves the scope of its library, because the door and the rule
// brand-escape refuse it (neg_library_function_returned_from_scope and
// neg_library_function_called_after_close).

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Loader.h>
#include <fixy/os/Process.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/PathText.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace fo = ::fixy::os;

// The signature of getpid of the C library.
using ProcessIdRead = int() noexcept;

// The C library, which each process holds.
constexpr ::fixy::CText kCLibrary{"libc.so.6"};

// The count of the characters of the name of a library that does not exist,
// which makes the text of the loader longer than the diagnostic holds.
constexpr std::size_t kLongNameBytes = 2 * fo::kLoaderDiagnosticBytes;

// The failure of a load of path, or no value when the load succeeds.  The end
// of the scope of a load that succeeds closes the library.
[[nodiscard]] ::fixy::Option<fo::LoaderFailure> failure_of_load(::fixy::TestRunnerCtx const& test, ::fixy::CText path,
                                                                fo::LoaderDiagnostic& diagnostic) {
    using Failure = ::fixy::Option<fo::LoaderFailure>;
    return fo::with_library(test, path, ::fixy::to(diagnostic), [](auto const&) noexcept {})
        .match([](::fixy::Unit) -> Failure { return ::fixy::none; },
               [](fo::LoaderFailure refused) { return Failure::some(refused); });
}

// The library loads, and a call of its getpid gives the id of the process.
// The two calls that succeed leave the diagnostic empty.
void call_gives_the_result_of_the_function(::fixy::TestRunnerCtx const& test) {
    fo::LoaderDiagnostic diagnostic{};
    std::uint32_t const process_id =
        fo::with_library(test, kCLibrary, ::fixy::to(diagnostic), [&test, &diagnostic](auto const& library) noexcept {
            CRUCIBLE_TEST_ASSERT(test, diagnostic.text().size() == 0);
            auto const read_id = fo::find_function<ProcessIdRead>(test, library, "getpid", ::fixy::to(diagnostic))
                                     .expect("the C library has getpid");
            CRUCIBLE_TEST_ASSERT(test, diagnostic.text().size() == 0);
            return static_cast<std::uint32_t>(read_id());
        }).expect("the test loads the C library");
    CRUCIBLE_TEST_ASSERT(test, process_id == std::to_underlying(::fixy::process::current_process(test)));
    CRUCIBLE_TEST_ASSERT(test, diagnostic.text().size() == 0);
    ::fixy::check::pass(test, "  call_gives_the_result_of_the_function: PASSED\n");
}

// A path with no library gives refused_open, calls no body, and the
// diagnostic names the path.  A load that then succeeds empties the
// diagnostic.
void missing_library_gives_the_text_of_the_loader(::fixy::TestRunnerCtx const& test) {
    fo::LoaderDiagnostic diagnostic{};
    unsigned body_count = 0;
    fo::LoaderFailure const failure = fo::with_library(test, "libcrucible_no_such_library.so", ::fixy::to(diagnostic),
                                                       [&body_count](auto const&) noexcept { ++body_count; })
                                          .match([](::fixy::Unit) { return fo::LoaderFailure::refused_symbol; },
                                                 [](fo::LoaderFailure refused) { return refused; });
    CRUCIBLE_TEST_ASSERT(test, failure == fo::LoaderFailure::refused_open && body_count == 0);
    CRUCIBLE_TEST_ASSERT(test, ::fixy::find(diagnostic.text(), "libcrucible_no_such_library.so").is_some());
    CRUCIBLE_TEST_ASSERT(test, failure_of_load(test, kCLibrary, diagnostic).is_none());
    CRUCIBLE_TEST_ASSERT(test, diagnostic.text().size() == 0);
    ::fixy::check::pass(test, "  missing_library_gives_the_text_of_the_loader: PASSED\n");
}

// A name with no function gives refused_symbol, and the diagnostic names the
// name.  A lookup that then succeeds empties the diagnostic.
void missing_name_gives_the_text_of_the_loader(::fixy::TestRunnerCtx const& test) {
    fo::LoaderDiagnostic diagnostic{};
    fo::with_library(test, kCLibrary, ::fixy::to(diagnostic), [&test, &diagnostic](auto const& library) noexcept {
        fo::LoaderFailure const failure =
            fo::find_function<ProcessIdRead>(test, library, "crucible_no_such_function", ::fixy::to(diagnostic))
                .match([](auto) { return fo::LoaderFailure::refused_open; },
                       [](fo::LoaderFailure refused) { return refused; });
        CRUCIBLE_TEST_ASSERT(test, failure == fo::LoaderFailure::refused_symbol);
        CRUCIBLE_TEST_ASSERT(test, ::fixy::find(diagnostic.text(), "crucible_no_such_function").is_some());
        CRUCIBLE_TEST_ASSERT(test,
                             fo::find_function<ProcessIdRead>(test, library, "getpid", ::fixy::to(diagnostic)).is_ok());
        CRUCIBLE_TEST_ASSERT(test, diagnostic.text().size() == 0);
    }).expect("the test loads the C library");
    ::fixy::check::pass(test, "  missing_name_gives_the_text_of_the_loader: PASSED\n");
}

// The text of the loader for a long name is longer than the diagnostic holds,
// so the diagnostic keeps its first kLoaderDiagnosticBytes characters, which
// start with the name.
void long_text_keeps_its_first_characters(::fixy::TestRunnerCtx const& test) {
    ::foundation::os::PathText<kLongNameBytes> name{};
    for (std::size_t index = 0; index < kLongNameBytes; ++index) {
        name.append('x').expect("the name fits its text");
    }
    fo::LoaderDiagnostic diagnostic{};
    CRUCIBLE_TEST_ASSERT(test, failure_of_load(test, name.c_text(), diagnostic).is_some());
    CRUCIBLE_TEST_ASSERT(test, ::fixy::starts_with(diagnostic.text(), "xxxxxxxx"));
    CRUCIBLE_TEST_ASSERT(test, diagnostic.text().size() == fo::kLoaderDiagnosticBytes);
    ::fixy::check::pass(test, "  long_text_keeps_its_first_characters: PASSED\n");
}

// Two scopes of one library nest.  The C library counts the two loads, so the
// end of the inner scope leaves the library of the outer scope open, and a
// function of the outer scope runs after the inner scope ends.
void nested_scopes_keep_the_outer_library(::fixy::TestRunnerCtx const& test) {
    fo::LoaderDiagnostic outer_diagnostic{};
    std::uint32_t const process_id = std::to_underlying(::fixy::process::current_process(test));
    fo::with_library(test, kCLibrary, ::fixy::to(outer_diagnostic), [&test, process_id](auto const& outer) noexcept {
        fo::LoaderDiagnostic inner_diagnostic{};
        auto const outer_read = fo::find_function<ProcessIdRead>(test, outer, "getpid", ::fixy::to(inner_diagnostic))
                                    .expect("the C library has getpid");
        bool const is_inner_right =
            fo::with_library(test, kCLibrary, ::fixy::to(inner_diagnostic),
                             [&test, process_id](auto const& inner) noexcept {
                                 fo::LoaderDiagnostic lookup_diagnostic{};
                                 auto const inner_read = fo::find_function<ProcessIdRead>(test, inner, "getpid",
                                                                                          ::fixy::to(lookup_diagnostic))
                                                             .expect("the C library has getpid");
                                 return static_cast<std::uint32_t>(inner_read()) == process_id;
                             })
                .expect("the test loads the C library again");
        CRUCIBLE_TEST_ASSERT(test, is_inner_right);
        CRUCIBLE_TEST_ASSERT(test, static_cast<std::uint32_t>(outer_read()) == process_id);
    }).expect("the test loads the C library");
    ::fixy::check::pass(test, "  nested_scopes_keep_the_outer_library: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::report(::fixy::Sink::Out, "test_os_loader:\n");
    call_gives_the_result_of_the_function(test);
    missing_library_gives_the_text_of_the_loader(test);
    missing_name_gives_the_text_of_the_loader(test);
    long_text_keeps_its_first_characters(test);
    nested_scopes_keep_the_outer_library(test);
    ::fixy::check::pass(test, "test_os_loader: PASSED\n");
    return 0;
}

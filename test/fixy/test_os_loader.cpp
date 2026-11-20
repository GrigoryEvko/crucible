// The loader door of fixy/os/Loader.h (THE LOADER DOOR, THE DIAGNOSTIC).
//
// The cases load the C library, which each process holds, and call its
// getpid through the door, and the diagnostic stays empty.  A path with no
// library gives refused_open, and a name with no function gives
// refused_symbol.  Each puts the text of the loader into the diagnostic, and
// the next call that succeeds empties it.  A text longer than the diagnostic
// holds keeps its first characters.  A move takes the library, and the empty
// owner gives not_open with no lookup.  The door has no read of a moved owner,
// because the rule use-after-move refuses it.

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

[[nodiscard]] fo::Library load(::fixy::TestRunnerCtx const& test, fo::LoaderDiagnostic& diagnostic) {
    return std::move(fo::mint_library(test, kCLibrary, ::fixy::to(diagnostic)).expect("the test loads the library"))
        .consume();
}

// The failure of a load of path, or no value when the load succeeds.  The end
// of the owner of a load that succeeds closes the library.
[[nodiscard]] ::fixy::Option<fo::LoaderFailure> failure_of_load(::fixy::TestRunnerCtx const& test, ::fixy::CText path,
                                                                fo::LoaderDiagnostic& diagnostic) {
    using Failure = ::fixy::Option<fo::LoaderFailure>;
    return fo::mint_library(test, path, ::fixy::to(diagnostic))
        .match(
            [](::fixy::Linear<fo::Library> loaded) -> Failure {
                [[maybe_unused]] fo::Library const unexpected = std::move(loaded).consume();
                return ::fixy::none;
            },
            [](fo::LoaderFailure refused) { return Failure::some(refused); });
}

// The library loads, and a call of its getpid gives the id of the process.
// The two calls that succeed leave the diagnostic empty.
void call_gives_the_result_of_the_function(::fixy::TestRunnerCtx const& test) {
    fo::LoaderDiagnostic diagnostic{};
    fo::Library const library = load(test, diagnostic);
    CRUCIBLE_TEST_ASSERT(test, library.is_open() && diagnostic.text().size() == 0);
    fo::LibraryFunction<ProcessIdRead> const read_id =
        fo::find_function<ProcessIdRead>(test, library, "getpid", ::fixy::to(diagnostic))
            .expect("the C library has getpid");
    CRUCIBLE_TEST_ASSERT(test, diagnostic.text().size() == 0);
    CRUCIBLE_TEST_ASSERT(test, static_cast<std::uint32_t>(read_id())
                                   == std::to_underlying(::fixy::process::current_process(test)));
    ::fixy::check::pass(test, "  call_gives_the_result_of_the_function: PASSED\n");
}

// A path with no library gives refused_open, and the diagnostic names the
// path.  A load that then succeeds empties the diagnostic.
void missing_library_gives_the_text_of_the_loader(::fixy::TestRunnerCtx const& test) {
    fo::LoaderDiagnostic diagnostic{};
    fo::LoaderFailure const failure =
        failure_of_load(test, "libcrucible_no_such_library.so", diagnostic).value_or(fo::LoaderFailure::not_open);
    CRUCIBLE_TEST_ASSERT(test, failure == fo::LoaderFailure::refused_open);
    CRUCIBLE_TEST_ASSERT(test, ::fixy::find(diagnostic.text(), "libcrucible_no_such_library.so").is_some());
    fo::Library const library = load(test, diagnostic);
    CRUCIBLE_TEST_ASSERT(test, library.is_open() && diagnostic.text().size() == 0);
    ::fixy::check::pass(test, "  missing_library_gives_the_text_of_the_loader: PASSED\n");
}

// A name with no function gives refused_symbol, and the diagnostic names the
// name.  A lookup that then succeeds empties the diagnostic.
void missing_name_gives_the_text_of_the_loader(::fixy::TestRunnerCtx const& test) {
    fo::LoaderDiagnostic diagnostic{};
    fo::Library const library = load(test, diagnostic);
    fo::LoaderFailure const failure =
        fo::find_function<ProcessIdRead>(test, library, "crucible_no_such_function", ::fixy::to(diagnostic))
            .match([](fo::LibraryFunction<ProcessIdRead>) { return fo::LoaderFailure::not_open; },
                   [](fo::LoaderFailure refused) { return refused; });
    CRUCIBLE_TEST_ASSERT(test, failure == fo::LoaderFailure::refused_symbol);
    CRUCIBLE_TEST_ASSERT(test, ::fixy::find(diagnostic.text(), "crucible_no_such_function").is_some());
    CRUCIBLE_TEST_ASSERT(test,
                         fo::find_function<ProcessIdRead>(test, library, "getpid", ::fixy::to(diagnostic)).is_ok());
    CRUCIBLE_TEST_ASSERT(test, diagnostic.text().size() == 0);
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

// A move takes the library.  A lookup through the empty owner gives not_open.
// A move assignment of the empty owner leaves the target empty.
void move_takes_the_library(::fixy::TestRunnerCtx const& test) {
    fo::LoaderDiagnostic diagnostic{};
    fo::Library source = load(test, diagnostic);
    fo::Library target = std::move(source);
    CRUCIBLE_TEST_ASSERT(test, target.is_open());
    CRUCIBLE_TEST_ASSERT(test,
                         fo::find_function<ProcessIdRead>(test, target, "getpid", ::fixy::to(diagnostic)).is_ok());
    fo::Library const empty{};
    fo::LoaderFailure const failure =
        fo::find_function<ProcessIdRead>(test, empty, "getpid", ::fixy::to(diagnostic))
            .match([](fo::LibraryFunction<ProcessIdRead>) { return fo::LoaderFailure::refused_open; },
                   [](fo::LoaderFailure refused) { return refused; });
    CRUCIBLE_TEST_ASSERT(test, failure == fo::LoaderFailure::not_open);
    CRUCIBLE_TEST_ASSERT(test, diagnostic.text() == ::fixy::TextView{"the library is not open"});
    fo::Library other_empty{};
    target = std::move(other_empty);
    CRUCIBLE_TEST_ASSERT(test, !target.is_open());
    ::fixy::check::pass(test, "  move_takes_the_library: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    ::fixy::report(::fixy::Sink::Out, "test_os_loader:\n");
    call_gives_the_result_of_the_function(test);
    missing_library_gives_the_text_of_the_loader(test);
    missing_name_gives_the_text_of_the_loader(test);
    long_text_keeps_its_first_characters(test);
    move_takes_the_library(test);
    ::fixy::check::pass(test, "test_os_loader: PASSED\n");
    return 0;
}

// The static text of foundation/core/StaticText.h: the literal form, the
// view, the facts of the type, and an atomic cell of an Option of it, which
// holds the text in one word.  The site door is in namespace detail of the
// base, so the check file of the header reads it.

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Transfer.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/StaticText.h>
#include <foundation/core/Text.h>

#include <cstdint>
#include <type_traits>

namespace {

namespace core = ::foundation::core;
using ::foundation::core::StaticText;

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

constexpr StaticText kLiteral{"abc"};
static_assert(kLiteral.view() == "abc");
static_assert(StaticText{""}.view().size() == 0);

// One word, which no bytes make, and no pointer of the run time makes either.
static_assert(sizeof(StaticText) == sizeof(std::uintptr_t));
static_assert(!std::is_trivially_copyable_v<StaticText>);
static_assert(!std::is_constructible_v<StaticText, decltype(+"abc")>);

// The text has static storage, so the type moves to another thread with no
// borrow, and its atomic cell holds one word.
static_assert(::foundation::transfer::Sendable<StaticText> && ::foundation::transfer::SendableOwned<StaticText>);
static_assert(sizeof(core::Atomic<core::Option<StaticText>>) == sizeof(std::uintptr_t));

// The cell starts empty, and it gives back the text that it stores.
[[nodiscard]] int store_and_load_a_text() noexcept {
    core::Atomic<core::Option<StaticText>> cell{};
    if (cell.load_acquire().is_some()) return 1;
    cell.store_release(core::Option<StaticText>::some(kLiteral));
    core::Option<StaticText> const loaded = cell.load_acquire();
    return loaded.match([](StaticText const& text) noexcept { return text.view() == "abc" ? 0 : 2; },
                        [] noexcept { return 3; });
}

}  // namespace

int main() {
    if (const int rc = store_and_load_a_text(); rc != 0) return rc;
    ::fixy::check::pass(kTest, "test_static_text: OK\n");
    return 0;
}

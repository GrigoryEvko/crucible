// A class that holds a Linear by value and deletes its assignments.  A
// trivially copyable Linear made the class trivially copyable, and an
// Option that chose its form by trivial copyability alone took the plain
// form.  A move then copied the bytes, and the source and the target each
// gave the value.  The seal of Linear and the marker that the Option reads
// give the owning form: the move empties the source, and expect() of the
// source reaches the fatal exit, which no constant evaluation calls.  The
// expect() reads the source through a call that hides its state from the
// rule must-consume on purpose.  So this fixture proves the fatal path of
// constant evaluation for a state that the rule cannot know.  The fixture
// neg_core_option_expect_after_move proves the compile-time refusal of the
// same expect() when the rule knows the state.

#include <fixy/Core.h>
#include <fixy/Qtt.h>

#include <utility>

namespace {

struct HoldsLinear {
    fixy::Linear<int> value;
    constexpr explicit HoldsLinear(fixy::Linear<int>&& held) noexcept : value{std::move(held)} {}
    HoldsLinear(HoldsLinear&&) noexcept = default;
    HoldsLinear& operator=(HoldsLinear&&) = delete("the probe keeps one eligible special member");
    ~HoldsLinear() = default;
};

// Gives the same Option back, through a reference that the rule does not
// follow.
constexpr fixy::Option<HoldsLinear>& pass_option(fixy::Option<HoldsLinear>& option) noexcept { return option; }

consteval int read_twice() {
    auto source = fixy::Option<HoldsLinear>::some(HoldsLinear{fixy::mint_linear<int>(7)});
    auto target = std::move(source);
    HoldsLinear first = std::move(target).expect("the target holds the value");
    HoldsLinear second = std::move(pass_option(source)).expect("the source keeps the value");
    return first.value.peek() + second.value.peek();
}

static_assert(read_twice() == 14);

}  // namespace

int main() { return 0; }

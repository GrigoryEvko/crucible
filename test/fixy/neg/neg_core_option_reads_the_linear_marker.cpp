// A class that holds a Linear by value and deletes its assignments.  A
// trivially copyable Linear made the class trivially copyable, and an
// Option that chose its form by trivial copyability alone took the plain
// form.  A move then copied the bytes, and the source and the target each
// gave the value.  The seal of Linear and the marker that the Option reads
// give the owning form: the move empties the source, and expect() of the
// source reaches the fatal exit, which no constant evaluation calls.

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

consteval int read_twice() {
    auto source = fixy::Option<HoldsLinear>::some(HoldsLinear{fixy::mint_linear<int>(7)});
    auto target = std::move(source);
    HoldsLinear first = std::move(target).expect("the target holds the value");
    HoldsLinear second = std::move(source).expect("the source keeps the value");
    return first.value.peek() + second.value.peek();
}

static_assert(read_twice() == 14);

}  // namespace

int main() { return 0; }

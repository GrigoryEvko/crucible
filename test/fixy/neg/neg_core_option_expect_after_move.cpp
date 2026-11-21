// The expect() of an Option that a move emptied.  The move of an Option of a
// linear payload consumes the source, so the rule must-consume knows that
// the source is empty, and it refuses the expect() at compile time with the
// kind option-known-empty.  The fixture neg_core_option_reads_the_linear_marker
// proves the fatal path of the same expect() for a state that the rule
// cannot know.

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

int read_twice() {
    auto source = fixy::Option<HoldsLinear>::some(HoldsLinear{fixy::mint_linear<int>(7)});
    auto target = std::move(source);
    HoldsLinear first = std::move(target).expect("the target holds the value");
    HoldsLinear second = std::move(source).expect("the source keeps the value");
    return std::move(first.value).consume() + std::move(second.value).consume();
}

}  // namespace

int main() { return read_twice(); }

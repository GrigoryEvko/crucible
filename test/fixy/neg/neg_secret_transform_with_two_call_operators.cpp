// A callable of transform() whose parameter reflection cannot read.
//
// The class has two call operators, so no one parameter says whether the
// call copies the payload.  The rule TransformTakesPayloadByReference then
// gives false, so it fails closed, and the deleted overload of transform()
// refuses the call.

#include <fixy/Secret.h>

#include <utility>

namespace {

struct TwoCalls {
    int operator()(int&& value) const noexcept { return value; }
    int operator()(int const& value) const noexcept { return value; }
};

}  // namespace

int main() {
    auto key = ::fixy::mint_secret<int>(1);
    [[maybe_unused]] auto derived = std::move(key).transform(TwoCalls{});
    return 0;
}

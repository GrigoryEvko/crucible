// A callable of transform() that takes the payload by reference to a
// different type.
//
// The reference to long binds to a temporary that a conversion builds from
// the int payload.  That temporary is a copy of the payload on the stack, and
// nothing zeroizes it.  The rule TransformTakesPayloadByReference asks that
// the reference name the payload type itself, so the deleted overload of
// transform() refuses the call.

#include <fixy/Secret.h>

#include <utility>

int main() {
    auto key = ::fixy::mint_secret<int>(1);
    [[maybe_unused]] auto derived = std::move(key).transform([](long const& value) noexcept { return value + 1; });
    return 0;
}

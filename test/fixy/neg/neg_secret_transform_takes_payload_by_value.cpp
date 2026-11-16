// A callable of transform() that takes the payload by value.
//
// The parameter is a copy of the payload on the stack, and nothing zeroizes
// it at the end of the call.  The constraint TransformTakesPayloadByReference
// reads the parameter by reflection and refuses the copy, and the deleted
// overload gives the reason.  The sibling fixture
// neg_secret_transform_takes_payload_by_value_generic covers a generic
// callable whose parameter is a value.

#include <fixy/Secret.h>

#include <utility>

int main() {
    auto key = ::fixy::mint_secret<int>(1);
    [[maybe_unused]] auto derived = std::move(key).transform([](int value) noexcept { return value + 1; });
    return 0;
}

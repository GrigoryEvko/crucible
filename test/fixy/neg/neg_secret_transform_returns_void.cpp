// The other callable shape transform() refuses: one that returns
// nothing.
//
// There is no value to rewrap, so the only work such a callable can do
// is an observation of the classified payload as a side effect, and an
// observation belongs behind declassify<Policy>() where it leaves a
// policy tag.  The constraint TransformReturnsNonVoid refuses the call
// by name.  The sibling fixture neg_secret_transform_returns_reference
// covers the reference return, the other route around the policy tag.

#include <fixy/Secret.h>

#include <utility>

namespace {

int observed = 0;

}  // namespace

int main() {
    auto key = ::fixy::mint_secret<int>(1);
    [[maybe_unused]] auto derived = std::move(key).transform([](int&& value) { observed = value; });
    return observed;
}

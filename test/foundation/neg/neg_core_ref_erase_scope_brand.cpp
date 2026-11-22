// Negative-compile test. This file must fail to compile.
//
// A Ref of a branded scope names the brand of the scope, and the rule
// brand-escape refuses each value of that brand that outlives the scope.  A
// Ref on the erased brand names no brand that the rule could read, so the
// erase door refuses a scope brand: the Ref would leave the scope unseen.

#include <foundation/core/Array.h>
#include <foundation/core/Branded.h>
#include <foundation/core/Ref.h>

int main() {
    ::foundation::core::FixedArray<int, 2> storage{1, 2};
    return storage.with_mut([](auto numbers) noexcept -> int {
        return ::foundation::core::mint_branded_scope(numbers.view(), [](auto scope) noexcept {
            int sum = 0;
            for (auto [index, element] : scope.enumerate()) {
                auto const escaped = ::foundation::core::erase_brand(element);
                sum += *escaped + static_cast<int>(index.raw());
            }
            return sum;
        });
    });
}

// A scan does not throw, so a predicate that can throw is refused.

#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>

inline bool has_large(::foundation::core::FixedArray<int, 4> const& numbers) {
    return ::foundation::core::find_if(numbers.view(), [](int value) { return value > 3; }).is_some();
}

int main() {}

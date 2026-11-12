// A floating key has no total order, because a NaN is in no order.  So the
// lower bound refuses it.

#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>

#include <cstddef>

inline std::size_t bound_of(::foundation::core::FixedArray<float, 4> const& values) {
    return ::foundation::core::lower_bound_by_key(values.view(), [](float value) noexcept { return value; }, 1.0f);
}

int main() {}

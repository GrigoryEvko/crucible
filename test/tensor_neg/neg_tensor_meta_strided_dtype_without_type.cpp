// An element type with no dtype of its own gives no strided view of a
// tensor.  A struct of two floats has the bits of ComplexFloat, but no rule
// maps it to that dtype, so the constraint of strided_view_of refuses it.

#include <crucible/TensorMeta.h>

#include <utility>

namespace {

struct FloatPair {
    float real = 0.0f;
    float imaginary = 0.0f;
};

}  // namespace

int main() {
    auto [values, values_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<FloatPair, 4>);
    ::crucible::TensorMeta const meta{};
    return values.with_mut(values_key, [&meta](auto run) noexcept -> int {
        return static_cast<int>(::crucible::strided_view_of(meta, std::move(run)).is_ok());
    });
}

// An element type with no dtype of its own gives no strided view of a
// tensor.  A struct of two floats has the bits of ComplexFloat, but no rule
// maps it to that dtype, so the constraint of strided_view_of refuses it.

#include <crucible/TensorMeta.h>

namespace {

struct FloatPair {
    float real = 0.0f;
    float imaginary = 0.0f;
};

}  // namespace

int main() {
    ::fixy::FixedArray<FloatPair, 4> values{};
    ::crucible::TensorMeta const meta{};
    return static_cast<int>(::crucible::strided_view_of(meta, ::fixy::View<FloatPair>{values.view()}).is_ok());
}

// A bool is no element of a strided view of a tensor.  A byte of the memory
// plan can hold a value other than 0 and 1, and a read of such a bool is
// undefined behavior, so the constraint of strided_view_of refuses it.

#include <crucible/TensorMeta.h>

int main() {
    ::fixy::FixedArray<bool, 4> values{};
    ::crucible::TensorMeta const meta{};
    return static_cast<int>(::crucible::strided_view_of(meta, ::fixy::View<bool>{values.view()}).is_ok());
}

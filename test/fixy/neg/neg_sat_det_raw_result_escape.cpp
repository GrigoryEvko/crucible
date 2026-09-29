// *_sat_det returns DetSafe<Pure, Saturated<T>>.  The DetSafe band must
// not silently decay to the underlying Saturated<T>; consumers that
// drop the determinism pin have to do so explicitly.

#include <fixy/Saturate.h>
#include <fixy/Saturated.h>

int main() {
    fixy::Saturated<unsigned> escaped = fixy::sat::add_sat_det<unsigned>(1u, 2u);
    return static_cast<int>(escaped.value());
}

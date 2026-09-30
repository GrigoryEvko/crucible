// The second translation unit of test_stable_id_function_local.  Its
// class UnitLocal has the printed name of the class in the first unit,
// and holds a different number.

#include "stable_id_function_local.h"

namespace stable_id_function_local {

namespace {

struct UnitLocal {
    static constexpr int unit_number = 2;
};

}  // namespace

UnitView view_of_second_unit() noexcept { return view_of<UnitLocal>(); }

}  // namespace stable_id_function_local

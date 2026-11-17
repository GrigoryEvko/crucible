// An append to a SmallList takes the allocation capability first, so the
// growth past the inline slots shows at its call.  A different capability tag
// fails the gate.

#include <foundation/core/SmallList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    auto list = ::foundation::core::mint_small_list<int, 4>(
        ::foundation::core::Quantity<::foundation::core::unit::Element, std::size_t>{64});
    list.append(::foundation::effects::testing::test().io, 1).expect("the list is below its maximum");
    return static_cast<int>(list.size());
}

// An alignment is a power of two, so the alignment query refuses 24.

#include <foundation/core/Ref.h>

int main() {
    int number = 1;
    return ::foundation::core::address_of(::foundation::core::Ref<int>::to(number)).is_aligned<24>() ? 0 : 1;
}

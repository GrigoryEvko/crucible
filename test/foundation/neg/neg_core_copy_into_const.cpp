// copy() writes the target.  A View of const elements gives no write
// access, so the copy gate refuses it as a target.

#include <foundation/core/Region.h>

inline void copy_into_read_only(::foundation::core::View<int const> target,
                                ::foundation::core::View<int const> source) {
    ::foundation::core::copy(target, source);
}

int main() {}

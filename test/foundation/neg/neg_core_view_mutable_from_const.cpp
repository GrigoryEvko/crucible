// A View of const elements gives read access only.  No conversion drops
// the const, so a View that writes cannot come from one that reads.

#include <foundation/core/Region.h>

inline void write_through(::foundation::core::View<int const> source) {
    ::foundation::core::View<int> target = source;
    static_cast<void>(target);
}

int main() {}

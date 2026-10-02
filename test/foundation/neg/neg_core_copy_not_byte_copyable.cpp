// copy() is a byte copy.  An element whose copy is not a byte copy would
// skip its own copy constructor, so the copy gate refuses it.

#include <foundation/core/Region.h>

struct Counted {
    int value = 0;
    Counted() = default;
    Counted(Counted const& other) : value{other.value + 1} {}
    Counted& operator=(Counted const& other) {
        value = other.value + 1;
        return *this;
    }
};

inline void copy_counted(::foundation::core::View<Counted> target, ::foundation::core::View<Counted const> source) {
    ::foundation::core::copy(target, source);
}

int main() {}

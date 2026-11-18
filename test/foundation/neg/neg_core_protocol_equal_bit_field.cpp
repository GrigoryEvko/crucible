// The walk of the Protocol family refuses a named bit-field, which has no
// byte offset.  So a class with one has no equal of the family.

#include <foundation/core/Equatable.h>

struct[[= ::foundation::core::derive_protocol{}]] HoldsBitField {
    unsigned flag : 1 = 0;
};

int main() {
    HoldsBitField const value{};
    return ::foundation::core::equal(value, value) ? 0 : 1;
}

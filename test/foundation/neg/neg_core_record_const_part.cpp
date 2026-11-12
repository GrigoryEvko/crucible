// A const part would block the move of the Record, so the part gate refuses
// a const type.

#include <foundation/core/Record.h>

int main() { return static_cast<int>(sizeof(::foundation::core::Record<int const, int>)); }

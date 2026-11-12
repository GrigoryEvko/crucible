// An index below 8 is not proven below 4, so an Index<8> does not convert to
// an Index<4>.

#include <foundation/core/Index.h>

inline ::foundation::core::Index<4> narrow(::foundation::core::Index<8> wide) { return wide; }

int main() {}

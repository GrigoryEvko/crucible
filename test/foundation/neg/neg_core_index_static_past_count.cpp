// The constant index 4 is not below 4, so it converts to no Index<4>.

#include <foundation/core/Index.h>

inline ::foundation::core::Index<4> past_the_end() { return ::foundation::core::index<4>(); }

int main() {}

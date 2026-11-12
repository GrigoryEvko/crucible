// A volatile object is not memory that the base reads in order, so the
// target gate refuses a volatile type.

#include <foundation/core/Ref.h>

int main() { return static_cast<int>(sizeof(::foundation::core::Ref<int volatile>)); }

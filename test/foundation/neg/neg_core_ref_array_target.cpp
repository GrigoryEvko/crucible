// A Ref reaches one object.  A run of objects is a View of the Region
// family, so the target gate refuses an array type.

#include <foundation/core/Ref.h>

int main() { return static_cast<int>(sizeof(::foundation::core::Ref<int[2]>)); }

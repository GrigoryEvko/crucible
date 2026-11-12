// The Pack operations take a Pack only.  A Record is a product of values and
// no list of types, so pack_size_v refuses it.

#include <foundation/core/Record.h>

int main() { return static_cast<int>(::foundation::core::pack_size_v<::foundation::core::Record<int, long>>); }

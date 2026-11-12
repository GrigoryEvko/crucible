// at() of a constant index that is not below the count of parts does not
// compile.

#include <foundation/core/Index.h>
#include <foundation/core/Record.h>

int main() {
    ::foundation::core::Record<int, long> pair{1, 2};
    return static_cast<int>(pair.at(::foundation::core::index<2>()));
}

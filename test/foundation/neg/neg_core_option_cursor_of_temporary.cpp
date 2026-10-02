// A cursor of a temporary Option would point into an object that the full
// expression ends.  The rvalue overload of begin() is deleted.

#include <foundation/core/Choice.h>

int main() {
    auto cursor = ::foundation::core::Option<int>::some(1).begin();
    return *cursor;
}

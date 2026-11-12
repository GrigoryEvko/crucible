// ok_or gives a Result, so its error passes the error gate.  A plain
// integer holds no meaning in its type, and the gate refuses it.

#include <foundation/core/Choice.h>

#include <utility>

int main() {
    auto option = ::foundation::core::Option<int>::some(4);
    return std::move(option).ok_or(5).is_ok() ? 0 : 1;
}

// ok_or of a full Option gives the payload and drops the error.  A dropped
// linear value ends with no door, so ok_or refuses a linear error.

#include <fixy/Core.h>
#include <fixy/Qtt.h>

#include <utility>

int main() {
    auto option = fixy::Option<int>::some(4);
    return std::move(option).ok_or(fixy::mint_linear<int>(0)).is_ok() ? 0 : 1;
}

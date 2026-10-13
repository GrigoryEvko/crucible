// value_or of a full Option gives the payload and drops the fallback.  A
// dropped linear value ends with no door, so an Option of a linear payload
// has no value_or.

#include <fixy/Core.h>
#include <fixy/Qtt.h>

#include <utility>

int main() {
    auto option = fixy::Option<fixy::Linear<int>>::some(fixy::mint_linear<int>(7));
    static_cast<void>(std::move(option).value_or(fixy::mint_linear<int>(0)));
    return 0;
}

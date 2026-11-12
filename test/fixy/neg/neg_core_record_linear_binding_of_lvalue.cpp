// A binding by value of an lvalue Record copies the Record into the hidden
// variable of the binding.  A Record with a linear part does not copy, so the
// binding takes std::move(record).

#include <fixy/Core.h>
#include <fixy/Qtt.h>

#include <utility>

int main() {
    fixy::Record<fixy::Linear<int>> owned{fixy::mint_linear<int>(1)};
    auto [value] = owned;
    return std::move(value).consume();
}

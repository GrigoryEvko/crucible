// A Record with a linear part is linear, so a copy of it does not compile.
// Each copy would hold the same linear value.

#include <fixy/Core.h>
#include <fixy/Qtt.h>

#include <utility>

int main() {
    fixy::Record<fixy::Linear<int>, int> owned{fixy::mint_linear<int>(1), 2};
    fixy::Record<fixy::Linear<int>, int> copied = owned;
    auto [first, second] = std::move(copied);
    return std::move(first).consume() + second;
}

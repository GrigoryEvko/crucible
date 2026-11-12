// A structured binding names each part of a Record, so two names for three
// parts do not compile.

#include <foundation/core/Record.h>

int main() {
    ::foundation::core::Record<int, int, int> const triple{1, 2, 3};
    auto const& [first, second] = triple;
    return first + second;
}

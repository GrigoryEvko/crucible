// A container subscript takes its index as std::size_t, so a secret index
// reaches the member operator[] through the integral conversion, and
// Secret deletes that conversion with the reason.

#include <fixy/Secret.h>

#include <array>
#include <cstddef>

int main() {
    std::array<int, 4> table{1, 2, 3, 4};
    auto secret = fixy::mint_secret<std::size_t>(1u);
    return table[secret];
}

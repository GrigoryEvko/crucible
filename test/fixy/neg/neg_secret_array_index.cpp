// A load from a table at a secret index puts the secret in the address,
// and the cache lines that the load touches then tell the secret.  The
// built-in subscript converts the index to std::ptrdiff_t, and Secret
// deletes that conversion with the reason.

#include <fixy/Secret.h>

#include <cstddef>

int main() {
    static int const table[4] = {1, 2, 3, 4};
    auto secret = fixy::mint_secret<std::size_t>(2u);
    return table[secret];
}

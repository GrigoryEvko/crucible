// A pointer offset by a secret is the same leak as a secret index: the
// address depends on the secret.  The built-in pointer sum converts the
// offset to std::ptrdiff_t, and Secret deletes that conversion.

#include <fixy/Secret.h>

int main() {
    static int const table[4] = {1, 2, 3, 4};
    auto secret = fixy::mint_secret<int>(2);
    int const* cell = table + secret;
    return *cell;
}

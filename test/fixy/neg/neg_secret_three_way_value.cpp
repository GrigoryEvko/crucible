// The three-way compare against a plain value refuses as the relational
// operators do.  The ordering it would return depends on the secret.

#include <fixy/Secret.h>

#include <compare>

int main() {
    auto secret = fixy::mint_secret<int>(4);
    return (secret <=> 4) == 0 ? 1 : 0;
}

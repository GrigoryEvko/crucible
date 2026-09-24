// The rewritten != and a plain value on the left reach the same deleted
// operator== as a compare of two secrets.  A public value does not make
// the compare safe: the result still depends on the secret.

#include <fixy/Secret.h>

int main() {
    auto secret = fixy::mint_secret<int>(7);
    return 7 != secret ? 1 : 0;
}

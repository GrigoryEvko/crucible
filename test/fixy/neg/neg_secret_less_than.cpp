// An order compare of classified values is a branch in most code that
// uses it.  Secret deletes operator<=>, and the relational operators
// rewrite to it, so `<` refuses with the reason of the order.

#include <fixy/Secret.h>

int main() {
    auto left = fixy::mint_secret<unsigned>(1u);
    auto right = fixy::mint_secret<unsigned>(2u);
    return left < right ? 1 : 0;
}

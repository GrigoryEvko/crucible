// An equality compare of two classified values stops at the first
// difference in most implementations, so its time tells how many bytes
// agree.  Secret deletes operator== for every second operand.

#include <fixy/Secret.h>

int main() {
    auto left = fixy::mint_secret<int>(1);
    auto right = fixy::mint_secret<int>(1);
    return left == right ? 1 : 0;
}

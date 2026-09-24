// The logical not converts its operand to bool, as an if statement does.
// A secret under ! would feed a branch through the result, so Secret
// refuses the conversion with the same reason.

#include <fixy/Secret.h>

int main() {
    auto secret = fixy::mint_secret<int>(3);
    bool const is_clear = !secret;
    return is_clear ? 1 : 0;
}

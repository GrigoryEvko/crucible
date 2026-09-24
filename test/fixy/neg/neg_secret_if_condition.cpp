// A branch on a classified value makes the control flow depend on the
// secret, and the timing of the program then tells the secret.  Secret
// deletes operator bool, so an if statement over a Secret refuses with
// the reason.

#include <fixy/Secret.h>

int main() {
    auto secret = fixy::mint_secret<bool>(true);
    if (secret) return 1;
    return 0;
}

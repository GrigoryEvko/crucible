// std::bit_cast builds a value of a trivially copyable type from bytes, and
// no constructor runs.  GCC reports a class whose copy and move are all
// deleted as trivially copyable, so the trait alone does not refuse this
// route.  The deleted move does: bit_cast returns its result by value, and
// a witness cannot be moved.  This fixture keeps the move deleted.

#include <fixy/os/ThreadName.h>

#include <bit>

int main() {
    auto forged = std::bit_cast<fixy::ThreadNamed<"forged">>(static_cast<unsigned char>(0));
    (void)forged;
    return 0;
}

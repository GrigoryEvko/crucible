// std::bit_cast builds a value of a trivially copyable type from bytes, and
// no constructor runs.  GCC reports a class whose copy and move are all
// deleted as trivially copyable.  The seal member of the witness makes it
// not trivially copyable, so bit_cast refuses it at its constraint.

#include <fixy/os/ThreadName.h>

#include <bit>

int main() {
    auto forged = std::bit_cast<fixy::ThreadNamed<"forged">>(static_cast<unsigned char>(0));
    (void)forged;
    return 0;
}

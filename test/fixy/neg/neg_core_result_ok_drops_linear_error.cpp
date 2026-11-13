// ok() gives the value of a Result as an Option, and it drops the error.  A
// linear error must not drop, so a Result with a linear error has no ok().
// The rvalue match gives the value and the error.

#include <fixy/Core.h>
#include <fixy/Qtt.h>

int main() {
    fixy::Result<int, fixy::Linear<int>> result = fixy::err(fixy::mint_linear<int>(3));
    static_cast<void>(static_cast<fixy::Result<int, fixy::Linear<int>>&&>(result).ok());
    return 0;
}

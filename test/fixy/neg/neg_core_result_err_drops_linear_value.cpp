// The rvalue err() gives the linear error of a Result as an Option, and it
// drops the value when the Result holds one.  A linear value must not drop,
// so a Result of a linear value has no err() of a linear error.  The rvalue
// match gives the value and the error.

#include <fixy/Core.h>
#include <fixy/Qtt.h>

int main() {
    fixy::Result<fixy::Linear<int>, fixy::Linear<long>> result = fixy::mint_linear<int>(3);
    static_cast<void>(static_cast<fixy::Result<fixy::Linear<int>, fixy::Linear<long>>&&>(result).err());
    return 0;
}

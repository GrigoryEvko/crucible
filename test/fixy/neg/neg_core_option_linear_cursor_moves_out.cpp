// A loop over an Option gives a reference to its payload.  A move through
// that reference would empty a linear payload in place, and the Option would
// stay full of a moved-from value that its destructor then drops.  An Option
// of a linear payload takes the owning form, and an owning Option has no loop,
// so the loop itself does not compile.

#include <fixy/Core.h>
#include <fixy/Qtt.h>

#include <utility>

int main() {
    auto option = fixy::Option<fixy::Linear<int>>::some(fixy::mint_linear<int>(7));
    for (auto& value : option)
        ::foundation::linear::drop(std::move(value));
    return 0;
}

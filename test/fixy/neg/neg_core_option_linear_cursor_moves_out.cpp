// A loop over a mutable Option gives a reference to its payload.  A move
// through that reference empties a linear payload in place, and the Option
// stays full of a moved-from value that its destructor then drops.  An
// Option of a linear payload has only the const cursor, so the move is a
// copy, and the copy of a linear value is deleted.

#include <fixy/Core.h>
#include <fixy/Qtt.h>

#include <utility>

int main() {
    auto option = fixy::Option<fixy::Linear<int>>::some(fixy::mint_linear<int>(7));
    for (auto& value : option) {
        auto taken = std::move(value);
        drop(std::move(taken));
    }
    return 0;
}

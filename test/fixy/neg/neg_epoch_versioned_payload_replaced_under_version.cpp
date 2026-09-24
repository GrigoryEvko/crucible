// Writing a new payload through a mutable reference keeps the current
// version.  An older payload written into a value at a newer version then
// reads as fresh.  There is no mutable accessor.  A new payload gets a
// new EpochVersioned, with the version it was produced at.

#include <fixy/EpochVersioned.h>

int main() {
    fixy::EpochVersioned<int> current = fixy::EpochVersioned<int>::at_genesis(20);
    current.peek_mut() = 10;
    return current.peek();
}

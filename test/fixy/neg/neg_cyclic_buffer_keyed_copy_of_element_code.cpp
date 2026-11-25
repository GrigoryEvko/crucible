// A copy of a keyed CyclicBuffer whose element has a copy of its own.  The copy
// takes no key, so code of an element that a fresh claim ends could copy the
// ring, and the copy would read the ended element.

#include <fixy/CyclicBuffer.h>

struct Counted {
    int value = 0;
    Counted() noexcept = default;
    Counted(Counted const& other) noexcept : value{other.value} {}
    Counted& operator=(Counted const&) noexcept = default;
};

int main() {
    auto [ring, key] = ::fixy::mint_keyed_owner(::fixy::cyclic_buffer_owner<Counted, 2>);
    auto const copied = ring;
    static_cast<void>(copied.size());
    static_cast<void>(key);
    return 0;
}

// A push_new of an element that can throw at a move.  The door ends the old
// element of the slot before the new one starts, so a throw between the two
// would leave the slot with no element.

#include <fixy/CyclicBuffer.h>

#include <utility>

struct Step {
    int value = 0;

    Step() noexcept = default;
    Step(Step const& other) noexcept = default;
    Step(Step&& other) noexcept(false) : value{other.value} {}
    Step& operator=(Step const&) noexcept = default;
    Step& operator=(Step&&) noexcept = default;
    ~Step() = default;
};

int main() {
    ::fixy::CyclicBuffer<Step, 2> ring{};
    Step step{};
    ring.push_new(std::move(step));
    return 0;
}

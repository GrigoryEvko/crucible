// The reflection escape guard reads the public surface of a type, and a
// public base is part of it.  The door below is declared in the base,
// and the checked type declares nothing, so a walk over the members that
// the type declares itself sees no door.

#include <foundation/reflect/RawEscape.h>

namespace {

class LeakyBase {
public:
    [[nodiscard]] int* steal_the_pointer() noexcept { return &value_; }

private:
    int value_ = 0;
};

class InheritsTheDoor : public LeakyBase {};

}  // namespace

CRUCIBLE_NO_RAW_ESCAPE(InheritsTheDoor);

int main() { return 0; }

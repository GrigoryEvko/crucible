// The reflection escape guard reads the public surface of a type, and a
// using-declaration puts a member of a private base on it.  Reflection
// lists no member for the using-declaration, so the guard asks the
// compiler whether a caller can name the member of the base through the
// checked type.

#include <foundation/reflect/RawEscape.h>

namespace {

class LeakyBase {
public:
    [[nodiscard]] int* steal_the_pointer() noexcept { return &value_; }

private:
    int value_ = 0;
};

class ReExportsTheDoor : private LeakyBase {
public:
    using LeakyBase::steal_the_pointer;
};

}  // namespace

CRUCIBLE_NO_RAW_ESCAPE(ReExportsTheDoor);

int main() { return 0; }

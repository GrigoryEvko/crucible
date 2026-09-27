// The reflection escape guard refuses a conversion function to a raw
// pointer.  The conversion gives the resource to each caller that casts
// the object, and no name marks the door.

#include <foundation/reflect/RawEscape.h>

namespace {

class ConvertsToPointer {
public:
    [[nodiscard]] explicit operator int*() noexcept { return &value_; }

private:
    int value_ = 0;
};

}  // namespace

CRUCIBLE_NO_RAW_ESCAPE(ConvertsToPointer);

int main() { return 0; }

// The reflection escape guard refuses a public non-static data member of
// any type.  A caller takes the address of the member, so the member is
// a raw escape of the storage of the object, with no door to name.

#include <foundation/reflect/RawEscape.h>

namespace {

class ExposesStorage {
public:
    [[nodiscard]] int const& value() const noexcept { return count; }

    int count = 0;
};

}  // namespace

CRUCIBLE_NO_RAW_ESCAPE(ExposesStorage);

int main() { return 0; }

// The reflection escape guard fires on a member that hands out a raw
// pointer under a name that is neither an accessor, a hatch, nor a mint.
//
// The offending member is spelled with a trailing return type, the
// shape a source-text scan is most likely to miss.  The compiler
// resolves the return type regardless, so CRUCIBLE_NO_RAW_ESCAPE sees
// it: the check reads what the compiler computed, not what a regex
// matched, so a member added later cannot evade it by its spelling.

#include <foundation/reflect/RawEscape.h>

namespace {

struct Leaky {
    int value_ = 0;

    // A getter by a sanctioned name, to prove the guard admits the safe
    // door beside the unsafe one rather than firing on everything.
    [[nodiscard]] int& get() noexcept { return value_; }

    // The unsafe door: a raw pointer out under an unsanctioned name,
    // spelled with a trailing return so a text scan is most likely to
    // miss it.
    [[nodiscard]] auto steal_the_pointer() noexcept -> int* { return &value_; }
};

}  // namespace

CRUCIBLE_NO_RAW_ESCAPE(Leaky);

int main() { return 0; }

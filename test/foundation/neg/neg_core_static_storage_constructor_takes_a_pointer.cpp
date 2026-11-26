// A class of the base states refers_static{}, the mark of a class whose
// pointers name static storage (foundation/Transfer.h), but its constexpr
// constructor takes a pointer of the run time.  The read of the mark checks
// each constructor of the class, so the first walk that reads the class stops
// the build and names the constructor.

#include <foundation/Quarantine.h>
#include <foundation/core/CaptureStore.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace foundation::core::static_storage_probe {
class[[= ::foundation::transfer::refers_static{}]] LooseText {
public:
    constexpr explicit LooseText(char const* first) noexcept : first_{first} {}

private:
    char const* first_ = "";
};
}  // namespace foundation::core::static_storage_probe
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

struct CapturesLooseText {
    ::foundation::core::static_storage_probe::LooseText& text;
};
static_assert(::foundation::core::StoresNoBorrowOutside<CapturesLooseText>);

int main() { return 0; }

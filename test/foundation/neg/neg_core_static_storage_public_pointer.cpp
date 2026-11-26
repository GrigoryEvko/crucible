// A class of the base states refers_static{} (foundation/Transfer.h), and its
// one constructor is consteval, but its pointer is a public member.  Each
// caller can then write a pointer of the run time into it.  The read of the
// mark checks each part of the class, so the first walk that reads the class
// stops the build and names the member.

#include <foundation/Quarantine.h>
#include <foundation/core/CaptureStore.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace foundation::core::static_storage_probe {
class[[= ::foundation::transfer::refers_static{}]] PublicText {
public:
    consteval PublicText() noexcept {}
    char const* first = "";
};
}  // namespace foundation::core::static_storage_probe
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

struct CapturesPublicText {
    ::foundation::core::static_storage_probe::PublicText& text;
};
static_assert(::foundation::core::StoresNoBorrowOutside<CapturesPublicText>);

int main() { return 0; }

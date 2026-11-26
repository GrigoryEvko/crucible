// A class of the base states refers_static{} (foundation/Transfer.h), and its
// one constructor is consteval, but a member function that is not const keeps
// a pointer that a caller gives it.  That pointer can come from the run time.
// The read of the mark checks each member function that can write the class,
// so the first walk that reads the class stops the build and names the door.

#include <foundation/Quarantine.h>
#include <foundation/core/CaptureStore.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace foundation::core::static_storage_probe {
class[[= ::foundation::transfer::refers_static{}]] KeepingText {
public:
    consteval KeepingText() noexcept {}
    constexpr void keep(char const* text) noexcept { first_ = text; }

private:
    char const* first_ = "";
};
}  // namespace foundation::core::static_storage_probe
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

struct CapturesKeepingText {
    ::foundation::core::static_storage_probe::KeepingText& text;
};
static_assert(::foundation::core::StoresNoBorrowOutside<CapturesKeepingText>);

int main() { return 0; }

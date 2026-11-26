// A class of the base states refers_static{} (foundation/Transfer.h), and its
// constructor takes a class whose own constructor is constexpr and takes a
// number.  That class can carry a value of the run time, so it fails the rule
// of the constructors of static storage, and so does the constructor that
// takes it.  The first walk that reads the marked class stops the build and
// names the constructor.

#include <foundation/Quarantine.h>
#include <foundation/core/CaptureStore.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace foundation::core::static_storage_probe {
class LooseSite {
public:
    constexpr explicit LooseSite(int line) noexcept : line_{line} {}
    [[nodiscard]] constexpr int line() const noexcept { return line_; }

private:
    int line_ = 0;
};
class[[= ::foundation::transfer::refers_static{}]] TextOfALooseSite {
public:
    constexpr explicit TextOfALooseSite(LooseSite const& site) noexcept : line_{site.line()} {}

private:
    int line_ = 0;
};
}  // namespace foundation::core::static_storage_probe
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

struct CapturesTextOfALooseSite {
    ::foundation::core::static_storage_probe::TextOfALooseSite& text;
};
static_assert(::foundation::core::StoresNoBorrowOutside<CapturesTextOfALooseSite>);

int main() { return 0; }

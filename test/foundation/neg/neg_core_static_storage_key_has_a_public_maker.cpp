// A class of the base states refers_static{} (foundation/Transfer.h), and its
// constructor takes a pointer beside a key of the class.  But the key has a
// public constructor, so each caller builds one, and the key ties nothing.
// The pointer then can come from the run time, so the first walk that reads
// the marked class stops the build and names the constructor.

#include <foundation/Quarantine.h>
#include <foundation/core/CaptureStore.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace foundation::core::static_storage_probe {
template <class Owner>
class OpenTextKey {
public:
    constexpr OpenTextKey() noexcept {}
};
class[[= ::foundation::transfer::refers_static{}]] KeyedText {
public:
    constexpr KeyedText(char const* first, OpenTextKey<KeyedText> const& /*key*/) noexcept : first_{first} {}

private:
    char const* first_ = "";
};
}  // namespace foundation::core::static_storage_probe
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

struct CapturesKeyedText {
    ::foundation::core::static_storage_probe::KeyedText& text;
};
static_assert(::foundation::core::StoresNoBorrowOutside<CapturesKeyedText>);

int main() { return 0; }

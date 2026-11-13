// The route that a declaration of niche_key in Text.h gave: a unit that
// included only Text.h defined niche_key with a public constructor.  With that
// key, niche<TextView>::empty gave the empty value of a TextView, which no
// constructor of a TextView makes.  The rules of the base refuse such a
// definition, and this probe writes it in an opt-out region.  Text.h includes
// Choice.h, which defines niche_key, so the definition is refused.

#include <foundation/Quarantine.h>
#include <foundation/core/Text.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a forged niche key shows that Text.h declares no niche key of its own")
template <class T>
class foundation::core::niche_key {
public:
    constexpr niche_key() noexcept {}
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return sizeof(foundation::core::niche_key<foundation::core::TextView>) == 1 ? 0 : 1; }

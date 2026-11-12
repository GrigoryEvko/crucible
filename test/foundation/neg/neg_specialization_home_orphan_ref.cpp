// A specialization of the niche protocol for a base type, in a file outside
// the base, forges the empty value of that type.  The storage of an Option
// gives each niche the key of its type, and the key opens the hidden friend
// niche_null_ of Ref through argument-dependent lookup.  So this explicit
// specialization builds a Ref that holds the null pointer, which no Ref
// holds, and the gate of the niche admits it.  The rule specialization-home
// permits a specialization of an extension point outside its home only in
// the file that defines its arguments, so the plugin refuses it.

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

struct Evil {};
struct EvilBrand {};

using EvilRef = foundation::core::Ref<Evil, EvilBrand>;

template <>
struct foundation::core::niche<EvilRef> {
    [[nodiscard]] static constexpr EvilRef empty(niche_key<EvilRef> const& key) noexcept { return niche_null_(key); }
    [[nodiscard]] static constexpr bool is_empty(niche_key<EvilRef> const& key, EvilRef const& ref) noexcept {
        return niche_is_null_(key, ref);
    }
};

static_assert(foundation::core::WellFormedNiche<EvilRef>);

int main() { return 0; }

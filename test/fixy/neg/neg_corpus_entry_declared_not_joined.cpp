// A corpus entry declared in fixy::corpus and left out of Entries.
//
// The walk consults only the entries that the Entries tuple names, so an
// entry that is declared and not joined matches nothing and refuses
// nothing.  The check reads the declarations of the namespace from the
// vantage point of this translation unit, and it names the entry.

#include <fixy/Corpus.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::corpus {

struct forgotten_entry final : ::foundation::diag::tag_base {
    template <class Type, class... Atoms>
    [[nodiscard]] static consteval bool matches() noexcept {
        return true;
    }
};

}  // namespace fixy::corpus
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace fixture_site {
struct here final {};
}  // namespace fixture_site

[[maybe_unused]] constexpr bool joined = ::fixy::corpus::corpus_is_joined<fixture_site::here>();

int main() { return 0; }

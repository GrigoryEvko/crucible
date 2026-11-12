// A corpus entry declared as a class template in fixy::corpus.
//
// The walk cannot tell whether a specialization of a class template is an
// entry without an instantiation.  So an entry is a class, and the walk
// names each class template under fixy::corpus outside
// fixy::corpus::detail.

#include <fixy/Corpus.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a corpus entry template of the base")
namespace fixy::corpus {

template <int Variant>
struct generated_entry final : ::foundation::diag::tag_base {
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

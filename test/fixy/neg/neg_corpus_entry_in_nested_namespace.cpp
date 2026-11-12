// A corpus entry declared in a namespace nested in fixy::corpus and left
// out of Entries.
//
// The walk reads each nested namespace, so it names the entry.  A walk of
// the direct members of fixy::corpus alone does not see it.

#include <fixy/Corpus.h>

namespace fixy::corpus::extra {

struct nested_entry final : ::foundation::diag::tag_base {
    template <class Type, class... Atoms>
    [[nodiscard]] static consteval bool matches() noexcept {
        return true;
    }
};

}  // namespace fixy::corpus::extra

namespace fixture_site {
struct here final {};
}  // namespace fixture_site

[[maybe_unused]] constexpr bool joined = ::fixy::corpus::corpus_is_joined<fixture_site::here>();

int main() { return 0; }

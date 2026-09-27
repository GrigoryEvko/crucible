// A corpus entry declared in fixy::corpus and left out of Entries.
//
// The walk consults only the entries that the Entries tuple names, so an
// entry that is declared and not joined matches nothing and refuses
// nothing.  The check reads the declarations of the namespace from the
// vantage point of this translation unit, and it names the entry.

#include <fixy/Corpus.h>

#include <string_view>

namespace fixy::corpus {

struct forgotten_entry final : ::foundation::diag::tag_base {
    static constexpr std::string_view name = "forgotten_entry";

    template <class Type, class... Atoms>
    [[nodiscard]] static consteval bool matches() noexcept {
        return true;
    }
};

}  // namespace fixy::corpus

namespace fixture_site {
struct here final {};
}  // namespace fixture_site

static_assert(::fixy::corpus::corpus_entries_declared_but_not_joined<fixture_site::here>().empty(),
              ::fixy::corpus::corpus_join_diagnostic<fixture_site::here>());

int main() {
    return 0;
}

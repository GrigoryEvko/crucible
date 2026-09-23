// A view payload is refused.  A string_view points into storage that it
// does not own, so the characters can change under the version.

#include <fixy/EpochVersioned.h>

#include <string_view>

int main() {
    fixy::EpochVersioned<std::string_view> const name{std::string_view{"node"}, fixy::Epoch{2}, fixy::Generation{2}};
    return static_cast<int>(name.peek().size());
}

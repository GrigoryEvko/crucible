// A view payload is refused.  A string_view points into storage that it
// does not own, so the characters can change under the version.

#include <fixy/EpochVersioned.h>

#include <string_view>

int main() {
    auto const name = fixy::EpochVersioned<std::string_view>::at_genesis(std::string_view{"node"});
    return static_cast<int>(name.peek().size());
}

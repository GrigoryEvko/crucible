// The compile-time checks of crucible/forge/recipes/Network.h.

#include <crucible/forge/recipes/Network.h>

namespace crucible::forge::recipes {

static_assert(sizeof(NetworkChunkCount) == sizeof(std::uint8_t));
static_assert(sizeof(DeclaredNetworkRecipeConstraints) == sizeof(NetworkRecipeConstraints));
// The chunk count is refined, so no byte route builds the constraints.  The
// copy and the destruction stay trivial, so the value still passes in
// registers.
static_assert(std::is_trivially_copy_constructible_v<NetworkRecipeConstraints>
              && std::is_trivially_destructible_v<NetworkRecipeConstraints>);

}  // namespace crucible::forge::recipes

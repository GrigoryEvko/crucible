// The contract rule of the tree rejects a P2900 `post` specifier on a
// function.  CRUCIBLE_POST of foundation/contracts/Post.h replaces it, before
// each return statement.  The quarantine plugin of utils/tools/quarantine/
// applies the rule in each build.

namespace {

[[nodiscard]] int doubled(int const value) noexcept post(result : result == value * 2) { return value * 2; }

}  // namespace

int main() { return doubled(1) - 2; }

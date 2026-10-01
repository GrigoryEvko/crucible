// The contract rule of the tree rejects a P2900 `pre` specifier on a
// function.  GCC 16 does not keep the specifier of a template in a header unit
// or in a precompiled header, and a constant evaluation can ignore the
// specifier.  CRUCIBLE_PRE of foundation/contracts/Pre.h replaces it.  The
// quarantine plugin of utils/tools/quarantine/ applies the rule in each build.

namespace {

[[nodiscard]] int checked(int value) noexcept pre(value > 0) { return value; }

}  // namespace

int main() { return checked(1) - 1; }

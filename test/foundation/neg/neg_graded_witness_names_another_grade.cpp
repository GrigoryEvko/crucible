// Where the value is its own grade, the keyed constructor takes the grade
// as a witness, and the witness must name the grade that the value has.
// A counter of 5 with a witness of 10 is inside the order and names
// another grade, so the in-body CRUCIBLE_PRE refuses it at consteval.
#include <foundation/algebra/Graded.h>
#include <foundation/algebra/lattices/MonotoneLattice.h>

namespace {
namespace fa = ::foundation::algebra;

struct fixture_authority {
    [[nodiscard]] static constexpr fa::grade_key<fixture_authority> key() noexcept { return {}; }
};

using Counter = fa::Graded<fa::ModalityKind::Absolute, fa::lattices::MonotoneLattice<int>, int>;
constexpr Counter forged{fixture_authority::key(), 5, 10};
}  // namespace

int main() { return forged.peek(); }

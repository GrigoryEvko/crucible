// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// unique_target reads a relation as a function of its From.  A From with
// two edges has two targets, so the relation is not a function of it.
// Without the check that counts the edges from the type, the search
// returns the first edge it meets, and the order of declaration picks the
// answer.
//
// Expected diagnostic: the refusal in unique_target that asks for at most
// one edge from the type.

#include <foundation/diag/FailClosed.h>

#include <type_traits>

namespace {

namespace ffc = ::foundation::fail_closed;

struct Raw {};
struct Checked {};
struct Stored {};

namespace ingest {
inline constexpr ffc::edge<Raw, Checked> raw_to_checked{};
inline constexpr ffc::edge<Raw, Stored> raw_to_stored{};
}  // namespace ingest

[[maybe_unused]] constexpr bool target_is_checked = std::is_same_v<ffc::unique_target_t<^^ingest, Raw>, Checked>;

}  // namespace

int main() { return 0; }

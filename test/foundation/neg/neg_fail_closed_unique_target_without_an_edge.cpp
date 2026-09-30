// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// unique_target reads a relation as a function of its From.  A From with
// no edge has no target, and the relation must not answer for it.
// Without the check that counts the edges from the type, the search finds
// no edge and falls through to void, and the alias below names void as
// the target of Stored.
//
// Expected diagnostic: the refusal in unique_target that asks for an edge
// from the type.

#include <foundation/diag/FailClosed.h>

#include <type_traits>

namespace {

namespace ffc = ::foundation::fail_closed;

struct Raw {};
struct Checked {};
struct Stored {};

namespace ingest {
inline constexpr ffc::edge<Raw, Checked> raw_to_checked{};
}  // namespace ingest

[[maybe_unused]] constexpr bool target_is_void = std::is_void_v<ffc::unique_target_t<^^ingest, Stored>>;

}  // namespace

int main() { return 0; }

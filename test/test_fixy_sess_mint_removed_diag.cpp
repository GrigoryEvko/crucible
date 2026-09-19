// The old session mint is deleted, and a diagnostic tag carries the
// explanation a caller sees when they reach for it.  This file pins that
// the tag stays reachable, named and classified, and that the replacement
// it points at stays callable.

#include <crucible/fixy/Sess.h>

#include <string_view>
#include <type_traits>

namespace fd = crucible::safety::diag;
namespace fs = crucible::fixy::sess;

static_assert(std::is_base_of_v<fd::tag_base, fs::diag::FixyMintSessionRemoved>,
              "FixyMintSessionRemoved must inherit safety::diag::tag_base.");

static_assert(fd::is_diagnostic_class_v<fs::diag::FixyMintSessionRemoved>,
              "FixyMintSessionRemoved must satisfy is_diagnostic_class_v.");

static_assert(!fd::diagnostic_name_v<fs::diag::FixyMintSessionRemoved>.empty(),
              "FixyMintSessionRemoved must surface a non-empty name string.");

static_assert(fd::diagnostic_name_v<fs::diag::FixyMintSessionRemoved> == std::string_view{"FixyMintSessionRemoved"},
              "FixyMintSessionRemoved must surface the canonical name literal.");

static_assert(!fd::diagnostic_description_v<fs::diag::FixyMintSessionRemoved>.empty(),
              "FixyMintSessionRemoved description must explain the deletion.");

static_assert(!fd::diagnostic_remediation_v<fs::diag::FixyMintSessionRemoved>.empty(),
              "the remediation must point at the replacement mint");

static_assert(std::is_final_v<fs::diag::FixyMintSessionRemoved>, "a diagnostic tag is final, so nothing can extend it");

static_assert(std::is_empty_v<fs::diag::FixyMintSessionRemoved>,
              "a diagnostic tag is a type-level witness and carries no data");

static_assert(!fd::is_diagnostic_class_v<int>);

// There is no assertion here that the deleted mints stay deleted.  A
// deleted function is not friendly to substitution: naming one inside a
// requires-expression is a hard error rather than a false result, so that
// half of the claim lives in a fixture that must fail to compile.  The
// error the caller sees is itself the diagnostic surface.
//
// What can be asserted is the other half: the replacement the diagnostic
// points at must stay callable, so a rename that removes both the original
// and its replacement leaves no call site stranded.

#include <utility>

#include <crucible/effects/_ExecCtx.h>

namespace fixy_a4_014_pin {

struct ProbeResource {};

// The smallest well-formed session there is.  A malformed one would fail
// the well-formedness gate before reaching the check this file cares
// about.
using ProbeWellFormedProto = fs::Send<int, fs::End>;

static_assert(
    requires(::crucible::effects::BgCompileCtx ctx, ProbeResource res) {
        fs::mint_permissioned_session<ProbeWellFormedProto>(ctx, std::move(res));
    }, "the permissioned mint must remain callable; it is what the removed "
       "mint's remediation field points at");

}  // namespace fixy_a4_014_pin

int main() {
    // One runtime read of the name, so the tag cannot be eliminated as
    // dead code below the assertions.
    auto name = fd::diagnostic_name_v<fs::diag::FixyMintSessionRemoved>;
    return name.empty() ? 1 : 0;
}

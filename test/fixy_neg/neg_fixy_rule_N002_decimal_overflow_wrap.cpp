// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
//
// FIXY-B4 — N002 (decimal x overflow-wrap) reachable via fixy::fn:
//
//   * grant::overflow_wrap                 → Overflow = Wrap
//   * grant::typed<UserExactDecimal>       → Type     = UserExactDecimal
//   * is_exact_decimal<UserExactDecimal>   → user-side specialization
//
// The user opts INTO exact-decimal semantics by specializing
// `safety::fn::collision::is_exact_decimal` for their type.  At that
// point the substrate's ValidComposition sees Type as exact-decimal
// AND Overflow as Wrap → N002 fires.
//
// This is the second §6.8 rule (after M012) reachable from grant tags
// alone without substrate marker-propagation work.

#include <crucible/fixy/Fn.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

// User-defined exact-decimal type — substrate has no built-in decimal,
// so the application crate ships one (e.g., financial code).  Per the
// substrate's primary-template false_type fallback, opting in is one
// partial specialization.
struct UserDecimal {
    long long mantissa = 0;
    int       exponent = 0;
    constexpr UserDecimal() noexcept = default;
    explicit constexpr UserDecimal(long long m, int e) noexcept
        : mantissa{m}, exponent{e} {}
};

namespace crucible::safety::fn::collision {
template <>
struct is_exact_decimal<UserDecimal> : std::true_type {};
}  // namespace crucible::safety::fn::collision

// Compose the violating fn: Type = UserDecimal + Overflow = Wrap.
// Substrate's ValidComposition rejects with the N002 diagnostic.
using ViolatingFn = cf::fn<UserDecimal,
    cg::typed<UserDecimal>,                            // Type engagement
    cg::overflow_wrap,                                 // Overflow = Wrap
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>>;

// Force the substrate Fn body to fire ValidComposition.
static_assert(sizeof(ViolatingFn) > 0, "N002");

int main() { return 0; }

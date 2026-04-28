// Sentinel TU for `safety/diag/CheatProbe.h` (FOUND-E05).
//
// The cheat-probe pattern locks in concept-gate strictness across
// regressions.  This TU:
//
//   1. Verifies the foundation header's compile-time invariants
//      survive the project's full warning matrix (-Werror=conversion,
//      sign-conversion, etc.).
//   2. Exercises the user-extension workflow: a project defines its
//      own `concept_gate<Category>` specialization and registers
//      cheats against it.
//   3. Demonstrates the "gate not yet shipped" pre-registration
//      pattern: cheats registered against undefined gates are
//      no-ops; they activate when the gate's specialization lands.
//
// The build SUCCEEDS only when every cheat is correctly REJECTED by
// its gate.  If a future regression weakens a gate, the corresponding
// cheat starts being admitted, this TU fails to compile, and the
// regression is caught at build time.

#include <crucible/safety/diag/CheatProbe.h>

#include <cstddef>
#include <type_traits>

// ═════════════════════════════════════════════════════════════════════
// User-extension example: a project's per-tenant data sanitizer
// concept.  The gate accepts ONLY types that explicitly opt in via a
// `tenant_sanitized` typedef set to `std::true_type`.
// ═════════════════════════════════════════════════════════════════════

namespace user_proj {

template <typename T>
concept TenantSanitized = requires {
    typename T::tenant_sanitized;
    requires std::is_same_v<typename T::tenant_sanitized, std::true_type>;
};

// Production type that opts in.
struct SanitizedQueryResult {
    using tenant_sanitized = std::true_type;
};

// Cheat 1: forgot the typedef entirely — must be rejected.
struct Cheat1_NoOptIn {};

// Cheat 2: typedef has WRONG value (false_type) — must be rejected.
struct Cheat2_FalseOptIn {
    using tenant_sanitized = std::false_type;
};

// Cheat 3: typedef is the wrong NAME — must be rejected.
struct Cheat3_WrongName {
    using tenant_sanitised = std::true_type;  // British spelling typo
};

// Cheat 4: typedef value is something other than true_type/false_type
// — must be rejected (concept's `requires` clause is strict).
struct Cheat4_WrongValueType {
    using tenant_sanitized = int;
};

}  // namespace user_proj

// ═════════════════════════════════════════════════════════════════════
// Specialize the project's concept_gate.  Choose a Category that
// matches the gate's semantic — TenantSanitized is a refinement
// discipline, so RefinementViolation is the right slot.
// ═════════════════════════════════════════════════════════════════════

namespace crucible::safety::diag {

template <>
struct concept_gate<Category::RefinementViolation> {
    static constexpr bool defined = true;

    template <typename T>
    static constexpr bool admits_type = ::user_proj::TenantSanitized<T>;

    // No function-pointer side for this gate.
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

}  // namespace crucible::safety::diag

// ═════════════════════════════════════════════════════════════════════
// Lock in: the gate accepts the production type AND rejects every
// cheat.  These are the project's regression locks.
// ═════════════════════════════════════════════════════════════════════

namespace user_proj::cheat_locks {

namespace diag = ::crucible::safety::diag;

// Sanity check: gate is now defined.
static_assert(diag::is_gate_defined_v<diag::Category::RefinementViolation>);

// Production type IS admitted (would fire a different
// "rejected legitimate type" alarm if not — but this isn't a cheat
// probe, just an admittance assertion).
static_assert(diag::concept_gate<diag::Category::RefinementViolation>
              ::admits_type<SanitizedQueryResult>);

// Cheats — every one must be rejected.  cheat_probe_type's static_assert
// fires if the gate spuriously admits.  Build SUCCEEDS only when all
// four are correctly rejected.
using probe_1 = diag::cheat_probe_type<Cheat1_NoOptIn,
                                       diag::Category::RefinementViolation>;
using probe_2 = diag::cheat_probe_type<Cheat2_FalseOptIn,
                                       diag::Category::RefinementViolation>;
using probe_3 = diag::cheat_probe_type<Cheat3_WrongName,
                                       diag::Category::RefinementViolation>;
using probe_4 = diag::cheat_probe_type<Cheat4_WrongValueType,
                                       diag::Category::RefinementViolation>;

// Companion fact-checks (the cheat probe asserts !admits; these
// directly assert the same fact in a more readable form).
static_assert(!diag::concept_gate<diag::Category::RefinementViolation>
              ::admits_type<Cheat1_NoOptIn>);
static_assert(!diag::concept_gate<diag::Category::RefinementViolation>
              ::admits_type<Cheat2_FalseOptIn>);
static_assert(!diag::concept_gate<diag::Category::RefinementViolation>
              ::admits_type<Cheat3_WrongName>);
static_assert(!diag::concept_gate<diag::Category::RefinementViolation>
              ::admits_type<Cheat4_WrongValueType>);

// Other Categories' gates are STILL undefined — sanity-check the
// surface stays correct after ONE specialization.  No leak between
// gates.
static_assert(!diag::is_gate_defined_v<diag::Category::HotPathViolation>);
static_assert(!diag::is_gate_defined_v<diag::Category::DetSafeLeak>);
static_assert(!diag::is_gate_defined_v<diag::Category::LinearityViolation>);

// Pre-registered cheats against undefined gates are no-ops.  This
// is the documented "ship cheats ahead of gate" workflow.
struct future_cheat {};
using preregistered =
    diag::cheat_probe_type<future_cheat, diag::Category::HotPathViolation>;
// The probe's static_assert clause `!gate_defined || !admits` is
// trivially satisfied (gate_defined = false).  When HotPathViolation's
// gate eventually ships AND the cheat would be admitted, this line
// starts firing.

}  // namespace user_proj::cheat_locks

// ═════════════════════════════════════════════════════════════════════
// Runtime smoke test.  Forces the consteval/constexpr trait reads
// through a runtime path so the TU is non-trivial after optimization.
// ═════════════════════════════════════════════════════════════════════

int main() {
    namespace diag = ::crucible::safety::diag;

    // Read the trait at runtime via volatile to defeat constant-fold.
    // The shipped specialization (RefinementViolation) returns true.
    volatile int v = static_cast<int>(diag::Category::RefinementViolation);
    auto c = static_cast<diag::Category>(v);

    bool defined = false;
    switch (c) {
        case diag::Category::RefinementViolation:
            defined = diag::is_gate_defined_v<
                diag::Category::RefinementViolation>;
            break;
        default:
            defined = false;
            break;
    }
    return defined ? 0 : 1;  // exit 0 iff the gate is correctly defined
}

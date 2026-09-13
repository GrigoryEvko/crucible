// A cheat probe registers a type that a concept gate must refuse.  The
// build succeeds only while every registered cheat is still refused, so a
// change that weakens a gate is caught the moment the gate starts letting
// one through.  The gate below is an example of the same pattern applied
// to a concept a project defines for itself.

#include <crucible/safety/diag/CheatProbe.h>

#include <cstddef>
#include <type_traits>

namespace user_proj {

template <typename T>
concept TenantSanitized = requires {
    typename T::tenant_sanitized;
    requires std::is_same_v<typename T::tenant_sanitized, std::true_type>;
};

struct SanitizedQueryResult {
    using tenant_sanitized = std::true_type;
};

struct Cheat1_NoOptIn {};

struct Cheat2_FalseOptIn {
    using tenant_sanitized = std::false_type;
};

struct Cheat3_WrongName {
    using tenant_sanitised = std::true_type;  // one letter off, on purpose
};

struct Cheat4_WrongValueType {
    using tenant_sanitized = int;
};

}  // namespace user_proj

// The category a gate is registered under should match what the gate
// polices.  This one is an opt-in refinement, so it goes in the
// refinement slot.

namespace crucible::safety::diag {

template <>
struct concept_gate<Category::RefinementViolation> {
    static constexpr bool defined = true;

    template <typename T>
    static constexpr bool admits_type = ::user_proj::TenantSanitized<T>;

    // This gate polices types only.
    template <auto FnPtr>
    static constexpr bool admits_function = false;
};

}  // namespace crucible::safety::diag

namespace user_proj::cheat_locks {

namespace diag = ::crucible::safety::diag;

static_assert(diag::is_gate_defined_v<diag::Category::RefinementViolation>);

// The gate must still admit the type it exists to admit.  A gate that
// refuses everything would satisfy every cheat probe below.
static_assert(diag::concept_gate<diag::Category::RefinementViolation>::admits_type<SanitizedQueryResult>);

using probe_1 = diag::cheat_probe_type<Cheat1_NoOptIn, diag::Category::RefinementViolation>;
using probe_2 = diag::cheat_probe_type<Cheat2_FalseOptIn, diag::Category::RefinementViolation>;
using probe_3 = diag::cheat_probe_type<Cheat3_WrongName, diag::Category::RefinementViolation>;
using probe_4 = diag::cheat_probe_type<Cheat4_WrongValueType, diag::Category::RefinementViolation>;

// The probes above already assert this.  Spelling it out directly makes
// the failure readable when one of them fires.
static_assert(!diag::concept_gate<diag::Category::RefinementViolation>::admits_type<Cheat1_NoOptIn>);
static_assert(!diag::concept_gate<diag::Category::RefinementViolation>::admits_type<Cheat2_FalseOptIn>);
static_assert(!diag::concept_gate<diag::Category::RefinementViolation>::admits_type<Cheat3_WrongName>);
static_assert(!diag::concept_gate<diag::Category::RefinementViolation>::admits_type<Cheat4_WrongValueType>);

// Defining one gate must not define any other.
static_assert(!diag::is_gate_defined_v<diag::Category::HotPathViolation>);
static_assert(!diag::is_gate_defined_v<diag::Category::DetSafeLeak>);
static_assert(!diag::is_gate_defined_v<diag::Category::LinearityViolation>);

// A cheat may be registered before the gate that will refuse it exists.
// Until then the probe is satisfied trivially, because its condition holds
// whenever the gate is undefined.  The day that gate ships, this line
// starts carrying weight without anyone having to remember it.
struct future_cheat {};
using preregistered = diag::cheat_probe_type<future_cheat, diag::Category::HotPathViolation>;

}  // namespace user_proj::cheat_locks

int main() {
    namespace diag = ::crucible::safety::diag;

    // Volatile defeats constant folding, so the trait is read on a runtime
    // path rather than folded away with the rest of the file.
    volatile int v = static_cast<int>(diag::Category::RefinementViolation);
    auto c = static_cast<diag::Category>(v);

    bool defined = false;
    switch (c) {
        case diag::Category::RefinementViolation:
            defined = diag::is_gate_defined_v<diag::Category::RefinementViolation>;
            break;
        default:
            defined = false;
            break;
    }
    return defined ? 0 : 1;
}

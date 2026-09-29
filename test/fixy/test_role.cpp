// Sentinel TU for fixy/Role.h: every unary role passes the role gate
// and mints through mint_fn_for, every binary role mints through
// mint_fn with its pack spelled and is the type the alias names, each
// role pins the grades it says and leaves the rest strict, and the
// two roles that pin as_public are refused without it.
//
// The header self-test proves the gate for each role.  What this file
// adds is the minting, at compile time and at run time, and a check of
// three pins of each role: the effect rows, the security grades and the
// size collapse.

#include <fixy/Role.h>

#include <fixy/Atom.h>
#include <fixy/Fn.h>
#include <fixy/Reject.h>
#include <fixy/Tags.h>

#include <cstddef>
#include <meta>
#include <type_traits>

namespace {

namespace at = ::fixy::atom;
namespace role = ::fixy::role;
namespace policy = ::fixy::tags::secret_policy;
using Eff = ::foundation::effects::Effect;
using ::fixy::Axis;
using ::fixy::axis_traits;
using ::fixy::fn;
using ::fixy::IsAccepted;
using ::fixy::IsRoleFor;
using ::fixy::mint_fn;
using ::fixy::mint_fn_for;

// ---------------------------------------------------------------------
// The unary roles pass the role gate, so mint_fn_for is a door.

static_assert(IsRoleFor<role::PureLinear, int>);
static_assert(IsRoleFor<role::PureCopy, int>);
static_assert(IsRoleFor<role::IoFunction, int>);
static_assert(IsRoleFor<role::BgWorker, int>);
static_assert(IsRoleFor<role::CtCrypto, int>);
static_assert(IsRoleFor<role::PureLinear, double>);
static_assert(IsRoleFor<role::CtCrypto, unsigned char>);

// A payload the gate refuses is refused through a role too.
static_assert(!IsRoleFor<role::PureLinear, void>);
static_assert(!IsRoleFor<role::PureLinear, int&>);
static_assert(!IsRoleFor<role::IoFunction, const int>);

// ---------------------------------------------------------------------
// The binary roles pass the gate with their pack spelled, and the alias
// is that type.

static_assert(IsAccepted<int, at::declassify<policy::AuditedLogging>>);
static_assert(IsAccepted<int, at::with_io, at::declassify<policy::WireSerialize>>);
static_assert(
    std::is_same_v<role::SecretConsumer<int, policy::AuditedLogging>, fn<int, at::declassify<policy::AuditedLogging>>>);
static_assert(std::is_same_v<role::PublicEmit<int, policy::WireSerialize>,
                             fn<int, at::with_io, at::declassify<policy::WireSerialize>>>);

// ---------------------------------------------------------------------
// The grades each role pins, and the ones it leaves strict.

static_assert(std::is_same_v<role::PureLinear<int>, fn<int>>);
static_assert(role::PureLinear<int>::atom_count == 0);
static_assert(std::is_same_v<role::PureCopy<int>::grade_on<Axis::Usage>, at::copy>);
static_assert(std::is_same_v<role::IoFunction<int>::grade_on<Axis::Effect>, at::with<Eff::IO>>);
static_assert(std::is_same_v<role::IoFunction<int>::grade_on<Axis::Security>, at::as_public>);
static_assert(std::is_same_v<role::BgWorker<int>::grade_on<Axis::Effect>, at::with<Eff::Bg, Eff::Alloc>>);
static_assert(std::is_same_v<role::BgWorker<int>::grade_on<Axis::Security>, at::as_public>);
static_assert(std::is_same_v<role::CtCrypto<int>::grade_on<Axis::Effect>, at::with<>>);
static_assert(std::is_same_v<role::CtCrypto<int>::grade_on<Axis::Security>, at::constant_time>);
static_assert(std::is_same_v<role::PublicEmit<int, policy::WireSerialize>::grade_on<Axis::Effect>, at::with<Eff::IO>>);
static_assert(std::is_same_v<role::PublicEmit<int, policy::WireSerialize>::grade_on<Axis::Security>,
                             at::declassify<policy::WireSerialize>>);

// The strict poles carry the rest.  Usage stays linear on every role
// but PureCopy, and Reentrancy stays non-reentrant on all of them.
static_assert(std::is_same_v<role::IoFunction<int>::grade_on<Axis::Usage>, axis_traits<Axis::Usage>::strict>);
static_assert(std::is_same_v<role::BgWorker<int>::grade_on<Axis::Usage>, axis_traits<Axis::Usage>::strict>);
static_assert(std::is_same_v<role::CtCrypto<int>::grade_on<Axis::Usage>, axis_traits<Axis::Usage>::strict>);
static_assert(std::is_same_v<role::CtCrypto<int>::grade_on<Axis::Reentrancy>, axis_traits<Axis::Reentrancy>::strict>);
static_assert(std::is_same_v<role::PureCopy<int>::grade_on<Axis::Security>, axis_traits<Axis::Security>::strict>);
static_assert(std::is_same_v<role::PureCopy<int>::grade_on<Axis::Effect>, axis_traits<Axis::Effect>::strict>);
static_assert(!role::IoFunction<int>::mentions_axis<Axis::Usage>);
static_assert(role::IoFunction<int>::mentions_axis<Axis::Effect>);
static_assert(role::IoFunction<int>::mentions_axis<Axis::Security>);
static_assert(role::CtCrypto<int>::mentions_axis<Axis::Effect>, "the empty row is spelled out, not defaulted");

// ---------------------------------------------------------------------
// Why IoFunction and BgWorker pin as_public: the same pack without it
// is classified IO or classified Bg, and the corpus refuses it.  The
// two roles are the same packs with the public grade named.

static_assert(!IsAccepted<int, at::with_io>);
static_assert(!IsAccepted<int, at::with<Eff::Bg, Eff::Alloc>>);
static_assert(IsAccepted<int, at::with_io, at::as_public>);
static_assert(IsAccepted<int, at::with<Eff::Bg, Eff::Alloc>, at::as_public>);

// ---------------------------------------------------------------------
// Every atom is empty, so a role collapses to its payload.

static_assert(sizeof(role::PureLinear<int>) == sizeof(int));
static_assert(sizeof(role::PureCopy<int>) == sizeof(int));
static_assert(sizeof(role::IoFunction<int>) == sizeof(int));
static_assert(sizeof(role::BgWorker<int>) == sizeof(int));
static_assert(sizeof(role::CtCrypto<int>) == sizeof(int));
static_assert(sizeof(role::SecretConsumer<int, policy::AuditedLogging>) == sizeof(int));
static_assert(sizeof(role::PublicEmit<int, policy::WireSerialize>) == sizeof(int));
static_assert(sizeof(role::PureLinear<char>) == sizeof(char));
static_assert(sizeof(role::PureLinear<double>) == sizeof(double));

// ---------------------------------------------------------------------
// The door, at compile time.

static_assert(std::is_same_v<decltype(mint_fn_for<role::PureLinear>(0)), role::PureLinear<int>>);
static_assert(std::is_same_v<decltype(mint_fn_for<role::PureCopy>(0)), role::PureCopy<int>>);
static_assert(std::is_same_v<decltype(mint_fn_for<role::IoFunction>(0)), role::IoFunction<int>>);
static_assert(std::is_same_v<decltype(mint_fn_for<role::BgWorker>(0)), role::BgWorker<int>>);
static_assert(std::is_same_v<decltype(mint_fn_for<role::CtCrypto>(0)), role::CtCrypto<int>>);
static_assert(std::is_same_v<decltype(mint_fn<int, at::declassify<policy::AuditedLogging>>(0)),
                             role::SecretConsumer<int, policy::AuditedLogging>>);
static_assert(std::is_same_v<decltype(mint_fn<int, at::with_io, at::declassify<policy::WireSerialize>>(0)),
                             role::PublicEmit<int, policy::WireSerialize>>);

[[nodiscard]] consteval bool every_role_mints_the_value() noexcept {
    return mint_fn_for<role::PureLinear>(1).value() == 1 && mint_fn_for<role::PureCopy>(2).value() == 2
        && mint_fn_for<role::IoFunction>(3).value() == 3 && mint_fn_for<role::BgWorker>(4).value() == 4
        && mint_fn_for<role::CtCrypto>(5).value() == 5
        && mint_fn<int, at::declassify<policy::AuditedLogging>>(6).value() == 6
        && mint_fn<int, at::with_io, at::declassify<policy::WireSerialize>>(7).value() == 7;
}
static_assert(every_role_mints_the_value());

// The value constructor stays private through a role.
static_assert(!std::is_constructible_v<role::IoFunction<int>, int>);
static_assert(!std::is_constructible_v<role::PublicEmit<int, policy::WireSerialize>, int>);

// ---------------------------------------------------------------------
// What every role refuses, over every role there is.
//
// The walk reads the roles out of fixy::role.  A role that a later change
// declares is in the walk at once.  A unary role mints through
// mint_fn_for, and the walk asks IsRoleFor, the gate of that door.  A
// binary role mints through mint_fn, and the walk asks IsAccepted, the
// gate of that door, about the fn that the role names.  For each role
// the walk also checks four things: the gate admits an int payload, the
// value constructor stays private, the binding is not an atom, and a
// binary role takes no policy outside the closed set of fixy/Tags.h.

template <class F>
struct is_admitted_fn : std::false_type {};
template <class T, class... Atoms>
struct is_admitted_fn<fn<T, Atoms...>> : std::bool_constant<IsAccepted<T, Atoms...>> {};

struct policy_outside_the_catalog final {};

// The payloads that tier 1 of the gate refuses.
inline constexpr std::meta::info unholdable_payloads[] = {^^void,      ^^int[4],       ^^int&,    ^^int&&,
                                                          ^^const int, ^^volatile int, ^^int(int)};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

template <class Binding>
[[nodiscard]] consteval bool is_a_closed_binding() noexcept {
    return is_admitted_fn<Binding>::value && !std::is_constructible_v<Binding, int> && !::fixy::atom::IsAtom<Binding>;
}

[[nodiscard]] consteval bool unary_role_refuses_each_unholdable_payload(std::meta::info role_member) {
    for (const std::meta::info payload : unholdable_payloads) {
        if (std::meta::extract<bool>(std::meta::substitute(^^::fixy::IsRoleFor, {role_member, payload}))) return false;
    }
    return true;
}

struct role_walk {
    std::size_t declared = 0;
    std::size_t proven = 0;
};

[[nodiscard]] consteval role_walk walk_the_roles() noexcept {
    role_walk walk{};
    static constexpr auto role_members =
        std::define_static_array(std::meta::members_of(^^::fixy::role, std::meta::access_context::current()));
    constexpr auto policy_arg = ^^policy::WireSerialize;
    template for (constexpr auto role_member : role_members) {
        ++walk.declared;
        if constexpr (std::meta::can_substitute(^^::fixy::IsRoleFor, {role_member, ^^int})) {
            using Binding = [:std::meta::substitute(role_member, {^^int}):];
            if (is_a_closed_binding<Binding>()
                && std::meta::extract<bool>(std::meta::substitute(^^::fixy::IsRoleFor, {role_member, ^^int}))
                && unary_role_refuses_each_unholdable_payload(role_member)) {
                ++walk.proven;
            }
        } else if constexpr (std::meta::can_substitute(role_member, {^^int, policy_arg})) {
            using Binding = [:std::meta::substitute(role_member, {^^int, policy_arg}):];
            bool refuses_each_payload = true;
            template for (constexpr auto payload : unholdable_payloads) {
                using Refused = [:std::meta::substitute(role_member, {payload, policy_arg}):];
                refuses_each_payload = refuses_each_payload && !is_admitted_fn<Refused>::value;
            }
            const bool refuses_each_non_policy =
                !std::meta::can_substitute(role_member, {^^int, ^^void})
                && !std::meta::can_substitute(role_member, {^^int, ^^policy::WireSerialize& })
                && !std::meta::can_substitute(role_member, {^^int, ^^policy_outside_the_catalog});
            if (is_a_closed_binding<Binding>() && refuses_each_payload && refuses_each_non_policy) ++walk.proven;
        }
    }
    return walk;
}

#pragma GCC diagnostic pop

constexpr role_walk the_roles = walk_the_roles();
static_assert(the_roles.declared > 0, "the walk found no role in fixy::role, so it proves nothing");
static_assert(the_roles.proven == the_roles.declared,
              "a role admits a payload or a policy that its door must refuse, or it has an arity that is neither the "
              "payload alone nor a payload and a policy");

// ---------------------------------------------------------------------
// A static_assert proves the constant-evaluated path only.  These run.

[[nodiscard]] int check_runtime_paths() {
    volatile int seed = 10;

    const auto pure = mint_fn_for<role::PureLinear>(static_cast<int>(seed));
    if (pure.value() != 10) return 1;

    const auto copied = mint_fn_for<role::PureCopy>(static_cast<int>(seed) + 1);
    if (copied.value() != 11) return 2;

    const auto io = mint_fn_for<role::IoFunction>(static_cast<int>(seed) + 2);
    if (io.value() != 12) return 3;

    const auto worker = mint_fn_for<role::BgWorker>(static_cast<int>(seed) + 3);
    if (worker.value() != 13) return 4;

    const auto crypto = mint_fn_for<role::CtCrypto>(static_cast<int>(seed) + 4);
    if (crypto.value() != 14) return 5;

    const role::SecretConsumer<int, policy::AuditedLogging> consumer =
        mint_fn<int, at::declassify<policy::AuditedLogging>>(static_cast<int>(seed) + 5);
    if (consumer.value() != 15) return 6;

    const role::PublicEmit<int, policy::WireSerialize> emit =
        mint_fn<int, at::with_io, at::declassify<policy::WireSerialize>>(static_cast<int>(seed) + 6);
    if (emit.value() != 16) return 7;

    // The rvalue overload consumes.
    if (mint_fn_for<role::PureCopy>(static_cast<int>(seed) + 7).value() != 17) return 8;

    if (sizeof(io) != sizeof(int) || sizeof(emit) != sizeof(int)) return 9;
    if (io.atom_count != 2 || worker.atom_count != 2 || crypto.atom_count != 2 || pure.atom_count != 0) return 10;
    return 0;
}

}  // namespace

int main() {
    if (int rc = check_runtime_paths(); rc != 0) return rc;
    return 0;
}

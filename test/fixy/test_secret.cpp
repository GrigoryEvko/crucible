// Sentinel TU for fixy/Secret.h: the classification collapses to
// sizeof(T), the wrapper is move-only with one door, declassification is
// the only exit, and every policy tag leaves through its own edge and
// nothing else does.  The payload leaves only from an rvalue.  The length
// of a container leaves only through the LengthOnly policy, and that
// policy releases the length and never the payload.

#include <fixy/Secret.h>

#include <foundation/algebra/GradedTrait.h>
#include <foundation/diag/FailClosed.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace {

namespace fa = ::foundation::algebra;
namespace ffc = ::foundation::fail_closed;
namespace secret_policy = ::fixy::tags::secret_policy;
using ::fixy::AdmittedDeclassification;
using ::fixy::DeclassificationPolicy;
using ::fixy::LengthDeclassification;
using ::fixy::mint_secret;
using ::fixy::PayloadDeclassification;
using ::fixy::Secret;

struct CardNumber {
    std::uint64_t digits = 0;
};

// Regime 1: the classification is a type-level singleton and collapses
// under EBO.
static_assert(sizeof(Secret<int>) == sizeof(int));
static_assert(sizeof(Secret<long long>) == sizeof(long long));
static_assert(sizeof(Secret<CardNumber>) == sizeof(CardNumber), "Secret<T> must be zero-cost in layout");
static_assert(alignof(Secret<CardNumber>) == alignof(CardNumber));

// The diagnostic surface agrees with the substrate.
static_assert(fa::GradedWrapper<Secret<int>>);
static_assert(fa::is_graded_wrapper_v<Secret<int>>);
static_assert(!std::is_void_v<typename Secret<int>::graded_type>);
static_assert(Secret<int>::value_type_name().ends_with("int"));
static_assert(Secret<int>::lattice_name() == "ConfLattice::At<Secret>");
static_assert(Secret<int>::value_type_name() == Secret<int>::graded_type::value_type_name());
static_assert(Secret<int>::lattice_name() == Secret<int>::graded_type::lattice_name());
static_assert(Secret<int>::modality == fa::ModalityKind::Comonad);
static_assert(std::is_same_v<Secret<int>::lattice_type, fa::lattices::conf::SecretTier>);
static_assert(std::is_same_v<Secret<int>::value_type, int>);

// Move-only.
static_assert(!std::is_copy_constructible_v<Secret<int>>);
static_assert(!std::is_copy_assignable_v<Secret<int>>);
static_assert(std::is_nothrow_move_constructible_v<Secret<int>>);
static_assert(std::is_nothrow_move_assignable_v<Secret<int>>);

// One door: no public constructor of any shape.
static_assert(!std::is_default_constructible_v<Secret<int>>);
static_assert(!std::is_constructible_v<Secret<int>, int>);
static_assert(!std::is_constructible_v<Secret<int>, std::in_place_t, int>);
static_assert(!std::is_constructible_v<Secret<CardNumber>, CardNumber>);

// declassify takes an rvalue and nothing else.
template <typename W>
concept DeclassifiesLvalue = requires(W& w) { w.template declassify<secret_policy::AuditedLogging>(); };
template <typename W>
concept DeclassifiesConstRvalue =
    requires(W const& w) { std::move(w).template declassify<secret_policy::AuditedLogging>(); };
template <typename W>
concept DeclassifiesRvalue = requires(W&& w) { std::move(w).template declassify<secret_policy::AuditedLogging>(); };

static_assert(!DeclassifiesLvalue<Secret<int>>);
static_assert(!DeclassifiesConstRvalue<Secret<int>>);
static_assert(DeclassifiesRvalue<Secret<int>>);

// The mint and the exit are usable in a constant expression.
constexpr int minted_and_declassified =
    std::move(mint_secret<int>(7)).template declassify<secret_policy::HashForCompare>();
static_assert(minted_and_declassified == 7);
constexpr std::uint64_t built_in_place =
    std::move(mint_secret<CardNumber>(std::uint64_t{4242})).template declassify<secret_policy::UserDisplay>().digits;
static_assert(built_in_place == 4242);

// ── The length of a classified container ────────────────────────────

using Key16 = Secret<std::array<std::uint8_t, 16>>;

// No accessor gives the length out without a policy.
template <typename W>
concept ReleasesLengthWithoutPolicy = requires(W const& w) { w.size(); };
static_assert(!ReleasesLengthWithoutPolicy<Key16>);
static_assert(!ReleasesLengthWithoutPolicy<Secret<int>>);

// The LengthOnly policy releases the length from an lvalue and from an
// rvalue, and in each case the result is a count, not the payload.
template <typename W>
concept ReleasesLengthFromLvalue = requires(W const& w) { w.template declassify<secret_policy::LengthOnly>(); };
static_assert(ReleasesLengthFromLvalue<Key16>);
static_assert(std::is_same_v<decltype(std::declval<Key16 const&>().template declassify<secret_policy::LengthOnly>()),
                             std::size_t>);
static_assert(
    std::is_same_v<decltype(std::declval<Key16&&>().template declassify<secret_policy::LengthOnly>()), std::size_t>,
    "LengthOnly on an rvalue still releases the length and not the payload");

// A payload with no length has nothing for LengthOnly to release, and a
// payload policy does not release a length from an lvalue.
static_assert(!ReleasesLengthFromLvalue<Secret<int>>);
static_assert(!DeclassifiesLvalue<Key16>);

constexpr std::size_t length_in_constant_expression =
    mint_secret<std::array<std::uint8_t, 16>>().template declassify<secret_policy::LengthOnly>();
static_assert(length_in_constant_expression == 16);

// ── The policy relation, read out of the namespace ──────────────────

// The header pins the count; this TU derives it again and checks the
// shape of the relation.
static_assert(::fixy::admitted_policy_count == ffc::edge_count<^^secret_policy::admitted_policies>());
static_assert(ffc::every_edge_is_admitted<^^secret_policy::admitted_policies>());
static_assert(ffc::every_class_in_has_edge<^^secret_policy::admitted_policies, ^^secret_policy, ffc::EdgeEnd::To,
                                           secret_policy::secret_policy_base>());

// The From end of every edge is the lattice position Secret<T> sits at.
static_assert(std::is_same_v<secret_policy::admitted_policies::classified, Secret<int>::lattice_type>);
static_assert(ffc::has_edge_from<^^secret_policy::admitted_policies, Secret<int>::lattice_type>());
static_assert(!ffc::has_edge_from<^^secret_policy::admitted_policies, fa::lattices::conf::PublicTier>());

// The count of policy tags declared in the namespace.  A policy tag is a
// class derived from the marker base, other than the base itself.
[[nodiscard]] consteval std::size_t count_policy_tags_in_namespace() noexcept {
    std::size_t found = 0;
    for (const auto m : std::meta::members_of(^^secret_policy, std::meta::access_context::unchecked())) {
        if (!std::meta::is_type(m) || std::meta::is_type_alias(m) || !std::meta::is_class_type(m)) continue;
        if (m == ^^secret_policy::secret_policy_base) continue;
        if (std::meta::is_base_of_type(^^secret_policy::secret_policy_base, m)) ++found;
    }
    return found;
}

static_assert(count_policy_tags_in_namespace() == ::fixy::admitted_policy_count,
              "every declared policy tag has exactly one edge, and every edge names a declared tag");

// Every admitted policy is a DeclassificationPolicy, is admitted, and
// declassifies a minted value.  The LengthOnly policy releases the
// length of a container, and each other policy releases the payload.
[[nodiscard]] consteval bool every_policy_declassifies() noexcept {
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^secret_policy::admitted_policies, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto m : members) {
        if constexpr (ffc::is_edge(m)) {
            constexpr auto ends = ffc::ends_of(m);
            using Policy = typename[:ends.to:];
            static_assert(DeclassificationPolicy<Policy>);
            static_assert(AdmittedDeclassification<Policy>);
            if constexpr (LengthDeclassification<Policy>) {
                static_assert(!PayloadDeclassification<Policy>);
                if (mint_secret<std::array<int, 3>>().template declassify<Policy>() != 3) return false;
            } else {
                static_assert(PayloadDeclassification<Policy>);
                if (std::move(mint_secret<int>(3)).template declassify<Policy>() != 3) return false;
            }
        }
    }
#pragma GCC diagnostic pop
    return true;
}

// Exactly one admitted policy releases a length.
[[nodiscard]] consteval std::size_t count_length_policies() noexcept {
    std::size_t found = 0;
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^secret_policy::admitted_policies, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto m : members) {
        if constexpr (ffc::is_edge(m)) {
            constexpr auto ends = ffc::ends_of(m);
            using Policy = typename[:ends.to:];
            if constexpr (LengthDeclassification<Policy>) ++found;
        }
    }
#pragma GCC diagnostic pop
    return found;
}
static_assert(count_length_policies() == 1);

static_assert(every_policy_declassifies());

// A tag that derives from the marker base but has no edge is declared
// and not admitted: the base-class check passes and the edge check
// refuses.  A gate that checked only the base class would admit it, and
// the fail-closed namespace closes that gap.
struct UnadmittedPolicy final : secret_policy::secret_policy_base {};
static_assert(DeclassificationPolicy<UnadmittedPolicy>);
static_assert(!AdmittedDeclassification<UnadmittedPolicy>);

template <typename W, typename Policy>
concept DeclassifiesWith = requires(W&& w) { std::move(w).template declassify<Policy>(); };
static_assert(DeclassifiesWith<Secret<int>, secret_policy::AuditedLogging>);
static_assert(!DeclassifiesWith<Secret<int>, UnadmittedPolicy>);

// An ad-hoc struct is neither.
struct AdHocPolicy {};
static_assert(!DeclassificationPolicy<AdHocPolicy>);
static_assert(!AdmittedDeclassification<AdHocPolicy>);
static_assert(!DeclassificationPolicy<int>);
static_assert(!DeclassifiesWith<Secret<int>, AdHocPolicy>);

// ── Runtime cells ───────────────────────────────────────────────────

int check_transform_and_declassify() {
    Secret<CardNumber> card = mint_secret<CardNumber>(std::uint64_t{4242424242424242ULL});
    // Copy is deleted, so the card can only be moved out of.

    auto hashed = std::move(card).transform(
        [](CardNumber c) -> std::uint64_t { return c.digits ^ std::uint64_t{0xDEADBEEFCAFEBABEULL}; });
    static_assert(std::is_same_v<decltype(hashed), Secret<std::uint64_t>>);

    std::uint64_t raw = std::move(hashed).declassify<secret_policy::HashForCompare>();
    if (raw != (4242424242424242ULL ^ 0xDEADBEEFCAFEBABEULL)) return 10;
    return 0;
}

int check_length_release() {
    Secret<std::array<std::uint8_t, 16>> key =
        mint_secret<std::array<std::uint8_t, 16>>(std::array<std::uint8_t, 16>{std::uint8_t{0xA5}});
    // The length leaves through its policy, and the key stays classified.
    const std::size_t length = key.declassify<secret_policy::LengthOnly>();
    if (length != 16) return 20;
    // The payload leaves only through a payload policy, from an rvalue.
    auto released = std::move(key).declassify<secret_policy::AuditedLogging>();
    if (released.size() != 16 || released[0] != 0xA5) return 21;
    return 0;
}

int check_zeroize() {
    int probe = 0;
    for (int loop = 0; loop < 3; ++loop)
        probe += loop;  // not foldable to a literal

    Secret<std::uint64_t> key =
        mint_secret<std::uint64_t>(std::uint64_t{0xCAFEBABE12345678} + static_cast<std::uint64_t>(probe));
    key.zeroize();
    auto cleared = std::move(key).declassify<secret_policy::HashForCompare>();
    if (cleared != 0) return 30;
    return 0;
}

int check_move_transfer() {
    Secret<int> original = mint_secret<int>(11);
    Secret<int> moved = std::move(original);
    if (std::move(moved).declassify<secret_policy::UserDisplay>() != 11) return 40;
    return 0;
}

// Declassification routes through a substrate path distinct from the
// ordinary read and consume paths, and each policy names its own edge.
// A divergence between the compile-time and the run-time behaviour of
// one edge would classify the wrong bytes with no assertion noticing,
// so every edge a cell below does not already walk is walked here.
int check_each_policy_edge() {
    int seed = 17;

    Secret<int> s = mint_secret<int>(seed * 2);
    Secret<int> t = std::move(s).transform([](int&& v) { return v + 1; });
    if (std::move(t).declassify<secret_policy::AuditedLogging>() != 35) return 50;

    Secret<int> wire = mint_secret<int>(seed);
    if (std::move(wire).declassify<secret_policy::WireSerialize>() != 17) return 51;

    Secret<int> hashed = mint_secret<int>(seed);
    if (std::move(hashed).declassify<secret_policy::HashForCompare>() != 17) return 52;

    return 0;
}

}  // namespace

int main() {
    if (int rc = check_each_policy_edge(); rc != 0) return rc;
    if (int rc = check_transform_and_declassify(); rc != 0) return rc;
    if (int rc = check_length_release(); rc != 0) return rc;
    if (int rc = check_zeroize(); rc != 0) return rc;
    if (int rc = check_move_transfer(); rc != 0) return rc;

    return 0;
}

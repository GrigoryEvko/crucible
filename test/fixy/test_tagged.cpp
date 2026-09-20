// Sentinel TU for fixy/Tagged.h: the tag is phantom and collapses to
// sizeof(T), the wrapper has one door plus the documented slot-array
// door, retag moves along an admitted edge and nowhere else, and every
// property of the catalog is read out of the admitted_retags namespace
// rather than restated per edge.
//
// Cells ported from test/test_fixy_source.cpp, test/test_safety.cpp,
// test/test_fixy_v_261_arch_pinned.cpp, the self-tests of
// include/crucible/safety/source/Path.h, and the Tagged rows of
// test/test_migration_verification.cpp and test/test_graded_extract.cpp.

#include <fixy/Tagged.h>

#include <fixy/Qtt.h>
#include <foundation/algebra/GradedTrait.h>
#include <foundation/diag/FailClosed.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <type_traits>
#include <utility>

namespace {

namespace fa = ::foundation::algebra;
namespace ffc = ::foundation::fail_closed;
namespace tags = ::fixy::tags;
namespace source = ::fixy::tags::source;
namespace trust = ::fixy::tags::trust;
namespace access = ::fixy::tags::access;
namespace version = ::fixy::tags::version;
namespace vessel_trust = ::fixy::tags::vessel_trust;
using ::fixy::mint_tagged;
using ::fixy::RetagAllowed;
using ::fixy::Tagged;

struct VerificationTag {};

struct Config {
    int port;
};

// Regime 1: the grade is empty and collapses under EBO.
static_assert(sizeof(Tagged<int, VerificationTag>) == sizeof(int));
static_assert(sizeof(Tagged<long, VerificationTag>) == sizeof(long));
static_assert(sizeof(Tagged<Config, source::Sanitized>) == sizeof(Config),
              "Tagged<T, Tag> must be zero-cost in layout");
static_assert(sizeof(Tagged<int, source::X86Pinned>) == sizeof(int),
              "ArchPinned<Arch> must EBO-collapse in Tagged. The tag is phantom and carries no storage.");
static_assert(alignof(Tagged<Config, source::Sanitized>) == alignof(Config));

// Two regime-1 wrappers stacked collapse to the payload.  The collapse
// is the untracked build's: fixy/Qtt.h's consume tracker puts one byte
// of state inside Linear on purpose, and Tagged still adds none.
using TaggedLinear = Tagged<::fixy::Linear<int>, VerificationTag>;
static_assert(::fixy::qtt_consume_tracked || sizeof(TaggedLinear) == sizeof(int));
static_assert(sizeof(TaggedLinear) == sizeof(::fixy::Linear<int>));
static_assert(!std::is_copy_constructible_v<TaggedLinear>,
              "Tagged<Linear<T>, Tag> must preserve Linear's move-only discipline");
static_assert(std::is_move_constructible_v<TaggedLinear>);

// The diagnostic surface agrees with the substrate.
static_assert(fa::GradedWrapper<Tagged<int, VerificationTag>>);
static_assert(fa::is_graded_wrapper_v<Tagged<int, VerificationTag>>);
static_assert(fa::GradedWrapper<TaggedLinear>);
static_assert(!std::is_void_v<typename Tagged<int, VerificationTag>::graded_type>);
static_assert(Tagged<int, VerificationTag>::value_type_name().ends_with("int"));
static_assert(Tagged<int, VerificationTag>::lattice_name().ends_with("VerificationTag"));
static_assert(Tagged<int, VerificationTag>::value_type_name()
              == Tagged<int, VerificationTag>::graded_type::value_type_name());
static_assert(Tagged<int, VerificationTag>::lattice_name()
              == Tagged<int, VerificationTag>::graded_type::lattice_name());
static_assert(Tagged<int, VerificationTag>::modality == fa::ModalityKind::RelativeMonad);
static_assert(std::is_same_v<Tagged<int, VerificationTag>::lattice_type, fa::lattices::TrustLattice<VerificationTag>>);
static_assert(std::is_same_v<Tagged<int, VerificationTag>::value_type, int>);
static_assert(std::is_same_v<Tagged<int, VerificationTag>::tag_type, VerificationTag>);

// One door: the value constructor is private, so only the mint and
// retag construct a non-default value.  Copy and move stay defaulted:
// provenance is a fact about the value, and a copy carries the fact.
static_assert(!std::is_constructible_v<Tagged<int, source::Sanitized>, int>);
static_assert(!std::is_constructible_v<Tagged<Config, source::Sanitized>, Config>);
static_assert(std::is_copy_constructible_v<Tagged<int, source::Sanitized>>);
static_assert(std::is_nothrow_move_constructible_v<Tagged<int, source::Sanitized>>);

// The second door: an array of slots starts empty, so the default
// constructor exists exactly when T has one.
struct NoDefault {
    explicit NoDefault(int) {}
};
static_assert(std::is_default_constructible_v<Tagged<int, source::RegionOps>>);
static_assert(!std::is_default_constructible_v<Tagged<NoDefault, source::RegionOps>>);

// retag and into take an rvalue and nothing else.
template <typename W>
concept RetagsLvalue = requires(W& w) { w.template retag<source::Sanitized>(); };
template <typename W>
concept RetagsConstRvalue = requires(W const& w) { std::move(w).template retag<source::Sanitized>(); };
template <typename W>
concept RetagsRvalue = requires(W&& w) { std::move(w).template retag<source::Sanitized>(); };
template <typename W>
concept IntoLvalue = requires(W& w) { w.into(); };
template <typename W>
concept IntoRvalue = requires(W&& w) { std::move(w).into(); };

static_assert(!RetagsLvalue<Tagged<int, source::External>>);
static_assert(!RetagsConstRvalue<Tagged<int, source::External>>);
static_assert(RetagsRvalue<Tagged<int, source::External>>);
static_assert(!IntoLvalue<Tagged<int, source::External>>);
static_assert(IntoRvalue<Tagged<int, source::External>>);

// There is no value_mut(): a mutable reference would let a Sanitized
// tag wrap a value nobody sanitized.
template <typename W>
concept ExposesValueMut = requires(W& w) { w.value_mut(); };
static_assert(!ExposesValueMut<Tagged<int, source::Sanitized>>);

// The mint and both retag doors are usable in a constant expression.
constexpr int minted = mint_tagged<source::FromUser>(7).value();
static_assert(minted == 7);
constexpr int retagged_by_member = std::move(mint_tagged<source::External>(8)).retag<source::Sanitized>().value();
static_assert(retagged_by_member == 8);
constexpr int retagged_by_free = ::fixy::retag<source::Sanitized>(mint_tagged<source::External>(9)).value();
static_assert(retagged_by_free == 9);
constexpr int extracted = std::move(mint_tagged<source::FromUser>(10)).into();
static_assert(extracted == 10);

// Identity is admitted unconditionally; the sentinel pair never is.
static_assert(RetagAllowed<source::FromUser, source::FromUser>);
static_assert(RetagAllowed<source::X86Pinned, source::X86Pinned>);
static_assert(::fixy::retag_policy<source::FromUser, source::FromUser>::allowed);
static_assert(!RetagAllowed<::fixy::detail::retag_policy_test::NeverFrom, ::fixy::detail::retag_policy_test::NeverTo>);
static_assert(!::fixy::retag_policy<::fixy::detail::retag_policy_test::NeverFrom,
                                    ::fixy::detail::retag_policy_test::NeverTo>::allowed);

// The trust ratchet: Verified -> Unverified stays rejected.  Admitting it
// would defeat the verification-status monotonicity contract.
static_assert(!RetagAllowed<trust::Verified, trust::Unverified>);

// Cross-axis transitions stay rejected: laundering across orthogonal
// axes is never safe.
static_assert(!RetagAllowed<source::External, trust::Verified>);
static_assert(!RetagAllowed<source::FromUser, access::WriteOnce>);

// The path lanes: each external lane sanitizes, no lane re-introduces
// taint, no lane cross-narrows, and External does not back-fill a
// narrower provenance.  CipherPath has no edge at all.
static_assert(RetagAllowed<source::FromUserPath, source::Sanitized>);
static_assert(RetagAllowed<source::FromEnvPath, source::Sanitized>);
static_assert(RetagAllowed<source::FromConfigPath, source::Sanitized>);
static_assert(!RetagAllowed<source::Sanitized, source::FromUserPath>);
static_assert(!RetagAllowed<source::FromUserPath, source::FromEnvPath>);
static_assert(!RetagAllowed<source::External, source::FromUserPath>);
static_assert(!RetagAllowed<source::FromUserPath, source::External>);
static_assert(!ffc::has_edge_from<^^tags::admitted_retags, source::CipherPath>());
static_assert(!ffc::has_edge_to<^^tags::admitted_retags, source::CipherPath>());

// The architecture pins: Portable weakens into a concrete trunk, a
// concrete trunk never widens back or relabels.
static_assert(RetagAllowed<source::PortablePinned, source::X86Pinned>);
static_assert(RetagAllowed<source::PortablePinned, source::ArmPinned>);
static_assert(!RetagAllowed<source::X86Pinned, source::PortablePinned>);
static_assert(!RetagAllowed<source::X86Pinned, source::ArmPinned>);
static_assert(!RetagAllowed<source::ArmPinned, source::X86Pinned>);

// The vessel boundary edge that TraceRing consumes.
static_assert(RetagAllowed<vessel_trust::FromPytorch, vessel_trust::Validated>);
static_assert(!RetagAllowed<vessel_trust::Validated, vessel_trust::FromPytorch>);

// ── The catalog, read out of the namespace ──────────────────────────
//
// The header pins the count.  This TU derives everything else from the
// namespace: every edge is admitted, no edge has its inverse, no edge
// crosses a family, and every edge actually moves a value.

static_assert(::fixy::admitted_retag_count == ffc::edge_count<^^tags::admitted_retags>());
static_assert(ffc::every_edge_is_admitted<^^tags::admitted_retags>());
static_assert(ffc::is_antisymmetric<^^tags::admitted_retags>());
static_assert(ffc::is_intra_namespace<^^tags::admitted_retags>());

// The pin in the header is against the edges of the three families the
// catalog spans; the split by family is derived here so a review of
// the count can see where an edge landed.
[[nodiscard]] consteval std::size_t edges_in_family(std::meta::info family) noexcept {
    std::size_t count = 0;
    for (const auto m : std::meta::members_of(^^tags::admitted_retags, std::meta::access_context::unchecked())) {
        if (ffc::is_edge(m) && ffc::family_of(ffc::ends_of(m).from) == family) ++count;
    }
    return count;
}

// A namespace alias reflects as the alias, so the families are named
// through the namespace itself.
static_assert(edges_in_family(^^::fixy::tags::trust) == 5);
static_assert(edges_in_family(^^::fixy::tags::source) == 16);
static_assert(edges_in_family(^^::fixy::tags::vessel_trust) == 1);
static_assert(edges_in_family(^^::fixy::tags::trust) + edges_in_family(^^::fixy::tags::source)
                  + edges_in_family(^^::fixy::tags::vessel_trust)
              == ::fixy::admitted_retag_count);

// Every admitted edge retags a minted value, through the member door
// and through the free door, and the inverse of every edge is refused
// by the concept the doors consult.
[[nodiscard]] consteval bool every_edge_moves_a_value() noexcept {
    static constexpr auto members = std::define_static_array(
        std::meta::members_of(^^tags::admitted_retags, std::meta::access_context::unchecked()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto m : members) {
        if constexpr (ffc::is_edge(m)) {
            constexpr auto ends = ffc::ends_of(m);
            using From = typename[:ends.from:];
            using To = typename[:ends.to:];
            static_assert(RetagAllowed<From, To>);
            static_assert(!RetagAllowed<To, From>);
            Tagged<int, To> by_member = std::move(mint_tagged<From>(3)).template retag<To>();
            if (by_member.value() != 3) return false;
            Tagged<int, To> by_free = ::fixy::retag<To>(mint_tagged<From>(4));
            if (by_free.value() != 4) return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}

static_assert(every_edge_moves_a_value());

// ── Runtime cells ───────────────────────────────────────────────────

void apply_sanitized(Tagged<Config, source::Sanitized> cfg) { assert(cfg.value().port == 8080); }

int check_retag_route() {
    Tagged<Config, source::External> raw = mint_tagged<source::External>(Config{8080});
    // Passing raw straight to apply_sanitized does not compile: the tags
    // differ, so the retag below is the only route in.
    auto sanitized = std::move(raw).retag<source::Sanitized>();
    apply_sanitized(std::move(sanitized));

    Tagged<int, trust::Verified> v = mint_tagged<trust::Verified>(42);
    Tagged<int, access::RO> ro = mint_tagged<access::RO>(99);
    Tagged<int, version::V<3>> vv = mint_tagged<version::V<3>>(7);
    if (v.value() != 42 || ro.value() != 99 || vv.value() != 7) return 10;
    if (version::V<3>::number != 3) return 11;
    return 0;
}

int check_arch_weakening() {
    int probe = 0;
    for (int loop = 0; loop < 3; ++loop)
        probe += loop;  // not foldable to a literal

    Tagged<int, source::PortablePinned> portable = mint_tagged<source::PortablePinned>(probe);
    if (portable.value() != probe) return 20;

    // Portable to x86 is the admitted weakening direction.
    auto x86 = std::move(portable).retag<source::X86Pinned>();
    if (x86.value() != probe) return 21;
    return 0;
}

int check_value_and_into() {
    Tagged<int, VerificationTag> t = mint_tagged<VerificationTag>(99);
    if (t.value() != 99) return 30;
    int v = std::move(t).into();
    if (v != 99) return 31;

    // A copy carries the provenance with the value.
    Tagged<int, VerificationTag> original = mint_tagged<VerificationTag>(5);
    Tagged<int, VerificationTag> copy = original;
    if (copy.value() != original.value()) return 32;
    return 0;
}

int check_slot_array() {
    // The second door: a slot array starts empty, and a slot is filled
    // by assignment from a minted value.
    Tagged<const int*, source::RegionOps> slots[4]{};
    for (const auto& slot : slots)
        if (slot.value() != nullptr) return 40;
    static const int cell = 1;
    slots[2] = mint_tagged<source::RegionOps>(&cell);
    if (slots[2].value() != &cell || slots[3].value() != nullptr) return 41;
    return 0;
}

// The doors the cells above do not walk: the mint overload that names
// the value type, the free-function retag beside the member one, and the
// vessel_trust edge.
int check_remaining_doors() {
    int seed = 7;

    Tagged<int, source::FromUser> deduced = mint_tagged<source::FromUser>(seed * 6);
    if (deduced.value() != 42) return 50;

    // The same value through the overload that names the value type, so
    // a narrowing argument cannot pick the element type of the result.
    auto named = mint_tagged<source::FromUser, int>(seed * 6);
    if (named.value() != 42) return 51;
    static_assert(std::is_same_v<decltype(named), Tagged<int, source::FromUser>>);

    Tagged<int, source::Sanitized> sanitized = std::move(deduced).retag<source::Sanitized>();
    if (std::move(sanitized).into() != 42) return 52;

    // The two trust poles carry the same value; only the tag differs.
    Tagged<long, trust::Verified> verified = mint_tagged<trust::Verified>(static_cast<long>(seed * seed));
    Tagged<long, trust::Unverified> unverified = mint_tagged<trust::Unverified>(static_cast<long>(seed * seed));
    if (verified.value() != unverified.value()) return 53;

    Tagged<int, vessel_trust::FromPytorch> raw = mint_tagged<vessel_trust::FromPytorch>(seed);
    auto validated = std::move(raw).retag<vessel_trust::Validated>();
    if (validated.value() != 7) return 54;

    // The free-function door, beside the member one check_arch_weakening
    // walks.
    Tagged<int, source::PortablePinned> portable = mint_tagged<source::PortablePinned>(seed);
    auto x86 = ::fixy::retag<source::X86Pinned>(std::move(portable));
    if (x86.value() != 7) return 55;

    return 0;
}

}  // namespace

int main() {
    if (int rc = check_remaining_doors(); rc != 0) return rc;
    if (int rc = check_retag_route(); rc != 0) return rc;
    if (int rc = check_arch_weakening(); rc != 0) return rc;
    if (int rc = check_value_and_into(); rc != 0) return rc;
    if (int rc = check_slot_array(); rc != 0) return rc;

    return 0;
}

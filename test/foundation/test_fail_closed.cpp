// Sentinel TU for the namespace-member relation: what it admits, what
// it skips, and what it refuses.

#include <foundation/diag/FailClosed.h>

#include <cstdlib>
#include <type_traits>

namespace {

namespace ffc = ::foundation::fail_closed;

struct Raw {};
struct Checked {};
struct Stored {};

// Two edges, one direction each.
namespace ingest {
inline constexpr ffc::edge<Raw, Checked> raw_to_checked{};
inline constexpr ffc::edge<Checked, Stored> checked_to_stored{};
}  // namespace ingest

static_assert(ffc::Admitted<^^ingest, Raw, Checked>);
static_assert(ffc::Admitted<^^ingest, Checked, Stored>);
// The reverse of an admitted pair.
static_assert(!ffc::Admitted<^^ingest, Checked, Raw>);
// A pair no edge names.
static_assert(!ffc::Admitted<^^ingest, Raw, Stored>);
// Identity is a pair like any other.
static_assert(!ffc::Admitted<^^ingest, Raw, Raw>);

// A member that is not an edge variable is skipped, whatever its kind.
namespace crowded {
inline constexpr ffc::edge<Raw, Stored> raw_to_stored{};
inline constexpr int width = 3;
inline constexpr Raw a_value_of_another_type{};
inline int count() { return width; }
struct Nested {};
enum class Mode : unsigned char {
    on
};
using Alias = int;
template <class T>
struct Box {};
// A variable template is a template, not a variable.
template <class T>
inline constexpr ffc::edge<T, T> loop{};
// One level down is not a member of crowded.
namespace inner {
inline constexpr ffc::edge<Stored, Raw> stored_to_raw{};
}  // namespace inner
}  // namespace crowded

static_assert(ffc::Admitted<^^crowded, Raw, Stored>);
static_assert(!ffc::Admitted<^^crowded, Raw, Raw>);
static_assert(!ffc::Admitted<^^crowded, Stored, Raw>);
static_assert(ffc::Admitted<^^crowded::inner, Stored, Raw>);

// An edge declared outside the relation's namespace is inert there.
namespace elsewhere {
inline constexpr ffc::edge<Checked, Raw> checked_to_raw{};
}  // namespace elsewhere

static_assert(ffc::Admitted<^^elsewhere, Checked, Raw>);
static_assert(!ffc::Admitted<^^ingest, Checked, Raw>);

// A relation with no edges admits nothing.
namespace vacant {}

static_assert(!ffc::Admitted<^^vacant, Raw, Checked>);

// The spelling of the declaration does not matter, only its type.
namespace spelled {
using RawToChecked = ffc::edge<Raw, Checked>;
inline constexpr RawToChecked via_alias{};
inline constexpr const ffc::edge<Checked, Stored> via_const{};
}  // namespace spelled

static_assert(ffc::Admitted<^^spelled, Raw, Checked>);
static_assert(ffc::Admitted<^^spelled, Checked, Stored>);

// An edge carries nothing.
static_assert(std::is_empty_v<ffc::edge<Raw, Checked>>);
static_assert(sizeof(ffc::edge<Raw, Checked>) == 1);

template <class From, class To>
    requires ffc::Admitted<^^ingest, From, To>
[[nodiscard]] constexpr bool transition(From const&, To const&) noexcept {
    return true;
}

// ── Properties of a whole relation ──────────────────────────────────

// is_edge answers for the member kind and the variable's type, and
// nothing else: an int, a value of another type, a function, a nested
// type, an enumeration, an alias, a variable template and a nested
// namespace are all not edges.
static_assert(ffc::is_edge(^^ingest::raw_to_checked));
static_assert(ffc::is_edge(^^spelled::via_alias));
static_assert(ffc::is_edge(^^spelled::via_const));
static_assert(!ffc::is_edge(^^crowded::width));
static_assert(!ffc::is_edge(^^crowded::a_value_of_another_type));
static_assert(!ffc::is_edge(^^crowded::count));
static_assert(!ffc::is_edge(^^crowded::Nested));
static_assert(!ffc::is_edge(^^crowded::Mode));
static_assert(!ffc::is_edge(^^crowded::Alias));
static_assert(!ffc::is_edge(^^crowded::inner));

// ends_of reads the two ends through their aliases.
static_assert(ffc::ends_of(^^ingest::raw_to_checked).from == ^^Raw);
static_assert(ffc::ends_of(^^ingest::raw_to_checked).to == ^^Checked);
static_assert(ffc::ends_of(^^spelled::via_alias).from == ^^Raw);
static_assert(ffc::ends_of(^^spelled::via_alias).to == ^^Checked);

// edge_count counts edges and skips every other member kind; a nested
// namespace is not opened.
static_assert(ffc::edge_count<^^ingest>() == 2);
static_assert(ffc::edge_count<^^crowded>() == 1);
static_assert(ffc::edge_count<^^crowded::inner>() == 1);
static_assert(ffc::edge_count<^^vacant>() == 0);
static_assert(ffc::edge_count<^^spelled>() == 2);

// every_edge_is_admitted: the enumeration and admits agree.
static_assert(ffc::every_edge_is_admitted<^^ingest>());
static_assert(ffc::every_edge_is_admitted<^^crowded>());
static_assert(ffc::every_edge_is_admitted<^^spelled>());
static_assert(ffc::every_edge_is_admitted<^^vacant>());

// is_antisymmetric: an inverse pair is refused, an identity edge is
// not an inverse of itself, and a relation with no edges holds.
namespace symmetric {
inline constexpr ffc::edge<Raw, Checked> raw_to_checked{};
inline constexpr ffc::edge<Checked, Raw> checked_to_raw{};
}  // namespace symmetric

namespace reflexive {
inline constexpr ffc::edge<Raw, Raw> raw_to_raw{};
inline constexpr ffc::edge<Raw, Checked> raw_to_checked{};
}  // namespace reflexive

static_assert(ffc::is_antisymmetric<^^ingest>());
static_assert(!ffc::is_antisymmetric<^^symmetric>());
static_assert(ffc::is_antisymmetric<^^reflexive>());
static_assert(ffc::is_antisymmetric<^^vacant>());

// The inverse is looked for through aliases too.
namespace symmetric_through_alias {
using Raw2 = Raw;
inline constexpr ffc::edge<Raw, Checked> raw_to_checked{};
inline constexpr ffc::edge<Checked, Raw2> checked_to_raw{};
}  // namespace symmetric_through_alias

static_assert(!ffc::is_antisymmetric<^^symmetric_through_alias>());

// is_intra_namespace: a family is the namespace that declares the
// type, and a specialization belongs to its template's family.
namespace family_a {
struct Left {};
struct Right {};
template <int N>
struct Pinned {};
using PinnedOne = Pinned<1>;
}  // namespace family_a

namespace family_b {
struct Other {};
}  // namespace family_b

namespace within_family {
inline constexpr ffc::edge<family_a::Left, family_a::Right> left_to_right{};
inline constexpr ffc::edge<family_a::PinnedOne, family_a::Pinned<2>> one_to_two{};
inline constexpr ffc::edge<family_a::Left, family_a::Pinned<2>> left_to_two{};
}  // namespace within_family

namespace across_families {
inline constexpr ffc::edge<family_a::Left, family_a::Right> left_to_right{};
inline constexpr ffc::edge<family_a::Left, family_b::Other> left_to_other{};
}  // namespace across_families

static_assert(ffc::family_of(^^family_a::Left) == ^^family_a);
static_assert(ffc::family_of(^^family_a::PinnedOne) == ^^family_a);
static_assert(ffc::family_of(^^family_a::Pinned<3>) == ^^family_a);
static_assert(ffc::family_of(^^family_b::Other) == ^^family_b);
static_assert(ffc::is_intra_namespace<^^within_family>());
static_assert(!ffc::is_intra_namespace<^^across_families>());
static_assert(ffc::is_intra_namespace<^^vacant>());

// has_edge_from and has_edge_to ask for one end and any other.
static_assert(ffc::has_edge_from<^^ingest, Raw>());
static_assert(ffc::has_edge_from<^^ingest, Checked>());
static_assert(!ffc::has_edge_from<^^ingest, Stored>());
static_assert(ffc::has_edge_to<^^ingest, Checked>());
static_assert(ffc::has_edge_to<^^ingest, Stored>());
static_assert(!ffc::has_edge_to<^^ingest, Raw>());
static_assert(ffc::has_edge_to<^^within_family, family_a::Pinned<2>>());
static_assert(ffc::has_edge_from<^^within_family, family_a::Pinned<1>>());
static_assert(!ffc::has_edge_from<^^vacant, Raw>());

// every_class_in_has_edge: each class declared directly in the tag
// namespace must sit at the named end of some edge, the excluded ones
// aside; templates, enumerations and aliases are skipped.
namespace tag_family {
struct Base {};
struct Alpha : Base {};
struct Beta : Base {};
struct Gamma : Base {};
template <class T>
struct Boxed {};
enum class Mode : unsigned char {
    on
};
using AlphaAlias = Alpha;
}  // namespace tag_family

namespace exits_from_base {
inline constexpr ffc::edge<tag_family::Base, tag_family::Alpha> to_alpha{};
inline constexpr ffc::edge<tag_family::Base, tag_family::Beta> to_beta{};
inline constexpr ffc::edge<tag_family::Base, tag_family::Gamma> to_gamma{};
}  // namespace exits_from_base

namespace exits_missing_gamma {
inline constexpr ffc::edge<tag_family::Base, tag_family::Alpha> to_alpha{};
inline constexpr ffc::edge<tag_family::Base, tag_family::Beta> to_beta{};
}  // namespace exits_missing_gamma

static_assert(ffc::every_class_in_has_edge<^^exits_from_base, ^^tag_family, ffc::EdgeEnd::To, tag_family::Base>());
static_assert(!ffc::every_class_in_has_edge<^^exits_from_base, ^^tag_family, ffc::EdgeEnd::To>());
static_assert(ffc::every_class_in_has_edge<^^exits_from_base, ^^tag_family, ffc::EdgeEnd::Either>());
static_assert(!ffc::every_class_in_has_edge<^^exits_from_base, ^^tag_family, ffc::EdgeEnd::From, tag_family::Base>());
static_assert(!ffc::every_class_in_has_edge<^^exits_missing_gamma, ^^tag_family, ffc::EdgeEnd::To, tag_family::Base>());
static_assert(ffc::every_class_in_has_edge<^^exits_missing_gamma, ^^tag_family, ffc::EdgeEnd::To, tag_family::Base,
                                           tag_family::Gamma>());
// A relation with no edges covers a namespace with no classes and
// nothing else.
static_assert(ffc::every_class_in_has_edge<^^vacant, ^^vacant, ffc::EdgeEnd::Either>());
static_assert(!ffc::every_class_in_has_edge<^^vacant, ^^tag_family, ffc::EdgeEnd::Either>());

// Keeps a compile-time answer from folding into the caller.
[[gnu::noipa]] bool as_runtime(bool value) noexcept { return value; }

}  // namespace

int main() {
    const bool admitted = as_runtime(ffc::admits<^^ingest, Raw, Checked>());
    const bool refused = as_runtime(ffc::admits<^^ingest, Checked, Raw>());
    if (!admitted || refused) std::abort();
    if (!as_runtime(transition(Raw{}, Checked{}))) std::abort();
    return 0;
}

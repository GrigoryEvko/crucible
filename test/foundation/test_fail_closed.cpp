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

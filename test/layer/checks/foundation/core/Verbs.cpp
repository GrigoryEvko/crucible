// The compile-time checks of foundation/core/Verbs.h.

#include <foundation/core/Verbs.h>

namespace foundation::core::detail::verbs_checks {

// The list of the probes below: two names of a family.
inline constexpr TextView probe_names{"is_full count"};

// A type that gives only verbs, language protocols and listed names.
class Closed {
    int held_ = 0;

public:
    constexpr Closed() noexcept = default;
    constexpr Closed(Closed const&) noexcept = default;
    constexpr Closed& operator=(Closed const&) noexcept = default;
    constexpr ~Closed() = default;

    [[nodiscard]] constexpr bool is_full() const noexcept { return held_ != 0; }
    [[nodiscard]] constexpr int count() const noexcept { return held_; }
    [[nodiscard]] constexpr int size() const noexcept { return held_; }
    template <class Arm>
    constexpr int match(Arm&& arm) const noexcept {
        return arm(held_);
    }
    [[nodiscard]] constexpr bool operator==(Closed const&) const noexcept = default;
};
CRUCIBLE_CLOSED_SURFACE(probe_names, Closed);

// A private member is outside the surface.
class PrivateName {
    [[nodiscard]] constexpr int has_value() const noexcept { return 0; }

public:
    [[nodiscard]] constexpr int count() const noexcept { return has_value(); }
};
CRUCIBLE_CLOSED_SURFACE(probe_names, PrivateName);

// A member with a name of no list is refused, and so is each other shape
// outside the lists.
struct OpenName {
    [[nodiscard]] constexpr bool has_value() const noexcept { return false; }
};
struct OpenData {
    int count = 0;
    int held = 0;
};
struct OpenType {
    using value_type = int;
};
struct OpenConversion {
    explicit constexpr operator bool() const noexcept { return false; }
};
struct OpenDereference {
    constexpr int operator*() const noexcept { return 0; }
};
struct OpenArrow {
    constexpr OpenName operator->() const noexcept { return OpenName{}; }
};
struct OpenIncrement {
    constexpr OpenIncrement& operator++() noexcept { return *this; }
};
struct OpenCall {
    constexpr int operator()() const noexcept { return 0; }
};
struct OpenPlus {
    constexpr int operator+(OpenPlus) const noexcept { return 0; }
};
static_assert(!closed_surface<OpenName>(probe_names).is_closed());
static_assert(!closed_surface<OpenData>(probe_names).is_closed());
static_assert(!closed_surface<OpenType>(probe_names).is_closed());
static_assert(!closed_surface<OpenConversion>(probe_names).is_closed());
static_assert(!closed_surface<OpenDereference>(probe_names).is_closed());
static_assert(!closed_surface<OpenArrow>(probe_names).is_closed());
static_assert(!closed_surface<OpenIncrement>(probe_names).is_closed());
static_assert(!closed_surface<OpenCall>(probe_names).is_closed());
static_assert(!closed_surface<OpenPlus>(probe_names).is_closed());

// A data member with a listed name is a listed name.
static_assert(!closed_surface<OpenData>(TextView{"count"}).is_closed());
static_assert(closed_surface<OpenData>(TextView{"count held"}).is_closed());

// A name matches a whole name of a list, and not a part of one.
static_assert(!closed_surface<OpenName>(TextView{"has_value_or has"}).is_closed());
static_assert(closed_surface<OpenName>(TextView{"has has_value"}).is_closed());

// The grants give the dereference and the cursor, and nothing else.
static_assert(closed_surface<OpenDereference, DereferenceProtocol>(probe_names).is_closed());
static_assert(closed_surface<OpenDereference, CursorProtocol>(probe_names).is_closed());
static_assert(closed_surface<OpenArrow, DereferenceProtocol>(probe_names).is_closed());
static_assert(!closed_surface<OpenArrow, CursorProtocol>(probe_names).is_closed());
static_assert(closed_surface<OpenIncrement, CursorProtocol>(probe_names).is_closed());
static_assert(!closed_surface<OpenIncrement, DereferenceProtocol>(probe_names).is_closed());
static_assert(!closed_surface<OpenCall, DereferenceProtocol, CursorProtocol>(probe_names).is_closed());

// A public base adds its surface, and a private base adds none.
struct PublicBase : OpenName {};
struct PrivateBase : private OpenName {};
static_assert(!closed_surface<PublicBase>(probe_names).is_closed());
static_assert(closed_surface<PrivateBase>(probe_names).is_closed());

// The verdict names the member and the type.
[[nodiscard]] consteval bool verdict_reads(SurfaceVerdict const& verdict, TextView expected) noexcept {
    return verdict.view() == expected;
}
static_assert(verdict_reads(closed_surface<OpenType>(probe_names),
                            "the public member foundation::core::detail::verbs_checks::OpenType::value_type {aka int} "
                            "of foundation::core::detail::verbs_checks::OpenType is no shared verb, no language "
                            "protocol and no name of the family list"));
static_assert(closed_surface<Closed>(probe_names).size() == 0);
static_assert(verdict_reads(closed_surface<Closed>(probe_names), ""));

// The verdict itself gives only its test, and the text that static_assert
// prints.
CRUCIBLE_CLOSED_SURFACE(TextView{"is_closed data"}, SurfaceVerdict);

}  // namespace foundation::core::detail::verbs_checks

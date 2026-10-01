// The compile-time checks of foundation/reflect/EnumPins.h.

#include <foundation/reflect/EnumPins.h>

namespace foundation::reflect {

// The walk answers no for each way a table and its enum can disagree.
// Without these, a pin_enum that answered yes for everything would leave
// every pin assertion green and pin nothing.
namespace detail::pin_enum_self_test {

enum class Probe : std::uint8_t {
    First = 0,
    Second = 1,
    Third = 2
};

inline constexpr std::array<enum_pin<Probe>, 3> correct{{{"First", 0}, {"Second", 1}, {"Third", 2}}};
static_assert(pin_enum(correct));

inline constexpr std::array<enum_pin<Probe>, 3> value_moved{{{"First", 0}, {"Second", 9}, {"Third", 2}}};
static_assert(!pin_enum(value_moved), "A moved value must be caught.");

inline constexpr std::array<enum_pin<Probe>, 3> renamed{{{"First", 0}, {"Deuxieme", 1}, {"Third", 2}}};
static_assert(!pin_enum(renamed), "A renamed enumerator must be caught.");

inline constexpr std::array<enum_pin<Probe>, 2> entry_missing{{{"First", 0}, {"Second", 1}}};
static_assert(!pin_enum(entry_missing), "An enumerator the table does not name must be caught.");

inline constexpr std::array<enum_pin<Probe>, 4> entry_extra{{{"First", 0}, {"Second", 1}, {"Third", 2}, {"Fourth", 3}}};
static_assert(!pin_enum(entry_extra), "An entry no enumerator answers to must be caught.");

inline constexpr std::array<enum_pin<Probe>, 3> duplicated{{{"First", 0}, {"First", 0}, {"Third", 2}}};
static_assert(!pin_enum(duplicated), "A table that names one enumerator twice must be caught, because "
                                     "the sizes still agree and one entry goes unread.");

// A wide enum and a signed one pin the same way, at their full width.
enum class WideProbe : std::uint32_t {
    Low = 1u << 0,
    High = 1u << 31
};

inline constexpr std::array<enum_pin<WideProbe>, 2> wide_correct{{{"Low", 1u << 0}, {"High", 1u << 31}}};
static_assert(pin_enum(wide_correct));

inline constexpr std::array<enum_pin<WideProbe>, 2> wide_truncated{{{"Low", 1u << 0}, {"High", 1u << 7}}};
static_assert(!pin_enum(wide_truncated), "A value that differs above the low byte must be caught.");

enum class SignedProbe : std::int16_t {
    Negative = -300,
    Positive = 300
};

inline constexpr std::array<enum_pin<SignedProbe>, 2> signed_correct{{{"Negative", -300}, {"Positive", 300}}};
static_assert(pin_enum(signed_correct));

inline constexpr std::array<enum_pin<SignedProbe>, 2> signed_flipped{{{"Negative", 300}, {"Positive", -300}}};
static_assert(!pin_enum(signed_flipped), "A sign flip must be caught.");

}  // namespace detail::pin_enum_self_test

}  // namespace foundation::reflect

// The compile-time checks of foundation/diag/JsonEmitter.h.

#include <foundation/diag/JsonEmitter.h>

#include <foundation/core/Array.h>

namespace foundation::diag {

namespace detail {

static_assert(record_fields_are<SourcePosition>(source_position_fields),
              "SourcePosition gained, lost or renamed a field.  The nested source_position object in "
              "write_json_record writes each field by name, so a new one is absent from the output until "
              "it is written there and added to source_position_fields.");

static_assert(record_fields_are<JsonDiagnosticRecord>(json_record_fields),
              "JsonDiagnosticRecord gained, lost or renamed a field.  write_json_record writes each field "
              "by name, so a new one is absent from the output until it is written there and added to "
              "json_record_fields.  Renaming or removing one changes an external contract and needs a "
              "format version.");

// The roster answers no for each way it and its record can disagree.
// Without these, a record_fields_are that answered yes to everything
// would leave both assertions above green and pin nothing.
namespace record_roster_self_test {

struct Probe {
    int first = 0;
    int second = 0;
};

using core::FixedArray;
using core::TextView;

inline constexpr FixedArray<TextView, 2> correct{TextView{"first"}, TextView{"second"}};
static_assert(record_fields_are<Probe>(correct.view()));

inline constexpr FixedArray<TextView, 2> renamed{TextView{"first"}, TextView{"deuxieme"}};
static_assert(!record_fields_are<Probe>(renamed.view()), "A renamed field must be caught.");

inline constexpr FixedArray<TextView, 2> reordered{TextView{"second"}, TextView{"first"}};
static_assert(!record_fields_are<Probe>(reordered.view()), "A reordered roster must be caught, because the writer "
                                                           "emits in declaration order.");

inline constexpr FixedArray<TextView, 1> too_short{TextView{"first"}};
static_assert(!record_fields_are<Probe>(too_short.view()), "A field the roster does not name must be caught.");

inline constexpr FixedArray<TextView, 3> too_long{TextView{"first"}, TextView{"second"}, TextView{"third"}};
static_assert(!record_fields_are<Probe>(too_long.view()), "A roster entry no field answers to must be caught.");

}  // namespace record_roster_self_test

// The search from the end gives the last position of the character, or no
// value when the text holds none.
static_assert(find_last_<':'>("a:b:c").value_or(99) == 3);
static_assert(find_last_<':'>(":").value_or(99) == 0);
static_assert(find_last_<':'>("abc").is_none());
static_assert(find_last_<':'>("").is_none());

// The reader of a number takes decimal digits only, and refuses a value past
// the largest value of std::uint_least32_t.
static_assert(parse_u32("42").value_or(0) == 42);
static_assert(parse_u32("4294967295").value_or(0) == 4294967295U);
static_assert(parse_u32("4294967296").is_none());
static_assert(parse_u32("").is_none());
static_assert(parse_u32("4x").is_none());
static_assert(parse_u32("+4").is_none());
static_assert(all_decimal_digits("0123"));
static_assert(!all_decimal_digits(""));
static_assert(!all_decimal_digits("12a"));

}  // namespace detail

}  // namespace foundation::diag

// The compile-time checks of foundation/diag/JsonEmitter.h.

#include <foundation/diag/JsonEmitter.h>

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

inline constexpr std::array<std::string_view, 2> correct{"first", "second"};
static_assert(record_fields_are<Probe>(correct));

inline constexpr std::array<std::string_view, 2> renamed{"first", "deuxieme"};
static_assert(!record_fields_are<Probe>(renamed), "A renamed field must be caught.");

inline constexpr std::array<std::string_view, 2> reordered{"second", "first"};
static_assert(!record_fields_are<Probe>(reordered), "A reordered roster must be caught, because the writer "
                                                    "emits in declaration order.");

inline constexpr std::array<std::string_view, 1> too_short{"first"};
static_assert(!record_fields_are<Probe>(too_short), "A field the roster does not name must be caught.");

inline constexpr std::array<std::string_view, 3> too_long{"first", "second", "third"};
static_assert(!record_fields_are<Probe>(too_long), "A roster entry no field answers to must be caught.");

}  // namespace record_roster_self_test

}  // namespace detail

}  // namespace foundation::diag

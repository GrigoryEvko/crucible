// Sentinel TU for fixy/Insights.h: every axis has a provider on its
// duplicate tag, the provider's text names the axis and the strict pole
// that Axis.h declares, no generated text names a pole the table does
// not have, the six corpus entries are insighted at Fatal, and the
// strings exist at runtime.
//
// The pole name is spliced here a second time, from the table and by
// reflection, so the check is against Axis.h and not against the
// header's own helper.  A provider whose text named a default the
// table does not carry would fail here whatever the header's helper
// says.

#include <fixy/Insights.h>

#include <fixy/Axis.h>
#include <fixy/Corpus.h>
#include <fixy/Reject.h>
#include <foundation/core/Algorithms.h>
#include <foundation/diag/Insights.h>
#include <foundation/reflect/EnumName.h>

#include <cstddef>
#include <meta>
#include <string>
#include <type_traits>

namespace {

namespace fd = ::foundation::diag;
using ::fixy::Axis;
using ::fixy::axis_name;
using ::fixy::strict_pole_t;
using ::fixy::duplicate_atom_on;
using ::foundation::core::find;
using ::foundation::core::mint_static_text;
using ::foundation::core::starts_with;
using ::foundation::core::TextView;

// ---------------------------------------------------------------------
// The independent splice.  Three shapes: a value pole named by its
// value, a specialisation named by its template, a class named by
// itself; and the caller-supplied axis, which has no pole.  Each text
// that reflection or a static string gives has static storage and a zero
// after its last character, so the static text door gives it as a
// TextView.

[[nodiscard]] consteval TextView digits(unsigned long long value) noexcept {
    std::string text;
    do {
        text.insert(text.begin(), static_cast<char>('0' + static_cast<int>(value % 10)));
        value /= 10;
    } while (value != 0);
    return mint_static_text(std::define_static_string(text));
}

// The identifier of the enumerator that holds Value, read by reflection, so
// the expected name does not come from a name form of the base.
template <auto Value>
[[nodiscard]] consteval TextView identifier_of_value() noexcept {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : std::define_static_array(std::meta::enumerators_of(^^decltype(Value)))) {
        if ([:en:] == Value) return mint_static_text(std::meta::identifier_of(en).data());
    }
#pragma GCC diagnostic pop
    return {};
}

template <Axis A>
[[nodiscard]] consteval TextView expected_pole_name() noexcept {
    if constexpr (::fixy::IsCallerSupplied<A>) {
        return "the payload type the binding names";
    } else {
        using Strict = strict_pole_t<A>;
        if constexpr (requires { Strict::value; }) {
            constexpr auto value = Strict::value;
            if constexpr (std::is_enum_v<decltype(value)>) {
                return identifier_of_value<value>();
            } else {
                return digits(static_cast<unsigned long long>(value));
            }
        } else if constexpr (std::meta::has_template_arguments(std::meta::dealias(^^Strict))) {
            return mint_static_text(
                std::meta::identifier_of(std::meta::template_of(std::meta::dealias(^^Strict))).data());
        } else {
            return mint_static_text(std::meta::identifier_of(std::meta::dealias(^^Strict)).data());
        }
    }
}

// The splice pinned on one pole of each shape, so the walk below is
// not vacuous.
static_assert(expected_pole_name<Axis::Usage>() == "One");
static_assert(expected_pole_name<Axis::Security>() == "Secret");
static_assert(expected_pole_name<Axis::Overflow>() == "Trap");
static_assert(expected_pole_name<Axis::Version>() == "Unconstrained");
static_assert(expected_pole_name<Axis::Refinement>() == "True");
static_assert(expected_pole_name<Axis::Lifetime>() == "Unconstrained");
static_assert(expected_pole_name<Axis::Trust>() == "Unverified");
static_assert(expected_pole_name<Axis::Effect>() == "Row");
static_assert(expected_pole_name<Axis::Observability>() == "Row");
static_assert(expected_pole_name<Axis::Synchronization>() == "Unconstrained");
static_assert(expected_pole_name<Axis::Regime>() == "Unconstrained");
static_assert(expected_pole_name<Axis::Type>() == "the payload type the binding names");

// ---------------------------------------------------------------------
// Every axis has a provider that names the axis and its pole.  The walk
// is over the enum, so an axis added to it is covered the day it is
// declared, and the count of providers is the count of axes.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
[[nodiscard]] consteval std::size_t axes_with_a_provider_naming_their_pole() noexcept {
    std::size_t counted = 0;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        using Tag = duplicate_atom_on<axis>;
        using Provider = fd::insight_provider<Tag>;
        const bool provided = fd::has_insights_v<Tag> && fd::has_substantive_insights_v<Tag>
                           && fd::HasSubstantiveInsights<Tag> && fd::WellInsightedTag<Tag>
                           && Provider::severity == fd::Severity::Error
                           && find(Provider::why_this_matters, axis_name(axis)).is_some()
                           && find(Provider::why_this_matters, expected_pole_name<axis>()).is_some()
                           && find(Provider::symptom_pattern, axis_name(axis)).is_some()
                           && find(Provider::correct_example, expected_pole_name<axis>()).is_some()
                           && starts_with(Provider::correct_example, "fixy::fn<")
                           && find(Provider::violating_example, axis_name(axis)).is_some()
                           && starts_with(Provider::violating_example, "fixy::fn<");
        if (provided) ++counted;
    }
    return counted;
}
#pragma GCC diagnostic pop

static_assert(axes_with_a_provider_naming_their_pole() == ::fixy::axis_count,
              "an axis has no substantive provider on its duplicate tag, or the provider's text does not "
              "name the axis and the strict pole Axis.h declares");

// Two axes' texts differ where the table differs.
static_assert(fd::insight_provider<duplicate_atom_on<Axis::Usage>>::why_this_matters
              != fd::insight_provider<duplicate_atom_on<Axis::Effect>>::why_this_matters);
static_assert(fd::insight_provider<duplicate_atom_on<Axis::Usage>>::correct_example
              != fd::insight_provider<duplicate_atom_on<Axis::Security>>::correct_example);

// A tag this header does not describe reads as uninsighted, so the
// providers above are the partial specialisation and not a primary
// that answers for everything.
struct undescribed_tag : fd::tag_base {
    static constexpr fd::tag_text_literal name = "Undescribed";
    static constexpr fd::tag_text_literal description = "fixture";
    static constexpr fd::tag_text_literal remediation = "fixture";
};
static_assert(!fd::has_insights_v<undescribed_tag>);

// ---------------------------------------------------------------------
// The six corpus entries are insighted at Fatal, and their examples
// cite policies that exist.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
[[nodiscard]] consteval std::size_t corpus_entries_insighted_at_fatal() noexcept {
    std::size_t counted = 0;
    template for (constexpr auto entry : std::define_static_array(
                      std::meta::template_arguments_of(std::meta::dealias(^^::fixy::corpus::Entries)))) {
        using Entry = [:entry:];
        using Provider = fd::insight_provider<Entry>;
        const bool provided = fd::HasSubstantiveInsights<Entry> && Provider::severity == fd::Severity::Fatal
                           && starts_with(Provider::correct_example, "fixy::fn<")
                           && starts_with(Provider::violating_example, "fixy::fn<");
        if (provided) ++counted;
    }
    return counted;
}
#pragma GCC diagnostic pop

static_assert(corpus_entries_insighted_at_fatal() == ::fixy::corpus::corpus_size);

// The entries whose why field is the citation say what the tag says.
static_assert(fd::insight_provider<::fixy::corpus::staleness_secret_without_declassify>::why_this_matters
              == ::fixy::corpus::staleness_secret_without_declassify::cite());
static_assert(fd::insight_provider<::fixy::corpus::ghost_runtime_observable>::why_this_matters
              == ::fixy::corpus::ghost_runtime_observable::cite());
static_assert(fd::insight_provider<::fixy::corpus::internal_bg_without_declassify>::why_this_matters
              == ::fixy::corpus::internal_bg_without_declassify::cite());

// The policies the examples name are declassification policies.
static_assert(::fixy::atom::IsDeclassificationPolicy<::fixy::tags::secret_policy::WireSerialize>);
static_assert(::fixy::atom::IsDeclassificationPolicy<::fixy::tags::secret_policy::AuthorizedReplay>);
static_assert(find(fd::insight_provider<::fixy::corpus::classified_io_without_declassify>::correct_example,
                   "WireSerialize")
                  .is_some());
static_assert(find(fd::insight_provider<::fixy::corpus::staleness_secret_without_declassify>::correct_example,
                   "AuthorizedReplay")
                  .is_some());

// ---------------------------------------------------------------------
// A static_assert proves the constant-evaluated path only.  These run.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
[[nodiscard]] int check_runtime_paths() {
    std::size_t walked = 0;
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        using Provider = fd::insight_provider<duplicate_atom_on<axis>>;
        TextView const why = Provider::why_this_matters;
        TextView const symptom = Provider::symptom_pattern;
        TextView const correct = Provider::correct_example;
        TextView const violating = Provider::violating_example;
        if (why.size() == 0 || symptom.size() == 0 || correct.size() == 0 || violating.size() == 0) return 1;
        if (find(why, axis_name(axis)).is_none()) return 2;
        ++walked;
    }
    if (walked != ::fixy::axis_count) return 3;

    TextView const fatal_why = fd::insight_provider<::fixy::corpus::classified_io_without_declassify>::why_this_matters;
    if (fatal_why.size() == 0) return 4;
    if (fd::severity_name(fd::insight_provider<::fixy::corpus::classified_io_without_declassify>::severity)
        != "Fatal") {
        return 5;
    }
    return 0;
}
#pragma GCC diagnostic pop

}  // namespace

int main() {
    if (int rc = check_runtime_paths(); rc != 0) return rc;
    return 0;
}

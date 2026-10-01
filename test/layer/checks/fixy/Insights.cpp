// The compile-time checks of fixy/Insights.h.

#include <fixy/Insights.h>

// The four policies below are named verbatim inside the inert strings of
// fixy/Insights.h, which no compiler ever checks.  These pins put a
// rename or a removal in front of the reader who has to update those
// strings.  They live in the check file of the citing header rather than
// beside the policy definitions, because the citation is what breaks.
static_assert(::fixy::atom::IsDeclassificationPolicy<::fixy::tags::secret_policy::WireSerialize>,
              "tags::secret_policy::WireSerialize must exist and be a declassification policy: it is cited "
              "verbatim in the correct_example of classified_io_without_declassify above.");
static_assert(::fixy::atom::IsDeclassificationPolicy<::fixy::tags::secret_policy::AuthorizedReplay>,
              "tags::secret_policy::AuthorizedReplay must exist and be a declassification policy: it is cited "
              "verbatim in the correct_example of staleness_secret_without_declassify above, and it is the "
              "one policy that discharges Staleness.");
static_assert(::fixy::atom::IsDeclassificationPolicy<::fixy::tags::secret_policy::AuditedLogging>,
              "tags::secret_policy::AuditedLogging must exist and be a declassification policy: the roles and "
              "the corpus self-tests spell it, and the why_this_matters of classified_io_without_declassify "
              "above names it.");
static_assert(::fixy::atom::IsDeclassificationPolicy<::fixy::tags::secret_policy::UserDisplay>,
              "tags::secret_policy::UserDisplay must exist and be a declassification policy: the "
              "why_this_matters of classified_io_without_declassify above names it.");

namespace fixy::insights::detail::insights_self_test {

// Every axis has a provider, it clears the substance floor, and its
// text names the axis and the strict pole the table declares.
[[nodiscard]] consteval bool every_axis_has_a_provider_() noexcept {
    bool all_provided = true;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto axis_member : std::define_static_array(std::meta::enumerators_of(^^::fixy::Axis))) {
        constexpr Axis axis = [:axis_member:];
        using Tag = ::fixy::duplicate_atom_on<axis>;
        using Provider = ::foundation::diag::insight_provider<Tag>;
        all_provided = all_provided && ::foundation::diag::HasSubstantiveInsights<Tag>
                    && ::fixy::detail::text_contains(Provider::why_this_matters, ::fixy::axis_name(axis))
                    && ::fixy::detail::text_contains(Provider::why_this_matters, strict_pole_name_<axis>())
                    && ::fixy::detail::text_contains(Provider::violating_example, ::fixy::axis_name(axis));
    }
#pragma GCC diagnostic pop
    return all_provided;
}

static_assert(every_axis_has_a_provider_(),
              "fixy/Insights.h: an axis has no substantive insight provider on its duplicate tag, or the "
              "provider's text does not name the axis and its strict pole.");

// The pole names, one per shape the table carries: an enum value, an
// integer value, a plain class, a specialisation, and the caller-
// supplied axis.
static_assert(strict_pole_name_<Axis::Usage>() == "One");
static_assert(strict_pole_name_<Axis::Security>() == "Secret");
static_assert(strict_pole_name_<Axis::Version>() == "Unconstrained");
static_assert(strict_pole_name_<Axis::Refinement>() == "True");
static_assert(strict_pole_name_<Axis::Effect>() == "Row");
static_assert(strict_pole_name_<Axis::Regime>() == "Unconstrained");
static_assert(strict_pole_name_<Axis::Type>() == "the payload type the binding names");

// The six corpus entries are insighted, at Fatal.
[[nodiscard]] consteval bool every_corpus_entry_is_insighted_() noexcept {
    bool all_insighted = true;
    static constexpr auto entries =
        std::define_static_array(std::meta::template_arguments_of(std::meta::dealias(^^::fixy::corpus::Entries)));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto entry : entries) {
        using Entry = [:entry:];
        all_insighted = all_insighted && ::foundation::diag::HasSubstantiveInsights<Entry>
                     && ::foundation::diag::insight_provider<Entry>::severity == ::foundation::diag::Severity::Fatal;
    }
#pragma GCC diagnostic pop
    return all_insighted;
}

static_assert(every_corpus_entry_is_insighted_(),
              "fixy/Insights.h: a corpus entry has no insight provider, or one below the substance floor, or "
              "one that is not Fatal.");

}  // namespace fixy::insights::detail::insights_self_test

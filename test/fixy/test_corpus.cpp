// Sentinel TU for fixy/Corpus.h: each of the six entries matches its
// own witness pack and none of the other five, the corpus is the tuple
// it says it is, the gate reaches it, the discharge masks are what the
// policies say, and the strings exist at runtime.
//
// The witnesses go through the entries' own matchers and through
// corpus::is_in_corpus_v rather than through fn, for the reason the
// collision test gives: fn would refuse to compile, and a refusal that
// is only observable as a build failure cannot be tested.  The gate is
// checked through IsAccepted, which is the concept fn asserts.

#include <fixy/Corpus.h>
#include <fixy/Reject.h>

#include <cstddef>
#include <meta>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace {

namespace at = ::fixy::atom;
namespace corpus = ::fixy::corpus;
namespace policy = ::fixy::tags::secret_policy;
using Eff = ::foundation::effects::Effect;
using ::fixy::IsAccepted;

// A text search for constant expressions.  string_view::find
// null-checks a pointer into the define_static_string object behind
// every generated text, which GCC 16.2's constant evaluator refuses;
// indexing is accepted.
[[nodiscard]] consteval bool contains(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.size() > haystack.size()) return false;
    for (std::size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        bool same = true;
        for (std::size_t offset = 0; offset < needle.size(); ++offset) {
            if (haystack[start + offset] != needle[offset]) {
                same = false;
                break;
            }
        }
        if (same) return true;
    }
    return false;
}

// ---------------------------------------------------------------------
// The corpus is the tuple it says it is.  The literal sits on the right
// so the tuple, not the count, is what the assertion reads.

static_assert(std::tuple_size_v<corpus::Entries> == 6);
static_assert(corpus::corpus_size == std::tuple_size_v<corpus::Entries>);

// ---------------------------------------------------------------------
// One witness per entry, unique to it.  The walk asks every other entry
// about the same pack, so a witness that two entries match is caught
// rather than counted for the one it was written for.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
template <class Entry, class... Atoms>
[[nodiscard]] consteval bool only_this_entry_matches() noexcept {
    const bool self = Entry::template matches<int, Atoms...>();
    bool others = false;
    template for (constexpr auto entry :
                  std::define_static_array(std::meta::template_arguments_of(std::meta::dealias(^^corpus::Entries)))) {
        using Other = [:entry:];
        if constexpr (!std::is_same_v<Other, Entry>) {
            if (Other::template matches<int, Atoms...>()) others = true;
        }
    }
    return self && !others;
}
#pragma GCC diagnostic pop

static_assert(only_this_entry_matches<corpus::classified_io_without_declassify, at::as_classified, at::with_io>());
static_assert(only_this_entry_matches<corpus::classified_bg_without_declassify, at::as_secret, at::with_bg>());
static_assert(only_this_entry_matches<corpus::staleness_secret_without_declassify, at::as_secret, at::stale_to<5>>());
static_assert(only_this_entry_matches<corpus::ghost_runtime_observable, at::ghost, at::as_public, at::with_alloc>());
static_assert(only_this_entry_matches<corpus::internal_io_without_declassify, at::as_internal, at::with_io>());
static_assert(only_this_entry_matches<corpus::internal_bg_without_declassify, at::as_internal, at::with_bg>());

// ---------------------------------------------------------------------
// Reject by default on the Security axis: the strict pole is classified,
// so an effect row with no Security atom is a classified value on an
// observable channel.

static_assert(corpus::classified_io_without_declassify::matches<int, at::with_io>());
static_assert(corpus::classified_bg_without_declassify::matches<int, at::with_bg>());
static_assert(corpus::staleness_secret_without_declassify::matches<int, at::stale_to<100>>());

// The public and unclassified grades sit below the carrier.
static_assert(!corpus::classified_io_without_declassify::matches<int, at::as_public, at::with_io>());
static_assert(!corpus::classified_io_without_declassify::matches<int, at::as_unclassified, at::with_io>());
static_assert(!corpus::classified_bg_without_declassify::matches<int, at::as_public, at::with_bg>());

// A declassification displaces the carrier: the emission is licensed.
static_assert(!corpus::classified_io_without_declassify::matches<int, at::declassify<policy::WireSerialize>, at::with_io>());
static_assert(!corpus::classified_bg_without_declassify::matches<int, at::declassify<policy::AuditedLogging>, at::with_bg>());

// Internal is not classified, and classified is not internal.
static_assert(!corpus::classified_io_without_declassify::matches<int, at::as_internal, at::with_io>());
static_assert(!corpus::internal_io_without_declassify::matches<int, at::as_classified, at::with_io>());
static_assert(!corpus::internal_io_without_declassify::matches<int, at::with_io>());

// Each IO or Bg entry needs its own effect.
static_assert(!corpus::classified_io_without_declassify::matches<int, at::as_secret>());
static_assert(!corpus::classified_io_without_declassify::matches<int, at::as_secret, at::with_bg>());
static_assert(!corpus::classified_bg_without_declassify::matches<int, at::as_secret, at::with_io>());
static_assert(!corpus::internal_bg_without_declassify::matches<int, at::as_internal, at::with_alloc>());
static_assert(!corpus::internal_io_without_declassify::matches<int, at::as_internal, at::with<>>());

// A row that names several effects is read for each.
static_assert(corpus::classified_io_without_declassify::matches<int, at::as_secret, at::with<Eff::Bg, Eff::Alloc, Eff::IO>>());
static_assert(corpus::classified_bg_without_declassify::matches<int, at::as_secret, at::with<Eff::Bg, Eff::Alloc, Eff::IO>>());

// ---------------------------------------------------------------------
// Staleness: the discharge must match the axis.  A value declassified
// for export is still a secret where replay is concerned.

static_assert(corpus::staleness_secret_without_declassify::matches<int, at::as_secret, at::stale_to<5>>());
static_assert(corpus::staleness_secret_without_declassify::matches<int, at::as_classified, at::stale_to<5>>());
static_assert(corpus::staleness_secret_without_declassify::matches<int, at::declassify<policy::AuditedLogging>, at::stale_to<5>>());
static_assert(corpus::staleness_secret_without_declassify::matches<int, at::declassify<policy::WireSerialize>, at::stale_to<5>>());
static_assert(!corpus::staleness_secret_without_declassify::matches<int, at::declassify<policy::AuthorizedReplay>, at::stale_to<5>>());
static_assert(!corpus::staleness_secret_without_declassify::matches<int, at::as_public, at::stale_to<5>>());
static_assert(!corpus::staleness_secret_without_declassify::matches<int, at::as_internal, at::stale_to<5>>());
static_assert(!corpus::staleness_secret_without_declassify::matches<int, at::as_secret>());

// ---------------------------------------------------------------------
// Ghost: the observable set is the one foundation declares.

static_assert(corpus::ghost_runtime_observable::matches<int, at::ghost, at::with_io>());
static_assert(corpus::ghost_runtime_observable::matches<int, at::ghost, at::with_bg>());
static_assert(corpus::ghost_runtime_observable::matches<int, at::ghost, at::with_block>());
static_assert(corpus::ghost_runtime_observable::matches<int, at::ghost, at::with_alloc>());
static_assert(!corpus::ghost_runtime_observable::matches<int, at::ghost, at::with_init>());
static_assert(!corpus::ghost_runtime_observable::matches<int, at::ghost, at::with_test>());
static_assert(!corpus::ghost_runtime_observable::matches<int, at::ghost, at::with<>>());
static_assert(!corpus::ghost_runtime_observable::matches<int, at::ghost>());
static_assert(!corpus::ghost_runtime_observable::matches<int, at::as_public, at::with_io>());
static_assert(!corpus::ghost_runtime_observable::matches<int, at::affine, at::as_public, at::with_io>());

// ---------------------------------------------------------------------
// The discharge masks are what the policies say, and exactly one policy
// says anything.

static_assert(corpus::axes_discharged_of_v<policy::AuthorizedReplay> == corpus::DischargeAxis::Staleness);
static_assert(corpus::axes_discharged_of_v<policy::AuditedLogging> == corpus::DischargeAxis::None);
static_assert(corpus::axes_discharged_of_v<policy::WireSerialize> == corpus::DischargeAxis::None);
static_assert(corpus::axes_discharged_of_v<policy::HashForCompare> == corpus::DischargeAxis::None);
static_assert(corpus::axes_discharged_of_v<policy::LengthOnly> == corpus::DischargeAxis::None);
static_assert(corpus::axes_discharged_of_v<policy::UserDisplay> == corpus::DischargeAxis::None);
static_assert(corpus::axes_discharged_of_v<int> == corpus::DischargeAxis::None, "a non-policy discharges nothing");

static_assert(corpus::discharge_axis_contains(corpus::DischargeAxis::Staleness | corpus::DischargeAxis::IO,
                                              corpus::DischargeAxis::IO));
static_assert(corpus::discharge_axis_contains(corpus::DischargeAxis::Staleness | corpus::DischargeAxis::IO,
                                              corpus::DischargeAxis::Staleness));
static_assert(!corpus::discharge_axis_contains(corpus::DischargeAxis::Staleness, corpus::DischargeAxis::IO));
static_assert(!corpus::discharge_axis_contains(corpus::DischargeAxis::None, corpus::DischargeAxis::Bg));
static_assert((corpus::DischargeAxis::Staleness & corpus::DischargeAxis::IO) == corpus::DischargeAxis::None);

// ---------------------------------------------------------------------
// The gate reaches the corpus, and reports the entry.

static_assert(!IsAccepted<int, at::with_io>);
static_assert(IsAccepted<int, at::with_io, at::as_public>);
static_assert(!IsAccepted<int, at::as_internal, at::with_bg>);
static_assert(!IsAccepted<int, at::as_secret, at::stale_to<5>>);
static_assert(IsAccepted<int, at::declassify<policy::AuthorizedReplay>, at::stale_to<5>>);

// Corpus-only refusals: no collision rule reads these pairs.  P010
// reads Alloc, IO and Block, so ghost x Bg on a public grade is the
// corpus's alone.
static_assert(!IsAccepted<int, at::ghost, at::as_public, at::with_bg>);
static_assert(IsAccepted<int, at::ghost, at::as_public>);
static_assert(IsAccepted<int, at::as_public, at::with_bg>);

static_assert(std::is_same_v<corpus::matched_entry_or_void_t<int, at::with_io>,
                             corpus::classified_io_without_declassify>);
static_assert(std::is_same_v<corpus::matched_entry_or_void_t<int, at::ghost, at::as_public, at::with_bg>,
                             corpus::ghost_runtime_observable>);
static_assert(std::is_same_v<corpus::matched_entry_or_void_t<int, at::with_io, at::as_public>, void>);
static_assert(std::is_same_v<corpus::matched_entry_or_void_t<int>, void>);

// A pack that matches two entries reports the first in tuple order.
static_assert(std::is_same_v<corpus::matched_entry_or_void_t<int, at::ghost, at::with_io>,
                             corpus::classified_io_without_declassify>);

static_assert(corpus::corpus_entry_name_for_v<int, at::with_io> == "classified_io_without_declassify");
static_assert(corpus::corpus_entry_name_for_v<int, at::as_internal, at::with_bg> == "internal_bg_without_declassify");
static_assert(corpus::corpus_entry_name_for_v<int>.empty());
static_assert(contains(corpus::corpus_full_diagnostic_v<int, at::with_io>, "classified_io_without_declassify"));
static_assert(contains(corpus::corpus_full_diagnostic_v<int, at::with_io>, "Sabelfeld-Myers 2003"));
static_assert(corpus::corpus_full_diagnostic_v<int, at::with_io>.starts_with("fixy::fn<Type, Atoms...> [tier 5"));
static_assert(corpus::corpus_full_diagnostic_v<int>.empty());

// ---------------------------------------------------------------------
// Every entry is a diagnostic tag named after itself, with a citation.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
[[nodiscard]] consteval bool every_entry_is_a_named_tag() noexcept {
    bool all_named = true;
    template for (constexpr auto entry :
                  std::define_static_array(std::meta::template_arguments_of(std::meta::dealias(^^corpus::Entries)))) {
        using Entry = [:entry:];
        // `^^Entry` would reflect the alias just declared, whose
        // identifier is the word Entry; the class is behind dealias.
        all_named = all_named && ::foundation::diag::is_diagnostic_class_v<Entry>
                 && Entry::name == std::meta::identifier_of(std::meta::dealias(^^Entry)) && !Entry::cite().empty()
                 && contains(Entry::full_diagnostic(), Entry::name);
    }
    return all_named;
}
#pragma GCC diagnostic pop

static_assert(every_entry_is_a_named_tag());

// ---------------------------------------------------------------------
// A static_assert proves the constant-evaluated path only.  These run.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
[[nodiscard]] int check_runtime_paths() {
    std::size_t walked = 0;
    template for (constexpr auto entry :
                  std::define_static_array(std::meta::template_arguments_of(std::meta::dealias(^^corpus::Entries)))) {
        using Entry = [:entry:];
        const std::string_view name = Entry::name;
        const std::string_view description = Entry::description;
        const std::string_view remediation = Entry::remediation;
        const std::string_view cite = Entry::cite();
        const std::string_view full = Entry::full_diagnostic();
        if (name.empty()) return 1;
        if (description.empty()) return 2;
        if (remediation.empty()) return 3;
        if (cite.empty()) return 4;
        if (!full.contains(name)) return 5;
        if (!full.contains(cite)) return 6;
        ++walked;
    }
    if (walked != corpus::corpus_size) return 7;

    const std::string_view matched = corpus::corpus_entry_name_for_v<int, at::with_io>;
    if (matched != "classified_io_without_declassify") return 8;
    if (!corpus::corpus_entry_name_for_v<int>.empty()) return 9;

    const auto mask = corpus::DischargeAxis::Staleness | corpus::DischargeAxis::Bg;
    if (!corpus::discharge_axis_contains(mask, corpus::DischargeAxis::Bg)) return 10;
    if (corpus::discharge_axis_contains(mask, corpus::DischargeAxis::IO)) return 11;
    return 0;
}
#pragma GCC diagnostic pop

}  // namespace

int main() {
    if (int rc = check_runtime_paths(); rc != 0) return rc;
    return 0;
}

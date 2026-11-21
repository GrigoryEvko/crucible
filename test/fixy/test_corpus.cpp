// Sentinel TU for fixy/Corpus.h: each of the six entries matches its
// own witness pack and none of the other five, the corpus is the tuple
// it says it is, the gate reaches it, the discharge masks are what the
// policies say, and the strings exist at runtime.
//
// The witnesses go through the entries' own matchers and through
// corpus::IsInCorpus rather than through fn, for the reason the
// collision test gives: fn would refuse to compile, and a refusal that
// is only observable as a build failure cannot be tested.  The gate is
// checked through IsAccepted, which is the concept fn asserts.

#include <fixy/Corpus.h>
#include <fixy/Reject.h>
#include <fixy/atoms/Stdio.h>
#include <fixy/atoms/Sync.h>
#include <fixy/atoms/Syscall.h>
#include <foundation/core/Algorithms.h>

#include <cstddef>
#include <meta>
#include <tuple>
#include <type_traits>

namespace {

namespace at = ::fixy::atom;
namespace corpus = ::fixy::corpus;
namespace policy = ::fixy::tags::secret_policy;
using Eff = ::foundation::effects::Effect;
using ::fixy::IsAccepted;
using ::foundation::core::find;
using ::foundation::core::mint_static_text;
using ::foundation::core::starts_with;

// ---------------------------------------------------------------------
// The corpus is the tuple it says it is.  The literal sits on the right
// so the tuple, not the count, is what the assertion reads.

static_assert(std::tuple_size_v<corpus::Entries> == 6);
static_assert(corpus::corpus_size == std::tuple_size_v<corpus::Entries>);

// Every entry declared under fixy::corpus is in the tuple, read from the
// vantage point of this translation unit.
struct corpus_test_site final {};
static_assert(corpus::corpus_is_joined<corpus_test_site>());

// ---------------------------------------------------------------------
// One witness per entry, unique to it.  The walk asks every other entry
// about the same pack, so a witness that two entries match is caught
// rather than counted for the one it was written for.

// Each matcher reads the premises of a binding, so the test asks it about
// the premises of the pack.  No matcher reads a premise of the payload.
template <class Entry, class... Atoms>
[[nodiscard]] consteval bool matches_pack() noexcept {
    return Entry::matches(::fixy::collision::pack_verdict_v<Atoms...>.premises);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
template <class Entry, class... Atoms>
[[nodiscard]] consteval bool only_this_entry_matches() noexcept {
    const bool self = matches_pack<Entry, Atoms...>();
    bool others = false;
    template for (constexpr auto entry :
                  std::define_static_array(std::meta::template_arguments_of(std::meta::dealias(^^corpus::Entries)))) {
        using Other = [:entry:];
        if constexpr (!std::is_same_v<Other, Entry>) {
            if (matches_pack<Other, Atoms...>()) others = true;
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

static_assert(matches_pack<corpus::classified_io_without_declassify, at::with_io>());
static_assert(matches_pack<corpus::classified_bg_without_declassify, at::with_bg>());
static_assert(matches_pack<corpus::staleness_secret_without_declassify, at::stale_to<100>>());

// The public and unclassified grades sit below the carrier.
static_assert(!matches_pack<corpus::classified_io_without_declassify, at::as_public, at::with_io>());
static_assert(!matches_pack<corpus::classified_io_without_declassify, at::as_unclassified, at::with_io>());
static_assert(!matches_pack<corpus::classified_bg_without_declassify, at::as_public, at::with_bg>());

// A declassification licenses the channels its policy names and no
// other.  Each export policy licenses IO.
static_assert(
    !matches_pack<corpus::classified_io_without_declassify, at::declassify<policy::WireSerialize>, at::with_io>());
static_assert(
    !matches_pack<corpus::classified_io_without_declassify, at::declassify<policy::AuditedLogging>, at::with_io>());
static_assert(
    !matches_pack<corpus::classified_io_without_declassify, at::declassify<policy::UserDisplay>, at::with_io>());
// A policy that names no channel, or another channel, leaves IO
// classified.
static_assert(only_this_entry_matches<corpus::classified_io_without_declassify,
                                      at::declassify<policy::AuthorizedReplay>, at::with_io>());
static_assert(only_this_entry_matches<corpus::classified_io_without_declassify, at::declassify<policy::LengthOnly>,
                                      at::with_io>());
static_assert(
    matches_pack<corpus::classified_io_without_declassify, at::declassify<policy::HashForCompare>, at::with_io>());
// No policy licenses Bg, so every declassification leaves Bg classified.
static_assert(only_this_entry_matches<corpus::classified_bg_without_declassify, at::declassify<policy::AuditedLogging>,
                                      at::with_bg>());
static_assert(
    matches_pack<corpus::classified_bg_without_declassify, at::declassify<policy::WireSerialize>, at::with_bg>());
static_assert(
    matches_pack<corpus::classified_bg_without_declassify, at::declassify<policy::LengthOnly>, at::with_bg>());
static_assert(
    matches_pack<corpus::classified_bg_without_declassify, at::declassify<policy::AuthorizedReplay>, at::with_bg>());

// Internal is not classified, and classified is not internal.
static_assert(!matches_pack<corpus::classified_io_without_declassify, at::as_internal, at::with_io>());
static_assert(!matches_pack<corpus::internal_io_without_declassify, at::as_classified, at::with_io>());
static_assert(!matches_pack<corpus::internal_io_without_declassify, at::with_io>());

// Each IO or Bg entry needs its own effect.
static_assert(!matches_pack<corpus::classified_io_without_declassify, at::as_secret>());
static_assert(!matches_pack<corpus::classified_io_without_declassify, at::as_secret, at::with_bg>());
static_assert(!matches_pack<corpus::classified_bg_without_declassify, at::as_secret, at::with_io>());
static_assert(!matches_pack<corpus::internal_bg_without_declassify, at::as_internal, at::with_alloc>());
static_assert(!matches_pack<corpus::internal_io_without_declassify, at::as_internal, at::with<>>());

// A row that names several effects is read for each.
static_assert(
    matches_pack<corpus::classified_io_without_declassify, at::as_secret, at::with<Eff::Bg, Eff::Alloc, Eff::IO>>());
static_assert(
    matches_pack<corpus::classified_bg_without_declassify, at::as_secret, at::with<Eff::Bg, Eff::Alloc, Eff::IO>>());

// The row an entry reads is the row of the binding: the Effect grade
// joined with the lifts of the atoms.  A system call that lifts IO opens
// the channel that a stated with<IO> opens, and a declassification or a
// public grade closes it the same way.
using ProcessStateRead = at::syscall::per<at::syscall::SyscallId::getpid>;
using FileWrite = at::syscall::per<at::syscall::SyscallId::write>;
using FutexCall = at::syscall::per<at::syscall::SyscallId::futex>;
static_assert(only_this_entry_matches<corpus::classified_io_without_declassify, ProcessStateRead>());
static_assert(only_this_entry_matches<corpus::classified_io_without_declassify, at::as_secret, FileWrite>());
static_assert(only_this_entry_matches<corpus::internal_io_without_declassify, at::as_internal, FileWrite>());
static_assert(!matches_pack<corpus::classified_io_without_declassify, at::as_public, FileWrite>());
static_assert(
    !matches_pack<corpus::classified_io_without_declassify, at::declassify<policy::WireSerialize>, FileWrite>());
static_assert(!matches_pack<corpus::classified_io_without_declassify, FutexCall>(), "a futex call lifts Block alone");
using StderrWrite = at::stdio::write<at::stdio::streams::Stderr>;
static_assert(only_this_entry_matches<corpus::classified_io_without_declassify, StderrWrite>(),
              "a stdio write lifts IO, so a classified value written to a stream needs a declassification");
static_assert(!matches_pack<corpus::classified_io_without_declassify, at::as_public, StderrWrite>());
static_assert(matches_pack<corpus::ghost_runtime_observable, at::ghost, at::as_public, at::sync::park>(),
              "a park lifts Block, which is emitted code");
static_assert(matches_pack<corpus::ghost_runtime_observable, at::ghost, at::as_public, FutexCall>());
static_assert(matches_pack<corpus::ghost_runtime_observable, at::ghost, at::as_public, at::sync::spin_pause>(),
              "a spin with no exit lifts Div, which is observable");
static_assert(!matches_pack<corpus::ghost_runtime_observable, at::ghost, at::as_public, at::sync::bounded_spin>(),
              "a bounded spin lifts Nondet, which elision can drop");

// ---------------------------------------------------------------------
// Staleness: the discharge must match the axis.  A value declassified
// for export is still a secret where replay is concerned.

static_assert(matches_pack<corpus::staleness_secret_without_declassify, at::as_secret, at::stale_to<5>>());
static_assert(matches_pack<corpus::staleness_secret_without_declassify, at::as_classified, at::stale_to<5>>());
static_assert(matches_pack<corpus::staleness_secret_without_declassify, at::declassify<policy::AuditedLogging>,
                           at::stale_to<5>>());
static_assert(matches_pack<corpus::staleness_secret_without_declassify, at::declassify<policy::WireSerialize>,
                           at::stale_to<5>>());
static_assert(!matches_pack<corpus::staleness_secret_without_declassify, at::declassify<policy::AuthorizedReplay>,
                            at::stale_to<5>>());
static_assert(!matches_pack<corpus::staleness_secret_without_declassify, at::as_public, at::stale_to<5>>());
static_assert(!matches_pack<corpus::staleness_secret_without_declassify, at::as_internal, at::stale_to<5>>());
static_assert(!matches_pack<corpus::staleness_secret_without_declassify, at::as_secret>());

// ---------------------------------------------------------------------
// Ghost: the observable set is the one foundation declares.

static_assert(matches_pack<corpus::ghost_runtime_observable, at::ghost, at::with_io>());
static_assert(matches_pack<corpus::ghost_runtime_observable, at::ghost, at::with_bg>());
static_assert(matches_pack<corpus::ghost_runtime_observable, at::ghost, at::with_block>());
static_assert(matches_pack<corpus::ghost_runtime_observable, at::ghost, at::with_alloc>());
static_assert(!matches_pack<corpus::ghost_runtime_observable, at::ghost, at::with_init>());
static_assert(!matches_pack<corpus::ghost_runtime_observable, at::ghost, at::with_test>());
static_assert(!matches_pack<corpus::ghost_runtime_observable, at::ghost, at::with<>>());
static_assert(!matches_pack<corpus::ghost_runtime_observable, at::ghost>());
static_assert(!matches_pack<corpus::ghost_runtime_observable, at::as_public, at::with_io>());
static_assert(!matches_pack<corpus::ghost_runtime_observable, at::affine, at::as_public, at::with_io>());

// ---------------------------------------------------------------------
// The discharge masks are what the policies say: the replay policy names
// Staleness, the three export policies name IO, and no policy names Bg or
// Failure.

static_assert(at::discharge_mask_of(^^policy::AuthorizedReplay) == at::DischargeAxis::Staleness);
static_assert(at::discharge_mask_of(^^policy::AuditedLogging) == at::DischargeAxis::IO);
static_assert(at::discharge_mask_of(^^policy::WireSerialize) == at::DischargeAxis::IO);
static_assert(at::discharge_mask_of(^^policy::HashForCompare) == at::DischargeAxis::None);
static_assert(at::discharge_mask_of(^^policy::LengthOnly) == at::DischargeAxis::None);
static_assert(at::discharge_mask_of(^^policy::UserDisplay) == at::DischargeAxis::IO);
static_assert(at::discharge_mask_of(^^int) == at::DischargeAxis::None, "a non-policy discharges nothing");

static_assert(at::discharge_axis_contains(at::DischargeAxis::Staleness | at::DischargeAxis::IO, at::DischargeAxis::IO));
static_assert(at::discharge_axis_contains(at::DischargeAxis::Staleness | at::DischargeAxis::IO,
                                          at::DischargeAxis::Staleness));
static_assert(!at::discharge_axis_contains(at::DischargeAxis::Staleness, at::DischargeAxis::IO));
static_assert(!at::discharge_axis_contains(at::DischargeAxis::None, at::DischargeAxis::Bg));
static_assert((at::DischargeAxis::Staleness & at::DischargeAxis::IO) == at::DischargeAxis::None);

// ---------------------------------------------------------------------
// The gate reaches the corpus, and reports the entry.

static_assert(!IsAccepted<int, at::with_io>);
static_assert(IsAccepted<int, at::with_io, at::as_public>);
static_assert(!IsAccepted<int, at::as_internal, at::with_bg>);
static_assert(!IsAccepted<int, at::as_secret, at::stale_to<5>>);
static_assert(IsAccepted<int, at::declassify<policy::AuthorizedReplay>, at::stale_to<5>>);
static_assert(!IsAccepted<int, at::with_io, at::declassify<policy::AuthorizedReplay>>,
              "the replay policy does not license an export");
static_assert(!IsAccepted<int, at::with_bg, at::declassify<policy::LengthOnly>>, "no policy licenses Bg");
static_assert(IsAccepted<int, at::with_io, at::declassify<policy::WireSerialize>>);

// Corpus-only refusals: no collision rule reads these pairs.  P010
// reads Alloc, IO and Block, so ghost x Bg on a public grade is the
// corpus's alone.
static_assert(!IsAccepted<int, at::ghost, at::as_public, at::with_bg>);
static_assert(IsAccepted<int, at::ghost, at::as_public>);
static_assert(IsAccepted<int, at::as_public, at::with_bg>);

// The gate reads the lifts too.  A classified binding that writes a file
// is refused, and the public binding that writes the same file is not.
static_assert(!IsAccepted<int, FileWrite>);
static_assert(IsAccepted<int, at::as_public, FileWrite>);
static_assert(IsAccepted<int, FutexCall>, "Block alone opens no channel the corpus reads");
static_assert(
    std::is_same_v<corpus::matched_entry_or_void_t<int, ProcessStateRead>, corpus::classified_io_without_declassify>);

static_assert(
    std::is_same_v<corpus::matched_entry_or_void_t<int, at::with_io>, corpus::classified_io_without_declassify>);
static_assert(std::is_same_v<corpus::matched_entry_or_void_t<int, at::ghost, at::as_public, at::with_bg>,
                             corpus::ghost_runtime_observable>);
static_assert(std::is_same_v<corpus::matched_entry_or_void_t<int, at::with_io, at::as_public>, void>);
static_assert(std::is_same_v<corpus::matched_entry_or_void_t<int>, void>);

// A pack that matches two entries reports the first in tuple order.
static_assert(std::is_same_v<corpus::matched_entry_or_void_t<int, at::ghost, at::with_io>,
                             corpus::classified_io_without_declassify>);

static_assert(corpus::corpus_entry_name_for_v<int, at::with_io> == "classified_io_without_declassify");
static_assert(corpus::corpus_entry_name_for_v<int, at::as_internal, at::with_bg> == "internal_bg_without_declassify");
static_assert(corpus::corpus_entry_name_for_v<int>.size() == 0);
static_assert(find(corpus::corpus_full_diagnostic_v<int, at::with_io>, "classified_io_without_declassify").is_some());
static_assert(find(corpus::corpus_full_diagnostic_v<int, at::with_io>, "Sabelfeld-Myers 2003").is_some());
static_assert(starts_with(corpus::corpus_full_diagnostic_v<int, at::with_io>, "fixy::fn<Type, Atoms...> [tier 5"));
static_assert(corpus::corpus_full_diagnostic_v<int>.size() == 0);

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
                 && Entry::name == mint_static_text(std::meta::identifier_of(std::meta::dealias(^^Entry)).data())
                 && Entry::cite().size() != 0 && find(Entry::full_diagnostic(), Entry::name).is_some();
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
        ::foundation::core::TextView const name = Entry::name;
        ::foundation::core::TextView const description = Entry::description;
        ::foundation::core::TextView const remediation = Entry::remediation;
        ::foundation::core::TextView const cite = Entry::cite();
        ::foundation::core::TextView const full = Entry::full_diagnostic();
        if (name.size() == 0) return 1;
        if (description.size() == 0) return 2;
        if (remediation.size() == 0) return 3;
        if (cite.size() == 0) return 4;
        if (find(full, name).is_none()) return 5;
        if (find(full, cite).is_none()) return 6;
        ++walked;
    }
    if (walked != corpus::corpus_size) return 7;

    ::foundation::core::TextView const matched = corpus::corpus_entry_name_for_v<int, at::with_io>;
    if (matched != "classified_io_without_declassify") return 8;
    if (corpus::corpus_entry_name_for_v<int>.size() != 0) return 9;

    const auto mask = at::DischargeAxis::Staleness | at::DischargeAxis::Bg;
    if (!at::discharge_axis_contains(mask, at::DischargeAxis::Bg)) return 10;
    if (at::discharge_axis_contains(mask, at::DischargeAxis::IO)) return 11;
    return 0;
}
#pragma GCC diagnostic pop

}  // namespace

int main() {
    if (int rc = check_runtime_paths(); rc != 0) return rc;
    return 0;
}

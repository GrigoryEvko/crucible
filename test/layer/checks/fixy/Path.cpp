// The compile-time checks of fixy/Path.h.

#include <fixy/Path.h>

namespace fixy::detail::path_self_test {

static_assert(sizeof(Path<tags::source::External>) == sizeof(std::filesystem::path));

static_assert(!std::is_same_v<sanitize::path_traversal::no_dotdot, sanitize::path_traversal::absolute_root_locked>,
              "no_dotdot and absolute_root_locked must be distinct policy-marker types: separate "
              "sanitize policies need separate dispatch.");

static_assert(RetagAllowed<tags::source::External, tags::source::Sanitized>,
              "tags::source::External must launder into tags::source::Sanitized through a sanitize "
              "entry-point.");
static_assert(RetagAllowed<tags::source::FromUser, tags::source::Sanitized>,
              "tags::source::FromUser must launder into tags::source::Sanitized.");
static_assert(RetagAllowed<tags::source::FromUserPath, tags::source::Sanitized>,
              "tags::source::FromUserPath must launder into tags::source::Sanitized through a "
              "sanitize entry-point.");
static_assert(RetagAllowed<tags::source::FromEnvPath, tags::source::Sanitized>,
              "tags::source::FromEnvPath must launder into tags::source::Sanitized through a "
              "sanitize entry-point.");
static_assert(RetagAllowed<tags::source::FromConfigPath, tags::source::Sanitized>,
              "tags::source::FromConfigPath must launder into tags::source::Sanitized through a "
              "sanitize entry-point.");

static_assert(RetagAllowed<tags::source::Sanitized, tags::source::Sanitized>,
              "Identity retag from tags::source::Sanitized to tags::source::Sanitized must stay "
              "admitted, so re-sanitizing an already-sanitized path still compiles.");

static_assert(!RetagAllowed<::fixy::detail::retag_sentinel::NeverFrom, tags::source::Sanitized>,
              "The reserved never-admitted source tag must stay rejected into "
              "tags::source::Sanitized. Admitting it would defeat the fail-closed retag default.");

// CipherPath has no edge into Sanitized on purpose.  Its bytes never
// crossed an untrusted boundary, and keeping it out of the three
// external lanes is what stops operator-supplied bytes from reaching
// the directory-anchored open helpers.
template <typename From>
concept CanSanitize = requires(Path<From> p) { sanitize::path_traversal::sanitize_path_no_dotdot(std::move(p)); };
static_assert(CanSanitize<tags::source::External>);
static_assert(CanSanitize<tags::source::FromUserPath>);
static_assert(!CanSanitize<::fixy::detail::retag_sentinel::NeverFrom>);

}  // namespace fixy::detail::path_self_test

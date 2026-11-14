#pragma once

// A filesystem path that carries its provenance, and the predicates that
// advance that provenance to Sanitized.
//
// A path is a Tagged over a foundation::os::PathText of at most kPathBytes
// characters, with a zero after the last one.  The tag is the whole
// mechanism: a helper that opens a file demands Path<Sanitized>, and the
// only way to hold one is to have run a predicate below, because the retag
// catalog admits the edge and the value constructor of Tagged is private.
//
// The predicates read a TextView, and they read only the characters.  They
// open nothing and resolve no link, so a path that passes need not exist.
// A PathText refuses a zero byte and a text longer than its bound, so a path
// holds neither.  The text entry sanitize_path refuses the two before it
// copies the characters into a path.
//
// The path type and its sanitizer are one header.  Declaration order
// inside it carries the dependency between the two, with no include
// cycle.

#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>
#include <foundation/os/PathText.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace fixy {

// The largest count of the characters of a path.  PATH_MAX of Linux is 4096
// and counts the zero, so the kernel refuses a path of exactly kPathBytes
// characters with enametoolong.  The root of the Cipher has the same bound.
inline constexpr std::size_t kPathBytes = 4096;

template <typename Source>
using Path = Tagged<::foundation::os::PathText<kPathBytes>, Source>;

struct PathTraversal {};

// An embedded NUL is rejected because POSIX path calls stop at it, so a
// path built from a string that holds one silently opens a shorter
// path.  A `..` component is rejected because it escapes an intended
// root even when the path is absolute.
//
// A leading `/` is not rejected: an absolute root is a legitimate
// choice.  Symlinks are not resolved either, and a check here could not
// settle them anyway — the guarantee comes from opening the root with
// O_NOFOLLOW and O_DIRECTORY and anchoring later opens to that
// descriptor.  Nothing here touches the filesystem, so a path that
// passes need not exist.
enum class PathTraversalError : std::uint8_t {
    Empty = 0,
    TooLong = 1,
    EmbeddedNul = 2,
    DotDotComponent = 3,
    CandidateNotAbsolute = 4,
    AnchorNotAbsolute = 5,
    EscapesAnchor = 6,
};

}  // namespace fixy

namespace fixy::sanitize::path_traversal {

struct no_dotdot {};
struct absolute_root_locked {};

// One part of a path, and the characters after it.  The walk of the two
// predicates below reads it.  It stays in this namespace and in no new one,
// because the census of the row hash reads each namespace that declares a
// class.
struct PathPart final {
    ::foundation::core::TextView part{};
    ::foundation::core::TextView rest{};
};

// The next part of text that names an entry: the characters after the
// slashes at the start, up to the next slash.  An empty part and the part
// "." name no other entry, so the walk skips them.  The part is empty when
// text holds no such part.  Complexity: linear in the characters that it
// reads.
[[nodiscard]] constexpr PathPart next_part_(::foundation::core::TextView text) noexcept {
    ::foundation::core::TextView rest = text;
    while (rest.size() != 0) {
        std::size_t const length = ::foundation::core::find(rest, '/').value_or(rest.size());
        ::foundation::core::TextView const part = rest.window(0, length).expect("a find gives an offset in the text");
        std::size_t const after = length == rest.size() ? length : length + 1;
        rest = rest.window(after, rest.size() - after).expect("the offset after the part is in the text");
        if (part.size() != 0 && part != ::foundation::core::TextView{"."}) return PathPart{part, rest};
    }
    return PathPart{};
}

// True when a part of text is "..".  Complexity: linear in the size of text.
[[nodiscard]] constexpr bool has_dotdot_part_(::foundation::core::TextView text) noexcept {
    for (PathPart next = next_part_(text); next.part.size() != 0; next = next_part_(next.rest)) {
        if (next.part == ::foundation::core::TextView{".."}) return true;
    }
    return false;
}

// The checks of a text that a path holds: it is not empty, it fits
// kPathBytes, it holds no zero byte, and no part of it is "..".  The checks
// run in that order, and the first refusal is the error.  Complexity: linear
// in the size of the text.
[[nodiscard]] constexpr ::foundation::core::Result<::foundation::core::Unit, PathTraversalError>
check_no_dotdot(::foundation::core::TextView candidate) noexcept {
    if (candidate.size() == 0) return ::foundation::core::err(PathTraversalError::Empty);
    if (candidate.size() > kPathBytes) return ::foundation::core::err(PathTraversalError::TooLong);
    // POSIX path arguments end at the first NUL, so an embedded NUL makes the
    // kernel see a shorter path than the one that was checked.
    if (::foundation::core::find(candidate, '\0').is_some()) {
        return ::foundation::core::err(PathTraversalError::EmbeddedNul);
    }
    if (has_dotdot_part_(candidate)) return ::foundation::core::err(PathTraversalError::DotDotComponent);
    return ::foundation::core::Unit{};
}

// True when the parts of root_anchor are the first parts of candidate.  The
// compare reads the characters only.  An empty part and the part "." are
// skipped on the two sides, so "/var/./cipher//objects" is under
// "/var/cipher", and so is each path under "/var/cipher/".  The compare is
// of whole parts, so "/var/cipher2" is not under "/var/cipher".
//
// A relative text is refused and not resolved, because a resolve reads the
// working directory.  A ".." part of the candidate gives DotDotComponent,
// because a part after the anchor can climb out of it.  A ".." part of the
// anchor is compared as a name, and no candidate part is "..", so such an
// anchor refuses each candidate.  Complexity: linear in the sizes of the
// two texts.
[[nodiscard]] constexpr ::foundation::core::Result<::foundation::core::Unit, PathTraversalError>
check_absolute_root_locked(::foundation::core::TextView candidate, ::foundation::core::TextView root_anchor) noexcept {
    if (!::foundation::core::starts_with(root_anchor, "/")) {
        return ::foundation::core::err(PathTraversalError::AnchorNotAbsolute);
    }
    if (!::foundation::core::starts_with(candidate, "/")) {
        return ::foundation::core::err(PathTraversalError::CandidateNotAbsolute);
    }
    if (has_dotdot_part_(candidate)) return ::foundation::core::err(PathTraversalError::DotDotComponent);
    PathPart candidate_next = next_part_(candidate);
    for (PathPart anchor_next = next_part_(root_anchor); anchor_next.part.size() != 0;
         anchor_next = next_part_(anchor_next.rest)) {
        if (candidate_next.part != anchor_next.part) return ::foundation::core::err(PathTraversalError::EscapesAnchor);
        candidate_next = next_part_(candidate_next.rest);
    }
    return ::foundation::core::Unit{};
}

// The path of tainted under the tag Sanitized, or the first refusal of
// check_no_dotdot.
template <typename From>
    requires RetagAllowed<From, tags::source::Sanitized>
[[nodiscard]] inline ::foundation::core::Result<Path<tags::source::Sanitized>, PathTraversalError>
sanitize_path_no_dotdot(Path<From>&& tainted) noexcept {
    CRUCIBLE_TRY(_, check_no_dotdot(tainted.value().c_text().view()));
    return std::move(tainted).template retag<tags::source::Sanitized>();
}

// The path of tainted under the tag Sanitized, when it passes
// check_no_dotdot and lies under root_anchor.  The dot-dot check runs first,
// so a candidate with a ".." part gives DotDotComponent.
template <typename From>
    requires RetagAllowed<From, tags::source::Sanitized>
[[nodiscard]] inline ::foundation::core::Result<Path<tags::source::Sanitized>, PathTraversalError>
sanitize_path_root_locked(Path<From>&& tainted, ::foundation::core::TextView root_anchor) noexcept {
    ::foundation::core::TextView const candidate = tainted.value().c_text().view();
    CRUCIBLE_TRY(_, check_no_dotdot(candidate));
    CRUCIBLE_TRY(_, check_absolute_root_locked(candidate, root_anchor));
    return std::move(tainted).template retag<tags::source::Sanitized>();
}

}  // namespace fixy::sanitize::path_traversal

namespace fixy {

// The path of a text from outside, under the tag Sanitized, or the first
// refusal of check_no_dotdot.  The checks run on the text, so the copy into
// the path cannot fail.  The tag External is the only tag that this entry
// takes.  Complexity: linear in the size of the text.
[[nodiscard]] inline ::foundation::core::Result<Path<tags::source::Sanitized>, PathTraversalError>
sanitize_path(Tagged<::foundation::core::TextView, tags::source::External> const& external_text) noexcept {
    ::foundation::core::TextView const text = external_text.value();
    CRUCIBLE_TRY(_, sanitize::path_traversal::check_no_dotdot(text));
    ::foundation::os::PathText<kPathBytes> characters{};
    characters.append(text).expect("the check refused a zero byte and a text longer than the bound");
    return retag<tags::source::Sanitized>(mint_tagged<tags::source::External>(std::move(characters)));
}

}  // namespace fixy

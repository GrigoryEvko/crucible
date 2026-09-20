#pragma once

// The provenance tag headers are pulled in so that a consumer of a typed path
// gets the tags that name its provenance.
#include <crucible/safety/_Tagged.h>
#include <crucible/safety/source/_Path.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string_view>
#include <utility>

namespace crucible::safety {

template <typename Source>
using Path = Tagged<std::filesystem::path, Source>;

struct PathTraversal {};

// An embedded NUL is rejected because POSIX path calls stop at it, so a path
// built from a string that holds one silently opens a shorter path.  A `..`
// component is rejected because it escapes an intended root even when the path
// is absolute.
//
// A leading `/` is not rejected: an absolute root is a legitimate choice.
// Symlinks are not resolved either, and a check here could not settle them
// anyway — the guarantee comes from opening the root with O_NOFOLLOW and
// O_DIRECTORY and anchoring later opens to that descriptor.  Nothing here
// touches the filesystem, so a path that passes need not exist.
enum class PathTraversalError : std::uint8_t {
    Empty = 0,
    TooLong = 1,
    EmbeddedNul = 2,
    DotDotComponent = 3,
    CandidateNotAbsolute = 4,
    AnchorNotAbsolute = 5,
    EscapesAnchor = 6,
};

// POSIX PATH_MAX is typically 4096.  This leaves headroom over that while
// staying far below the width of the arithmetic that carries it.
inline constexpr std::size_t MAX_PATH_BYTES = 16 * 1024;

}  // namespace crucible::safety

// This include belongs at the bottom, not with the others.  The header it names
// defines the sanitizing promoter over the alias, the error enum and the byte
// cap declared above, so those declarations must precede it.  It is included
// here rather than left to the caller so that a typed path and the promoter
// that advances its provenance arrive together.
#include <crucible/safety/sanitize/PathTraversal.h>

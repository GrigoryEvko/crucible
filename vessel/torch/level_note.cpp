// The level note of the two vessel libraries (the note of level_note.h).
//
// Each vessel library compiles this file with its own flags, so its note
// names its own level.  CMake gives this file CRUCIBLE_X86_LEVEL_EXACT, the
// number of CRUCIBLE_X86_LEVEL, in a build at a level.  The configure step
// refuses each -m word of such a build other than -march and -mtune, so the
// kind is exact.  Each other build gives the kind host.

#include "level_note.h"

#if defined(__x86_64__)

#if defined(CRUCIBLE_X86_LEVEL_EXACT)
static_assert(CRUCIBLE_X86_LEVEL_EXACT == CRUCIBLE_X86_LEVEL_OF_TARGET,
              "the flags of the unit give a level other than CRUCIBLE_X86_LEVEL of the build");
#endif

namespace crucible::vessel {
namespace {

#if defined(CRUCIBLE_X86_LEVEL_EXACT)
constexpr LevelKind kKindOfBuild = LevelKind::exact;
#else
constexpr LevelKind kKindOfBuild = LevelKind::host;
#endif

[[gnu::section(".note.crucible"), gnu::used, gnu::retain]] alignas(kLevelNoteAlignment) constinit LevelNote const
    kLevelNoteOfLibrary = level_note_of(kKindOfBuild);

}  // namespace
}  // namespace crucible::vessel

#endif

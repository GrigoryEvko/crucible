// The level probe of the vessel, libcrucible_level.so: the one library that
// the loader of the vessel (crucible_native.py) opens before it picks the
// x86-64 level of the two vessel libraries.
//
// Python cannot read CPUID, and a vessel library of a level above the host
// can fault in its own global constructors.  So the probe compiles at
// -march=x86-64, the level v1 of each x86_64 core, with its own copy of each
// base source that it needs (CRUCIBLE_HANDLER_SOURCES of CMakeLists.txt).
// It links no library of the build, whose code has the level of the build.
// Its own note says exact v1, so the loader can read it as it reads the note
// of each vessel library.
//
// C ABI
//   crucible_level_abi_version()   the ABI stamp of the probe, which the
//                                  loader compares with its own constant
//   crucible_level_pick(request, size)
//                                  the level that the loader loads, 1 to 4:
//                                  the highest level of the host, or the
//                                  level of the request when the host has
//                                  it.  request and size give the text of
//                                  CRUCIBLE_ISA_LEVEL, and an empty text is
//                                  no request.  A text that is no level, and
//                                  a level above the host, end the process
//                                  with fatal (require_x86_level of
//                                  foundation/os/X86Level.h).

#include "level_note.h"

#include <foundation/Quarantine.h>
#include <foundation/core/Region.h>
#include <foundation/os/X86Level.h>

#include <cstdint>

static_assert(::foundation::os::x86_level_of_target() == ::foundation::os::X86Level::v1,
              "the level probe compiles at -march=x86-64, so it runs on each x86_64 host");

namespace crucible::vessel {
namespace {

// The ABI stamp of the probe.  Change it with each change of the C ABI, and
// change EXPECTED_LEVEL_ABI_VERSION of crucible_native.py and PROBE_ABI_VERSION
// of utils/assay/core/x86level.py with it.  The probe carries no layout note:
// the test launcher of the gate loads it too, and reads this stamp.
constexpr std::uint64_t kLevelProbeAbiVersion = 1;

[[gnu::section(".note.crucible"), gnu::used, gnu::retain]] alignas(kLevelNoteAlignment) constinit LevelNote const
    kLevelNoteOfProbe = level_note_of(LevelKind::exact);

}  // namespace
}  // namespace crucible::vessel

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: a C entry takes and gives the raw pointers of the C caller")
extern "C" {

[[gnu::visibility("default")]] uint64_t crucible_level_abi_version(void) noexcept;
[[gnu::visibility("default")]] uint32_t crucible_level_pick(char const* request, uint64_t size) noexcept;

uint64_t crucible_level_abi_version(void) noexcept { return crucible::vessel::kLevelProbeAbiVersion; }

uint32_t crucible_level_pick(char const* request, uint64_t size) noexcept {
    ::foundation::core::TextView const text = ::foundation::core::mint_foreign_text(request, size).characters();
    ::foundation::os::X86Level const level =
        ::foundation::os::require_x86_level(::foundation::os::read_x86_feature_words(), text);
    return static_cast<uint32_t>(level);
}

}  // extern "C"
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

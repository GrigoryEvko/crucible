#pragma once

// The opt-out region of the quarantine rule (CLAUDE.md section XXII, R7).
//
// CRUCIBLE_I_KNOW_WHAT_IM_DOING("CLASS: reason") opens a region, and
// CRUCIBLE_END_I_KNOW_WHAT_IM_DOING closes it.  A finding of the quarantine
// plugin inside a region is reported as opted_out and is not an error.  The
// reason starts with its class: ABI:, C-HEADER:, PROBE:, ORACLE: or MEASURE:.
// utils/scripts/quarantine-region-ledger.txt holds one row for each region of
// the tree, and the number of rows can only decrease.
//
// The build defines CRUCIBLE_QUARANTINE_ACTIVE when it loads the quarantine
// plugin (utils/tools/quarantine/Quarantine.cmake), and the macros then
// expand to the two pragmas of the plugin.  The plugin refuses each such
// pragma that these macros do not make.  With no plugin, the macros expand
// to nothing: a compile without the plugin knows no pragma quarantine, and
// GCC warns about an unknown pragma.

#if defined(CRUCIBLE_QUARANTINE_ACTIVE)
#define CRUCIBLE_QUARANTINE_PRAGMA_(text) _Pragma(#text)
#define CRUCIBLE_I_KNOW_WHAT_IM_DOING(reason) CRUCIBLE_QUARANTINE_PRAGMA_(quarantine I_KNOW_WHAT_IM_DOING(reason))
#define CRUCIBLE_END_I_KNOW_WHAT_IM_DOING CRUCIBLE_QUARANTINE_PRAGMA_(quarantine END_I_KNOW_WHAT_IM_DOING)
#else
#define CRUCIBLE_I_KNOW_WHAT_IM_DOING(reason)
#define CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
#endif

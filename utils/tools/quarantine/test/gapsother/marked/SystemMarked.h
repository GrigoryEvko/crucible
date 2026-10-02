// A quarantined header that marks itself as a system header.  With the
// plugin of main, the walk skipped each declaration of a system header and
// each namespace that a system header opens first, so the pointer and each
// later member of system_marked gave no finding.  The plugin refuses the
// pragma in each file under the root.

#pragma once
#pragma GCC system_header

namespace system_marked {
inline int* marked_pointer = nullptr;
}  // namespace system_marked

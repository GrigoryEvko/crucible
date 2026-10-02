// A quarantined header that isystem_user.cpp includes through -isystem.  The
// system flag of its line map does not make it a library file: its pointer
// is a finding, and its function is no library entity.

#pragma once

namespace marked {
inline int* marked_pointer = nullptr;
inline int marked_count() { return 1; }
}  // namespace marked

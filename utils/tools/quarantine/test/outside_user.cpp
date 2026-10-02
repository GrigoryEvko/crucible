// A quarantined unit that uses a library whose header is outside the source
// root and is not a system header.  check_plugin.py writes OutsideLibrary.h
// in its scratch directory.  The plugin names only the standard library and
// the C library, so the object and the call give no finding.  KNOWN_GAPS of
// check_plugin.py pins the two lines.

#include <OutsideLibrary.h>

OutsideType outside_object;
int outside_use() { return outside_value(); }

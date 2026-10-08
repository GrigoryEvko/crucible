// A quarantined unit that uses a library whose header is outside the source
// root and is not a system header.  utils/assay/guard/legacy/check_plugin.py writes OutsideLibrary.h
// in its scratch directory and names it with -I, as a build names the header
// of a third-party library.  A header outside the root is a library file, so
// the object is a c_library_object and the call is a c_library_call.

#include <OutsideLibrary.h>

OutsideType outside_object;
int outside_use() { return outside_value(); }

// A header outside the source root opens and closes an opt-out region.  The
// test writes OutsideRegion.h in its scratch directory.  The unit uses no
// entity of the header, because a header outside the root is a library file.
#include <OutsideRegion.h>

int read_outside_region() { return 1; }

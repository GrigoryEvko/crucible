// A header outside the source root opens and closes an opt-out region.  The
// test writes OutsideRegion.h in its scratch directory.
#include <OutsideRegion.h>

int read_outside_region() { return outside_region_value(); }

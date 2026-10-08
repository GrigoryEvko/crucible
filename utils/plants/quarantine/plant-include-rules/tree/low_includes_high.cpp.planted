// A quarantined unit that includes fixy/Low.h.  With HIGH_FIRST, this file
// includes fixy/high/High.h first, so the include in Low.h enters no file.
// The upward include of Low.h is a finding in the two forms.

#if defined(HIGH_FIRST)
#include <fixy/high/High.h>
#endif
#include <fixy/Low.h>

int read_low() { return probe::low_value(); }

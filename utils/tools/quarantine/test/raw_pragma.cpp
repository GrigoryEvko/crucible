// A raw pragma of a region in a file under the root is an error: only the
// macros of foundation/Quarantine.h open and close a region.

#pragma quarantine I_KNOW_WHAT_IM_DOING("PROBE: a raw pragma")
int raw_pragma_value = 0;
#pragma quarantine END_I_KNOW_WHAT_IM_DOING

// A quarantined file that defines its own region macros is an error: the
// plugin trusts the macros only when a file that is not quarantined defines
// them.

#define CRUCIBLE_I_KNOW_WHAT_IM_DOING(reason) _Pragma("quarantine I_KNOW_WHAT_IM_DOING(\"PROBE: an own macro\")")
#define CRUCIBLE_END_I_KNOW_WHAT_IM_DOING _Pragma("quarantine END_I_KNOW_WHAT_IM_DOING")
CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: an own macro")
int own_macro_value = 0;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

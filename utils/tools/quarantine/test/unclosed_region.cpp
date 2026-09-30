// A region that no END pragma closes is an error.

#pragma crucible I_KNOW_WHAT_IM_DOING("a region that nothing closes")
int unclosed_value = 0;

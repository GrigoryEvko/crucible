// A region that gives no reason is an error.

#pragma crucible I_KNOW_WHAT_IM_DOING()
int value_without_reason = 0;
#pragma crucible END_I_KNOW_WHAT_IM_DOING

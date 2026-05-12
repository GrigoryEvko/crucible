#include <crucible/cntp/CongestionControl.h>

// GAPS-120 fixture #3: a custom Fixy/user congestion-control module
// must expose a constexpr kernel congestion-control name.

struct MissingName {};

int main() {
    auto choice =
        crucible::cntp::mint_custom_cc_choice<
            MissingName,
            crucible::cntp::LinkClass::PublicInternet>();
    (void)choice;
    return 0;
}

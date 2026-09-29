// A custom congestion-control module must name a valid kernel module.
// A kernel name has no uppercase letter, so KernelCcName::from refuses
// "BBR" and the second clause of CustomCcModule is false.
#include <crucible/cntp/CongestionControl.h>

#include <string_view>

struct UppercaseName {
    static constexpr std::string_view congestion_control_name() noexcept { return "BBR"; }
};

int main() {
    auto choice = crucible::cntp::mint_custom_cc_choice<UppercaseName, crucible::cntp::LinkClass::PublicInternet>();
    (void)choice;
    return 0;
}

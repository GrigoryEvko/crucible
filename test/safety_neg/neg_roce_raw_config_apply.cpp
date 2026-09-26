// The apply path takes a config that came through the RoCE config mint, so
// a bare RoceConfig does not bind to the tagged parameter.

#include <crucible/cntp/RoceConfig.h>

// apply_roce_config is a stub that carries [[deprecated]].  The suppression
// keeps that warning from failing this translation unit, so the only error
// left is the refused binding.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main() {
    auto iface = crucible::cntp::NicInterfaceName::from("eth0");
    crucible::cntp::RoceConfig raw{.interface = *iface};
    auto result = crucible::cntp::apply_roce_config(raw);
    return result.has_value() ? 0 : 1;
}

#pragma GCC diagnostic pop

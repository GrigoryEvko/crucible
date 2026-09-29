// A config tagged with another source is not a RoCE config.  The tag is
// part of the type, and no conversion runs between two tags, so an External
// config does not reach the validation that takes a declared one.

#include <crucible/cntp/RoceConfig.h>

int main() {
    auto iface = crucible::cntp::NicInterfaceName::from("eth0");
    auto external =
        ::fixy::mint_tagged<::fixy::tags::source::External>(crucible::cntp::RoceConfig{.interface = *iface});
    auto valid = crucible::cntp::validate_roce_config(external);
    return valid.has_value() ? 0 : 1;
}

#include <crucible/fixy/Cap.h>

// FIXY-V-212 fixture #2: the fixy::cap::cntp::mint_custom_cc_choice
// re-export MUST preserve the substrate's CustomCcModule<Module>
// concept gate.  A struct lacking the static
// `congestion_control_name()` accessor is not a CustomCcModule —
// the call through the fixy:: surface must fail with the same
// constraint diagnostic as the bare substrate call.

struct NotAModule {
    // Deliberately missing: static constexpr std::string_view
    //                       congestion_control_name() noexcept;
};

int main() {
    auto choice =
        crucible::fixy::cap::cntp::mint_custom_cc_choice<
            NotAModule,
            crucible::cntp::LinkClass::CrossDatacenter>();
    (void)choice;
    return 0;
}

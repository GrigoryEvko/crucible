// An admission decision is background or test work.  The hot foreground
// context owns neither capability, so it cannot ask the controller to
// accept a connection.

#include <crucible/cntp/BackpressureRuntime.h>
#include <fixy/Ctx.h>

int main() {
    namespace cntp = crucible::cntp;
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto controller = cntp::mint_admission_controller<2, 2>(init);
    cntp::ConnectionRequest const request{.socket = cntp::admit_socket_fd(9).expect("9 is not negative")};
    auto decision = controller.try_accept_connection(::foundation::effects::testing::foreground(), request,
                                                     ::fixy::View<cntp::ResourcePressure const>{});
    return decision.is_ok() ? 0 : 1;
}

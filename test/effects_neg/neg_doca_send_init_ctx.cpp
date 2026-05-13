#include <crucible/cntp/Doca.h>

#include <array>
#include <cstddef>

int main() {
    namespace cog = crucible::cog;
    namespace doca = crucible::cntp::doca;
    namespace eff = crucible::effects;

    doca::OwnedDocaOffload offload{doca::DocaOffloadHandle{
        .dpu_uuid = cog::Uuid{1, 2},
        .program_id = *doca::admit_doca_program_id(1),
        .kind = doca::DocaOffloadKind::SwimGossip,
        .queue_depth = *doca::admit_doca_queue_depth(1),
    }};
    doca::DpuCommChannel channel{
        std::move(offload),
        doca::DocaChannelConfig{
            .max_payload_bytes = *doca::admit_doca_payload_bytes(1),
        },
    };
    std::array<std::byte, 1> payload{};
    auto sent = channel.send_to_dpu(eff::ColdInitCtx{}, payload);
    (void)sent;
    return 0;
}

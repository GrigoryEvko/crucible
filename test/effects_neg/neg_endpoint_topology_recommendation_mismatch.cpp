// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-077: mint_endpoint consults recommend_topology_for_workload when
// the ExecCtx carries ctx_workload::ChannelBudget.  A declared 4x4,
// DRAM-scale channel workload must use a ManyToMany substrate; an SPSC
// OneToOne endpoint is a topology recommendation mismatch.

#include <crucible/concurrent/Endpoint.h>

namespace eff  = crucible::effects;
namespace conc = crucible::concurrent;
namespace saf  = crucible::safety;

struct UserTag {};

using HugeFourByFourCtx = eff::ExecCtx<
    eff::ctx_cap::Fg,
    eff::ctx_numa::Local,
    eff::ctx_alloc::Stack,
    eff::ctx_heat::Hot,
    eff::ctx_resid::L1,
    eff::Row<>,
    eff::ctx_workload::ChannelBudget<
        16ULL * 1024ULL * 1024ULL * 1024ULL,
        4,
        4,
        false>>;

int main() {
    using Spsc = conc::PermissionedSpscChannel<int, 64, UserTag>;

    Spsc ch;
    auto whole = saf::mint_permission_root<conc::spsc_tag::Whole<UserTag>>();
    auto [pp, cp] = saf::mint_permission_split<
        conc::spsc_tag::Producer<UserTag>,
        conc::spsc_tag::Consumer<UserTag>>(std::move(whole));
    auto handle = ch.producer(std::move(pp));

    auto bad = conc::mint_endpoint<Spsc, conc::Direction::Producer>(
        HugeFourByFourCtx{}, handle);
    (void)bad;
    (void)cp;
    return 0;
}

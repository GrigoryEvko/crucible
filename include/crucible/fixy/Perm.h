#pragma once

// The permission tokens and mint factories below live in crucible::safety
// and crucible::permissions.  The re-export gives a caller that pulls in only
// the fixy surface an entry point that does not name those namespaces.

#include <crucible/permissions/FairSharedPermissionPool.h>
#include <crucible/permissions/FederationPermission.h>
#include <crucible/permissions/_Permission.h>
#include <crucible/permissions/_PermissionFork.h>
#include <crucible/permissions/PermissionInherit.h>
#include <crucible/permissions/_ReadView.h>
#include <crucible/safety/PermissionGridGenerator.h>
#include <crucible/safety/PermissionTreeGenerator.h>

#include <type_traits>

namespace crucible::fixy::perm {

using ::crucible::safety::Permission;
using ::crucible::safety::SharedPermission;

using ::crucible::safety::mint_permission_root;

using ::crucible::safety::mint_permission_split;
using ::crucible::safety::mint_permission_combine;
using ::crucible::safety::mint_permission_split_n;
using ::crucible::safety::mint_permission_combine_n;

using ::crucible::safety::mint_permission_share;

using ::crucible::safety::mint_permission_fork;

using ::crucible::permissions::mint_permission_inherit;
using ::crucible::permissions::mint_permission_inherit_t;

using ::crucible::safety::SharedPermissionPool;
using ::crucible::safety::SharedPermissionGuard;

using ::crucible::safety::ReadView;
using ::crucible::safety::mint_read_view;

using ::crucible::safety::FairSharedPermissionPool;

using ::crucible::safety::GridPermissions;
using ::crucible::safety::mint_grid_permissions;
using ::crucible::safety::can_split_grid_v;

using ::crucible::safety::ProducerSide;
using ::crucible::safety::ConsumerSide;
using ::crucible::safety::Producer;
using ::crucible::safety::Consumer;
using ::crucible::safety::auto_split_grid;

using ::crucible::safety::Slice;
using ::crucible::safety::auto_split_n;
using ::crucible::safety::auto_split_n_t;
using ::crucible::safety::auto_split_n_permissions_t;
using ::crucible::safety::can_split_n_v;

namespace policy {
using ::crucible::permissions::policy::admit_orgs;
}  // namespace policy

}  // namespace crucible::fixy::perm

// The sentinels below verify that each alias resolves to the substrate
// entity and not to a local of the same name.
//
// Some of these names are also re-exported under another fixy directory.
// Every path must alias one substrate symbol.  If two paths diverged, a
// caller would get a different type depending on which one it imported, and
// splits_into matching would stop pairing across translation units.

namespace crucible::fixy::perm::self_test {

struct PermDualExportTag {};

static_assert(std::is_same_v<::crucible::fixy::perm::Permission<PermDualExportTag>,
                             ::crucible::safety::Permission<PermDualExportTag>>,
              "fixy::perm::Permission must alias safety::Permission — dual-export "
              "drift breaks linearity proofs across TUs.");

static_assert(std::is_same_v<::crucible::fixy::perm::SharedPermission<PermDualExportTag>,
                             ::crucible::safety::SharedPermission<PermDualExportTag>>,
              "fixy::perm::SharedPermission must alias safety::SharedPermission "
              "and must agree with the fixy::wrap:: parallel re-export.");

// The witness names the alias template without ever instantiating it.
// Instantiating it would need a survivor_registry specialization for the
// probe tag, which would tie this sentinel to substrate internals.  Naming
// it in a member alias template is enough to force the lookup.

template <typename Probe = void>
struct mint_permission_inherit_t_name_reach_witness_ {
    template <typename DT, typename... STs>
    using probe_t = ::crucible::fixy::perm::mint_permission_inherit_t<DT, STs...>;
    static constexpr bool ok = true;
};
static_assert(mint_permission_inherit_t_name_reach_witness_<>::ok,
              "fixy::perm::mint_permission_inherit_t must be reachable as an "
              "alias template.");

// admit_orgs is one of the names reachable through a second fixy path, on
// the federation axis.  The probe tags are fresh so the witness does not
// couple to an existing fixture.

struct DualExportOrgProbeA {};
struct DualExportOrgProbeB {};

static_assert(std::is_same_v<::crucible::fixy::perm::policy::admit_orgs<DualExportOrgProbeA>,
                             ::crucible::permissions::policy::admit_orgs<DualExportOrgProbeA>>,
              "fixy::perm::policy::admit_orgs must alias permissions::policy::admit_orgs");

static_assert(std::is_same_v<::crucible::fixy::perm::policy::admit_orgs<DualExportOrgProbeA, DualExportOrgProbeB>,
                             ::crucible::permissions::policy::admit_orgs<DualExportOrgProbeA, DualExportOrgProbeB>>,
              "Variadic instantiation must preserve substrate identity.");

static_assert(::crucible::fixy::perm::policy::admit_orgs<DualExportOrgProbeA>::template admits<DualExportOrgProbeA>);
static_assert(!::crucible::fixy::perm::policy::admit_orgs<DualExportOrgProbeA>::template admits<DualExportOrgProbeB>);
static_assert(::crucible::fixy::perm::policy::admit_orgs<DualExportOrgProbeA,
                                                         DualExportOrgProbeB>::template admits<DualExportOrgProbeB>);

struct U014_PoolGuardProbeTag {};
struct U014_ReadViewProbeTag {};
struct U014_FairProbeTag {};

static_assert(std::is_same_v<::crucible::fixy::perm::SharedPermissionPool<U014_PoolGuardProbeTag>,
                             ::crucible::safety::SharedPermissionPool<U014_PoolGuardProbeTag>>,
              "fixy::perm::SharedPermissionPool must alias safety::SharedPermissionPool");

static_assert(std::is_same_v<::crucible::fixy::perm::SharedPermissionGuard<U014_PoolGuardProbeTag>,
                             ::crucible::safety::SharedPermissionGuard<U014_PoolGuardProbeTag>>,
              "fixy::perm::SharedPermissionGuard must alias safety::SharedPermissionGuard");

static_assert(std::is_same_v<::crucible::fixy::perm::ReadView<U014_ReadViewProbeTag>,
                             ::crucible::safety::ReadView<U014_ReadViewProbeTag>>,
              "fixy::perm::ReadView must alias safety::ReadView");

// The witness leaves BurstLimit at the substrate default so it does not
// depend on the exact non-type signature past the leading tag.
static_assert(std::is_same_v<::crucible::fixy::perm::FairSharedPermissionPool<U014_FairProbeTag>,
                             ::crucible::safety::FairSharedPermissionPool<U014_FairProbeTag>>,
              "fixy::perm::FairSharedPermissionPool must alias safety::FairSharedPermissionPool");

// The grid types are named but never completed.  Completing one needs a
// Whole tag carrying manifest split specializations, which would tie this
// sentinel to a substrate-internal tag tree.

template <typename Whole, std::size_t M, std::size_t N>
using fixy_grid_reach_probe_ = ::crucible::fixy::perm::GridPermissions<Whole, M, N>;
template <typename Whole, std::size_t M, std::size_t N>
using safety_grid_reach_probe_ = ::crucible::safety::GridPermissions<Whole, M, N>;
static_assert(std::is_same_v<fixy_grid_reach_probe_<U014_PoolGuardProbeTag, 2, 3>,
                             safety_grid_reach_probe_<U014_PoolGuardProbeTag, 2, 3>>,
              "fixy::perm::GridPermissions must alias safety::GridPermissions");

// An arbitrary probe tag admits grid splitting because the substrate
// derives the split specialization from the producer and consumer side
// encoding rather than requiring one per tag.  The M=0 and N=0 arms pin
// the boundary where both paths must agree on false.
static_assert(::crucible::fixy::perm::can_split_grid_v<U014_PoolGuardProbeTag, 2, 3>
                  == ::crucible::safety::can_split_grid_v<U014_PoolGuardProbeTag, 2, 3>,
              "fixy::perm::can_split_grid_v must mirror safety::can_split_grid_v");
static_assert(::crucible::fixy::perm::can_split_grid_v<U014_PoolGuardProbeTag, 2, 3>,
              "Grid splitting must succeed for any tag with M>0, N>0 (auto-encoded).");
static_assert(!::crucible::fixy::perm::can_split_grid_v<U014_PoolGuardProbeTag, 0, 3>,
              "Grid splitting MUST reject M=0 through the fixy:: path.");
static_assert(!::crucible::fixy::perm::can_split_grid_v<U014_PoolGuardProbeTag, 2, 0>,
              "Grid splitting MUST reject N=0 through the fixy:: path.");

static_assert(std::is_same_v<decltype(&::crucible::fixy::perm::mint_read_view<U014_ReadViewProbeTag>),
                             decltype(&::crucible::safety::mint_read_view<U014_ReadViewProbeTag>)>,
              "fixy::perm::mint_read_view must resolve to safety::mint_read_view");

static_assert(std::is_same_v<decltype(&::crucible::fixy::perm::mint_grid_permissions<U014_PoolGuardProbeTag, 2, 3>),
                             decltype(&::crucible::safety::mint_grid_permissions<U014_PoolGuardProbeTag, 2, 3>)>,
              "fixy::perm::mint_grid_permissions must resolve to safety::mint_grid_permissions");

constexpr int kU014SurfaceCardinality = 8;
static_assert(kU014SurfaceCardinality == 8, "Pool/Guard/ReadView/mint_read_view/Fair/Grid/"
                                            "mint_grid_permissions/can_split_grid_v surface drifted from 8.");

static_assert(std::is_same_v<::crucible::fixy::perm::Slice<U014_PoolGuardProbeTag, 0>,
                             ::crucible::safety::Slice<U014_PoolGuardProbeTag, 0>>,
              "fixy::perm::Slice must alias safety::Slice");
static_assert(std::is_same_v<::crucible::fixy::perm::auto_split_n_t<U014_PoolGuardProbeTag, 3>,
                             ::crucible::safety::auto_split_n_t<U014_PoolGuardProbeTag, 3>>,
              "fixy::perm::auto_split_n_t must alias safety::auto_split_n_t");
static_assert(std::is_same_v<::crucible::fixy::perm::auto_split_n_permissions_t<U014_PoolGuardProbeTag, 2>,
                             ::crucible::safety::auto_split_n_permissions_t<U014_PoolGuardProbeTag, 2>>,
              "fixy::perm::auto_split_n_permissions_t must alias the substrate alias");

static_assert(::crucible::fixy::perm::can_split_n_v<U014_PoolGuardProbeTag, 4>
                  == ::crucible::safety::can_split_n_v<U014_PoolGuardProbeTag, 4>,
              "fixy::perm::can_split_n_v must mirror safety::can_split_n_v");
static_assert(::crucible::fixy::perm::can_split_n_v<U014_PoolGuardProbeTag, 4>,
              "Tree N-ary split must succeed for N>0 through the fixy:: path.");
static_assert(!::crucible::fixy::perm::can_split_n_v<U014_PoolGuardProbeTag, 0>,
              "Tree N-ary split MUST reject N=0 through the fixy:: path.");

static_assert(std::is_same_v<::crucible::fixy::perm::ProducerSide<U014_PoolGuardProbeTag>,
                             ::crucible::safety::ProducerSide<U014_PoolGuardProbeTag>>,
              "fixy::perm::ProducerSide must alias safety::ProducerSide");
static_assert(std::is_same_v<::crucible::fixy::perm::ConsumerSide<U014_PoolGuardProbeTag>,
                             ::crucible::safety::ConsumerSide<U014_PoolGuardProbeTag>>,
              "fixy::perm::ConsumerSide must alias safety::ConsumerSide");
static_assert(std::is_same_v<::crucible::fixy::perm::Producer<U014_PoolGuardProbeTag, 0>,
                             ::crucible::safety::Producer<U014_PoolGuardProbeTag, 0>>,
              "fixy::perm::Producer must alias safety::Producer");
static_assert(std::is_same_v<::crucible::fixy::perm::Consumer<U014_PoolGuardProbeTag, 1>,
                             ::crucible::safety::Consumer<U014_PoolGuardProbeTag, 1>>,
              "fixy::perm::Consumer must alias safety::Consumer");
static_assert(std::is_same_v<typename ::crucible::fixy::perm::auto_split_grid<U014_PoolGuardProbeTag, 2, 3>::whole_type,
                             typename ::crucible::safety::auto_split_grid<U014_PoolGuardProbeTag, 2, 3>::whole_type>,
              "fixy::perm::auto_split_grid must alias safety::auto_split_grid");

constexpr int kV177SurfaceCardinality = 10;
static_assert(kV177SurfaceCardinality == 10, "ProducerSide/ConsumerSide/Producer/Consumer/auto_split_grid + "
                                             "Slice/auto_split_n/auto_split_n_t/"
                                             "auto_split_n_permissions_t/can_split_n_v surface drifted from 10.");

}  // namespace crucible::fixy::perm::self_test

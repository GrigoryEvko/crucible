// The compile-time checks of fixy/Bands.h.

#include <fixy/Bands.h>

namespace fixy {

namespace detail::bands_self_test {

using PureInt = DetSafe<DetSafeTier_v::Pure, int>;
using PhiloxInt = DetSafe<DetSafeTier_v::PhiloxRng, int>;
using MonoInt = DetSafe<DetSafeTier_v::MonotonicClockRead, int>;
using NdsInt = DetSafe<DetSafeTier_v::NonDeterministicSyscall, int>;

static_assert(sizeof(PureInt) == sizeof(int));
static_assert(sizeof(hot_path::Hot<double>) == sizeof(double));
static_assert(sizeof(RecipeSpec<int>) >= sizeof(int) + 2);

static_assert(IsBand<PureInt>);
static_assert(IsBand<PureInt const&>);
static_assert(IsBand<opaque_lifetime::PerFleet<int>>);
static_assert(!IsBand<int>);

// A pinned grade whose outer order puts the weaker claim higher is not a
// band.  relax cannot move a Public value to Secret, although Secret is
// the weaker end.  The same carrier over an order that puts the stronger
// claim higher is a band.
using PinnedConf = ::foundation::algebra::Graded<
    ::foundation::algebra::ModalityKind::Absolute,
    ::foundation::algebra::lattices::ConfLattice::At<::foundation::algebra::lattices::Conf::Public>, int>;
static_assert(!IsBand<PinnedConf>, "relax and satisfies_v need an outer order that puts the stronger claim higher");
static_assert(!IsBand<RecipeSpec<int>>, "the grade of a RecipeSpec is stored, not pinned");

static_assert(IsBandOf<DetSafeLattice, PureInt>);
static_assert(!IsBandOf<HotPathLattice, PureInt>);
static_assert(is_band_of_v<LifetimeLattice, opaque_lifetime::PerRequest<int>>);

static_assert(std::is_same_v<band_value_t<PureInt>, int>);
static_assert(std::is_same_v<band_lattice_t<PureInt>, DetSafeLattice>);
static_assert(std::is_same_v<band_tier_t<PureInt>, DetSafeTier_v>);
static_assert(band_tier_v<PureInt> == DetSafeTier_v::Pure);
static_assert(band_tier_v<PhiloxInt const&> == DetSafeTier_v::PhiloxRng);

static_assert(satisfies_v<PureInt, DetSafeTier_v::PhiloxRng>);
static_assert(satisfies_v<PhiloxInt, DetSafeTier_v::PhiloxRng>);
static_assert(!satisfies_v<MonoInt, DetSafeTier_v::PhiloxRng>,
              "MonotonicClockRead must not satisfy PhiloxRng.  A clock read "
              "does not reproduce on replay, so it must not reach a consumer "
              "that requires a value which does.");
static_assert(!satisfies_v<NdsInt, DetSafeTier_v::FilesystemMtime>);

static_assert(std::is_same_v<rebind_band_t<PureInt, DetSafeTier_v::PhiloxRng>, PhiloxInt>);

template <typename B, auto Target>
concept can_relax = requires(B b) { relax<Target>(std::move(b)); };

static_assert(can_relax<PureInt, DetSafeTier_v::PhiloxRng>);
static_assert(can_relax<PhiloxInt, DetSafeTier_v::PhiloxRng>);
static_assert(!can_relax<PhiloxInt, DetSafeTier_v::Pure>,
              "relax<Pure> on a PhiloxRng value must be rejected.  It would "
              "claim a determinism the source does not provide.");

// The tier is asserted at the door and nowhere else: not with braces,
// not with a default value, not from the substrate side, and not with a
// cv or reference spelling of the band.
template <typename B>
concept can_mint_band = requires(band_value_t<B> v) { mint_band<B>(std::move(v)); };
template <typename B>
concept can_build_at_bottom = requires(band_value_t<B> v) { B::at_bottom(std::move(v)); };

static_assert(can_mint_band<PureInt>);
static_assert(!can_mint_band<PureInt const>);
static_assert(!can_mint_band<PureInt&>);
static_assert(!std::is_constructible_v<PureInt, int, typename PureInt::grade_type>);
static_assert(!std::is_default_constructible_v<PureInt>);
static_assert(!can_build_at_bottom<PureInt>);

constexpr PureInt pinned_pure = mint_band<PureInt>(42);
static_assert(pinned_pure.peek() == 42);
static_assert(tier_of(pinned_pure) == DetSafeTier_v::Pure);
static_assert(relax<DetSafeTier_v::PhiloxRng>(pinned_pure).peek() == 42);
static_assert(tier_of(relax<DetSafeTier_v::NonDeterministicSyscall>(pinned_pure))
              == DetSafeTier_v::NonDeterministicSyscall);

// The poset band.  These cells pin the trunk structure, which a
// chain-shaped reading of this band would silently lose.
using CtaInt = ScopedFence<MemoryScope_v::Cta, int>;
using GpuInt = ScopedFence<MemoryScope_v::Gpu, int>;
using InnerInt = ScopedFence<MemoryScope_v::Inner, int>;
using OuterInt = ScopedFence<MemoryScope_v::Outer, int>;
using SystemInt = ScopedFence<MemoryScope_v::System, int>;
using ThreadInt = ScopedFence<MemoryScope_v::Thread, int>;
using WarpInt = ScopedFence<MemoryScope_v::Warp, int>;

static_assert(sizeof(CtaInt) == sizeof(int));
static_assert(sizeof(scoped_fence::Cta<double>) == sizeof(double));
static_assert(IsBand<CtaInt>);
static_assert(IsBandOf<MemoryScopeLattice, CtaInt>);
static_assert(!IsBandOf<DetSafeLattice, CtaInt>);
static_assert(band_tier_v<CtaInt> == MemoryScope_v::Cta);
static_assert(std::is_same_v<band_lattice_t<CtaInt>, MemoryScopeLattice>);

// Within a trunk the order holds in the admission direction.
static_assert(satisfies_v<GpuInt, MemoryScope_v::Cta>, "A device-wide fence publishes at block scope too, because Cta "
                                                       "sits below Gpu on the accelerator trunk.");
static_assert(!satisfies_v<CtaInt, MemoryScope_v::Gpu>,
              "A block-scope fence is too narrow for a device-wide requirement.");
static_assert(satisfies_v<OuterInt, MemoryScope_v::Inner>, "An outer-shareable fence subsumes an inner-shareable "
                                                           "requirement within the same trunk.");

// Across trunks nothing satisfies anything, which is the property a chain
// cannot express.
static_assert(!satisfies_v<GpuInt, MemoryScope_v::Inner>,
              "A device fence has no ordering relation to an inner-shareable "
              "domain, because the two trunks are incomparable.");
static_assert(!satisfies_v<InnerInt, MemoryScope_v::Cta>);

// The shared bottom and top.
static_assert(satisfies_v<SystemInt, MemoryScope_v::Inner>);
static_assert(satisfies_v<SystemInt, MemoryScope_v::Cta>);
static_assert(satisfies_v<CtaInt, MemoryScope_v::Thread>);
static_assert(satisfies_v<InnerInt, MemoryScope_v::Thread>);
static_assert(!satisfies_v<ThreadInt, MemoryScope_v::Cta>,
              "A thread-local provider does not subsume a block-scope requirement.");

static_assert(can_relax<GpuInt, MemoryScope_v::Cta>);
static_assert(can_relax<SystemInt, MemoryScope_v::Inner>);
static_assert(can_relax<CtaInt, MemoryScope_v::Cta>);
static_assert(!can_relax<CtaInt, MemoryScope_v::Gpu>, "relax<Gpu> on a ScopedFence<Cta> must be rejected.  Claiming a "
                                                      "value is device-visible when it was only published at block "
                                                      "scope would offer it to observers the fence never reached.");
static_assert(!can_relax<CtaInt, MemoryScope_v::Inner>, "relax<Inner> on a ScopedFence<Cta> must be rejected.  The two "
                                                        "trunks are incomparable.");
static_assert(!can_relax<InnerInt, MemoryScope_v::Cta>);

constexpr GpuInt pinned_gpu = mint_band<GpuInt>(42);
static_assert(tier_of(pinned_gpu) == MemoryScope_v::Gpu);
static_assert(relax<MemoryScope_v::Cta>(pinned_gpu).peek() == 42);
static_assert(tier_of(relax<MemoryScope_v::Cta>(pinned_gpu)) == MemoryScope_v::Cta);
static_assert(std::is_same_v<rebind_band_t<GpuInt, MemoryScope_v::Cta>, CtaInt>);
static_assert(std::is_same_v<scoped_fence::Cta<int>, CtaInt>);
static_assert(!std::is_same_v<CtaInt, InnerInt>);
static_assert(CtaInt::lattice_name() == "MemoryScopeLattice::At<Cta>");
static_assert(InnerInt::lattice_name() == "MemoryScopeLattice::At<Inner>");

// The second poset band.  The named backends are incomparable siblings,
// so a kernel built for one never reaches a consumer of another.
using NvInt = vendor::Nv<int>;
using AmdInt = vendor::Amd<int>;
using PortableInt = vendor::Portable<int>;
using NoVendorInt = vendor::None<int>;

static_assert(sizeof(NvInt) == sizeof(int));
static_assert(IsBandOf<VendorLattice, NvInt>);
static_assert(!IsBandOf<MemoryScopeLattice, NvInt>);
static_assert(band_tier_v<NvInt> == VendorBackend_v::NV);
static_assert(satisfies_v<PortableInt, VendorBackend_v::NV>, "A portable value runs on every named backend.");
static_assert(satisfies_v<NvInt, VendorBackend_v::None>);
static_assert(!satisfies_v<NvInt, VendorBackend_v::AMD>,
              "An NV value must not reach an AMD consumer.  The two backends are incomparable.");
static_assert(!satisfies_v<NvInt, VendorBackend_v::Portable>);
static_assert(!satisfies_v<NoVendorInt, VendorBackend_v::CPU>);
static_assert(can_relax<PortableInt, VendorBackend_v::NV>);
static_assert(can_relax<NvInt, VendorBackend_v::None>);
static_assert(!can_relax<NvInt, VendorBackend_v::Portable>,
              "relax<Portable> on an NV value must be rejected.  It would claim the value runs everywhere.");
static_assert(!can_relax<NvInt, VendorBackend_v::AMD>);
static_assert(std::is_same_v<rebind_band_t<PortableInt, VendorBackend_v::AMD>, AmdInt>);
static_assert(NvInt::lattice_name() == "VendorLattice::At<NV>");

constexpr PortableInt pinned_portable = mint_band<PortableInt>(7);
static_assert(tier_of(relax<VendorBackend_v::CPU>(pinned_portable)) == VendorBackend_v::CPU);
static_assert(relax<VendorBackend_v::CPU>(pinned_portable).peek() == 7);

// The residency chain: nearer the core is higher.
using HotInt = residency_heat::Hot<int>;
using WarmInt = residency_heat::Warm<int>;
using ColdInt = residency_heat::Cold<int>;

static_assert(sizeof(HotInt) == sizeof(int));
static_assert(IsBandOf<ResidencyHeatLattice, HotInt>);
static_assert(!IsBandOf<CipherTierLattice, HotInt>,
              "Residency heat and cipher tier spell the same three tiers and must stay two lattices.");
static_assert(!std::is_same_v<HotInt, cipher_tier::Hot<int>>);
static_assert(satisfies_v<HotInt, ResidencyHeatTag_v::Warm>);
static_assert(!satisfies_v<ColdInt, ResidencyHeatTag_v::Hot>,
              "A Cold value must not reach a consumer that budgets for an L1 hit.");
static_assert(can_relax<HotInt, ResidencyHeatTag_v::Cold>);
static_assert(!can_relax<WarmInt, ResidencyHeatTag_v::Hot>);
static_assert(std::is_same_v<rebind_band_t<HotInt, ResidencyHeatTag_v::Warm>, WarmInt>);
static_assert(HotInt::lattice_name() == "ResidencyHeatLattice::At<Hot>");

constexpr HotInt pinned_hot = mint_band<HotInt>(9);
static_assert(tier_of(relax<ResidencyHeatTag_v::Warm>(pinned_hot)) == ResidencyHeatTag_v::Warm);

static_assert(!std::is_constructible_v<RecipeSpec<int>, int, typename RecipeSpec<int>::grade_type>);
static_assert(!std::is_default_constructible_v<RecipeSpec<int>>);

constexpr RecipeSpec<int> spec = mint_recipe_spec(7, Tolerance::ULP_FP16, RecipeFamily::Kahan);
static_assert(tolerance_of(spec) == Tolerance::ULP_FP16);
static_assert(recipe_family_of(spec) == RecipeFamily::Kahan);
static_assert(admits(spec, Tolerance::ULP_FP8, RecipeFamily::Kahan));
static_assert(admits(spec, Tolerance::ULP_FP16, RecipeFamily::None));
static_assert(!admits(spec, Tolerance::BITEXACT, RecipeFamily::Kahan));
static_assert(!admits(spec, Tolerance::ULP_FP16, RecipeFamily::Pairwise));

// The substrate's weaken() relaxes a claim, and compose() keeps the
// looser tier and the common family.  A move toward a tighter tier fails
// the guard in weaken(): test/fixy/neg/neg_recipe_spec_weaken_to_a_tighter_tier.cpp.
static_assert(tolerance_of(spec.weaken({Tolerance::ULP_FP8, RecipeFamily::None})) == Tolerance::ULP_FP8);
static_assert(recipe_family_of(spec.weaken({Tolerance::ULP_FP8, RecipeFamily::None})) == RecipeFamily::None);
static_assert(tolerance_of(spec.compose(mint_recipe_spec(1, Tolerance::BITEXACT, RecipeFamily::Pairwise)))
              == Tolerance::ULP_FP16);
static_assert(recipe_family_of(spec.compose(mint_recipe_spec(1, Tolerance::BITEXACT, RecipeFamily::Pairwise)))
              == RecipeFamily::None);

// Every enumerator of each lattice enum has a short spelling in the
// namespace that mirrors it.  An enumerator added to one of these enums
// and left without an alias is reachable only through the long
// Band<Tier, T> form, which is the gap these lines close.
static_assert(every_tier_has_an_alias<^^::fixy::det_safe, std::meta::dealias(^^DetSafeTier_v)>(),
              "fixy/Bands.h: a DetSafeTier enumerator has no alias in fixy::det_safe.");
static_assert(every_tier_has_an_alias<^^::fixy::alloc_class, std::meta::dealias(^^AllocClassTag_v)>(),
              "fixy/Bands.h: an AllocClassTag enumerator has no alias in fixy::alloc_class.");
static_assert(every_tier_has_an_alias<^^::fixy::hot_path, std::meta::dealias(^^HotPathTier_v)>(),
              "fixy/Bands.h: a HotPathTier enumerator has no alias in fixy::hot_path.");
static_assert(every_tier_has_an_alias<^^::fixy::cipher_tier, std::meta::dealias(^^CipherTierTag_v)>(),
              "fixy/Bands.h: a CipherTierTag enumerator has no alias in fixy::cipher_tier.");
static_assert(every_tier_has_an_alias<^^::fixy::wait, std::meta::dealias(^^WaitStrategy_v)>(),
              "fixy/Bands.h: a WaitStrategy enumerator has no alias in fixy::wait.");
// Tolerance arrives here through a using-declaration rather than an
// alias declaration, and `^^` cannot be applied to one, so this names
// the enum where it is defined.
static_assert(every_tier_has_an_alias<^^::fixy::numerical_tier, ^^::foundation::algebra::lattices::Tolerance>(),
              "fixy/Bands.h: a Tolerance enumerator has no alias in fixy::numerical_tier.");
static_assert(every_tier_has_an_alias<^^::fixy::opaque_lifetime, std::meta::dealias(^^Lifetime_v)>(),
              "fixy/Bands.h: a Lifetime enumerator has no alias in fixy::opaque_lifetime.");
static_assert(every_tier_has_an_alias<^^::fixy::scoped_fence, std::meta::dealias(^^MemoryScope_v)>(),
              "fixy/Bands.h: a MemoryScope enumerator has no alias in fixy::scoped_fence.");
static_assert(every_tier_has_an_alias<^^::fixy::vendor, std::meta::dealias(^^VendorBackend_v)>(),
              "fixy/Bands.h: a VendorBackend enumerator has no alias in fixy::vendor.");
static_assert(every_tier_has_an_alias<^^::fixy::residency_heat, std::meta::dealias(^^ResidencyHeatTag_v)>(),
              "fixy/Bands.h: a ResidencyHeatTag enumerator has no alias in fixy::residency_heat.");

// The walk answers no when an enumerator has no alias, which is what
// keeps the assertions above from passing vacuously.  det_safe
// holds no HotPathTier alias, so asking it about one is the shape of
// the failure without planting a defect in the table.
static_assert(!some_alias_names_tier<^^::fixy::det_safe, HotPathTier_v::Hot>(),
              "fixy/Bands.h: the alias walk must answer no for a tier the namespace does not name, or "
              "every_tier_has_an_alias proves nothing.");

}  // namespace detail::bands_self_test

}  // namespace fixy

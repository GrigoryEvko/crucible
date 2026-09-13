#include <crucible/fixy/Profile.h>

#include <type_traits>

namespace fixy = crucible::fixy;
namespace gr = crucible::fixy::grant;
using D = crucible::fixy::dim::DimensionAxis;

template <D Axis>
using strict = gr::accept_default_strict_for<Axis>;

#if CRUCIBLE_FIXY_STRICT
static_assert(fixy::fixy_is_strict, "Under CRUCIBLE_FIXY_STRICT=1, fixy_is_strict must be true.");
#else
static_assert(!fixy::fixy_is_strict, "Under CRUCIBLE_FIXY_STRICT=0, fixy_is_strict must be false.");
#endif

static_assert(fixy::IsAcceptedSketch<int>, "IsAcceptedSketch<int> must accept the empty Grants pack.");
static_assert(fixy::IsAcceptedSketch<int*>, "IsAcceptedSketch<int*> must accept — object pointers.");
static_assert(fixy::IsAcceptedSketch<int (*)(int)>, "IsAcceptedSketch<int(*)(int)> must accept — function POINTERS "
                                                    "are object types.");
static_assert(fixy::IsAcceptedSketch<int, strict<D::Usage>>,
              "IsAcceptedSketch<int, partial pack> must accept — Grants axis "
              "is permissive.");

// Sketch mode relaxes the grants axis but not the type axis. These reject
// at the requires-clause rather than deeper inside the substrate, which is
// what keeps the diagnostic readable.
static_assert(!fixy::IsAcceptedSketch<void>, "IsAcceptedSketch<void> must reject. Sketch mode does not bypass the "
                                             "Type-axis floor.");
static_assert(!fixy::IsAcceptedSketch<const int>, "top-level const Type must reject under SKETCH.");
static_assert(!fixy::IsAcceptedSketch<volatile int>, "top-level volatile Type must reject under SKETCH.");
static_assert(!fixy::IsAcceptedSketch<int&>, "lvalue-reference Type must reject under SKETCH.");
static_assert(!fixy::IsAcceptedSketch<int&&>, "rvalue-reference Type must reject under SKETCH.");
static_assert(!fixy::IsAcceptedSketch<int[5]>, "array Type must reject under SKETCH.");
static_assert(!fixy::IsAcceptedSketch<int(int)>, "bare function-type Type must reject under SKETCH.");

#if CRUCIBLE_FIXY_STRICT
static_assert(!fixy::IsAcceptedActive<int>, "Under STRICT, IsAcceptedActive<int> with empty pack must reject "
                                            "(every dim must be engaged).");
#else
static_assert(fixy::IsAcceptedActive<int>, "Under SKETCH, IsAcceptedActive<int> with empty pack must accept "
                                           "(always-true).");
#endif

// The Type marker is injected for the caller, so the pack below names
// every other axis and must not name that one.
static_assert(
    fixy::IsAcceptedActive<
        int, strict<D::Refinement>, strict<D::Usage>, strict<D::Effect>, strict<D::Security>, strict<D::Protocol>,
        strict<D::Lifetime>, strict<D::Provenance>, strict<D::Trust>, strict<D::Representation>,
        strict<D::Observability>, strict<D::Complexity>, strict<D::Precision>, strict<D::Space>, strict<D::Overflow>,
        strict<D::Mutation>, strict<D::Reentrancy>, strict<D::Size>, strict<D::Version>, strict<D::Staleness>,
        strict<D::Synchronization>, strict<D::Regime>, strict<D::FpMode>, strict<D::SyscallSurface>,
        strict<D::ControlFlow>, strict<D::CallShape>, strict<D::StackUse>, strict<D::GlobalState>, strict<D::Stdio>,
        strict<D::HwInstruction>, strict<D::BarrierStrength>, strict<D::SimdIsa>, strict<D::MemoryScope>>,
    "An all-strict pack naming every axis must accept under both modes.");

int main() { return 0; }

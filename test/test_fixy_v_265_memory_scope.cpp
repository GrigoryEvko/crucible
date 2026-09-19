// The scope of a fence is how far a publication becomes visible, which
// is a different question from how strongly the fence orders accesses.
// A block-scope release and a system-scope release order identically
// and reach different sets of observers, so visibility needs an order
// of its own.
//
// That order is two trunks, an accelerator one and an ARM shareability
// one, meeting only at the thread-local bottom and the system-wide top.
// Widening within a trunk subsumes; across trunks nothing subsumes
// anything, because a block-scope device fence and an inner-shareable
// barrier are not substitutes in either direction.  Every admission
// rule built on this lattice reduces to that incomparability, so if the
// two trunks were ever collapsed into one chain the rules would keep
// compiling and start admitting unsound pairs.  The cross-trunk
// negatives and the non-distributivity witness below are what stop
// that: a single chain would be distributive, and this lattice must
// not be.
//
// The assertions are re-stated at translation-unit scope on purpose.  A
// static_assert that lives only in a header is never evaluated under
// the project warning flags until some translation unit includes it.

#include <crucible/algebra/_Graded.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/lattices/_MemoryScopeLattice.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace cal = ::crucible::algebra::lattices;
namespace ms = ::crucible::algebra::lattices::memory_scope;

namespace {

using cal::MemoryScope;
using L = cal::MemoryScopeLattice;

static_assert(crucible::algebra::Lattice<L>, "MemoryScopeLattice must satisfy the Lattice concept "
                                             "(element_type + leq + join + meet).");
static_assert(crucible::algebra::BoundedLattice<L>, "MemoryScopeLattice has both bottom() (Thread) and top() "
                                                    "(System) — it is a bounded lattice.");
static_assert(!crucible::algebra::UnboundedLattice<L>);
static_assert(!crucible::algebra::Semiring<L>, "MemoryScopeLattice is NOT a semiring — it carries no "
                                               "equality+add+mul algebra, only the order-theoretic operations.");

static_assert(cal::memory_scope_count == 8, "MemoryScope must have exactly 8 enumerators. Adding a "
                                            "scope requires placing it inside the correct trunk numeric range "
                                            "so the trunk classifiers pick it up, extending every name switch, "
                                            "and revisiting each admission rule that reads this order.");

static_assert(std::is_same_v<std::underlying_type_t<MemoryScope>, std::uint8_t>,
              "MemoryScope must use uint8_t underlying type — the trunk "
              "is packed into the high nibble (accel=0x1_, ARM=0x2_) so within-trunk "
              "integer order equals visibility-width rank.");

static_assert(std::to_underlying(MemoryScope::Thread) == 0x00, "Thread = bottom sentinel");
static_assert(std::to_underlying(MemoryScope::Warp) == 0x10, "accel trunk base");
static_assert(std::to_underlying(MemoryScope::Gpu) == 0x13, "accel trunk top");
static_assert(std::to_underlying(MemoryScope::Inner) == 0x20, "ARM trunk base");
static_assert(std::to_underlying(MemoryScope::Outer) == 0x21, "ARM trunk top");
static_assert(std::to_underlying(MemoryScope::System) == 0xFF, "System = top sentinel");

static_assert(cal::mem_scope_is_accel(MemoryScope::Warp));
static_assert(cal::mem_scope_is_accel(MemoryScope::Cta));
static_assert(cal::mem_scope_is_accel(MemoryScope::Gpu));
static_assert(!cal::mem_scope_is_accel(MemoryScope::Inner));
static_assert(!cal::mem_scope_is_accel(MemoryScope::Thread),
              "Thread belongs to NEITHER trunk — it is the shared bottom, so "
              "same-trunk must be false for it and leq must special-case it.");
static_assert(!cal::mem_scope_is_accel(MemoryScope::System));
static_assert(cal::mem_scope_is_arm(MemoryScope::Inner));
static_assert(cal::mem_scope_is_arm(MemoryScope::Outer));
static_assert(!cal::mem_scope_is_arm(MemoryScope::Cta));
static_assert(!cal::mem_scope_is_arm(MemoryScope::System));
static_assert(cal::mem_scope_same_trunk(MemoryScope::Warp, MemoryScope::Gpu));
static_assert(cal::mem_scope_same_trunk(MemoryScope::Inner, MemoryScope::Outer));
static_assert(!cal::mem_scope_same_trunk(MemoryScope::Cta, MemoryScope::Inner),
              "An accelerator scope and an ARM domain are the cross-trunk case, "
              "which is where the incomparability comes from.");

static_assert(L::bottom() == MemoryScope::Thread);
static_assert(L::top() == MemoryScope::System);

static_assert(L::leq(MemoryScope::Warp, MemoryScope::Cta));
static_assert(L::leq(MemoryScope::Cta, MemoryScope::Cluster));
static_assert(L::leq(MemoryScope::Cluster, MemoryScope::Gpu));
static_assert(L::leq(MemoryScope::Warp, MemoryScope::Gpu), "transitive endpoints");
static_assert(L::leq(MemoryScope::Cta, MemoryScope::Gpu),
              "Cta is below Gpu — a Cta-scope requirement IS satisfied by a "
              "device-wide fence.");
static_assert(!L::leq(MemoryScope::Gpu, MemoryScope::Cta),
              "Gpu is not below Cta — a device-wide requirement is NOT satisfied "
              "by a block-scope fence, and a peer outside the block reads stale.");

// Inner is the inner-shareable domain and Outer the outer-shareable one.
static_assert(L::leq(MemoryScope::Inner, MemoryScope::Outer));
static_assert(!L::leq(MemoryScope::Outer, MemoryScope::Inner), "descending is false");

static_assert(L::leq(MemoryScope::Thread, MemoryScope::Cta));
static_assert(L::leq(MemoryScope::Thread, MemoryScope::Outer));
static_assert(L::leq(MemoryScope::Thread, MemoryScope::System));
static_assert(L::leq(MemoryScope::Gpu, MemoryScope::System));
static_assert(L::leq(MemoryScope::Outer, MemoryScope::System));

// Every accelerator scope and ARM domain pair is incomparable in both
// directions.  One direction alone would not be enough: a fence must be
// refused for the other trunk's requirement whichever side it is on.
static_assert(!L::leq(MemoryScope::Cta, MemoryScope::Inner));
static_assert(!L::leq(MemoryScope::Inner, MemoryScope::Cta));
static_assert(!L::leq(MemoryScope::Gpu, MemoryScope::Outer));
static_assert(!L::leq(MemoryScope::Outer, MemoryScope::Gpu));
static_assert(!L::leq(MemoryScope::Warp, MemoryScope::Inner));
static_assert(!L::leq(MemoryScope::Inner, MemoryScope::Warp));
static_assert(!L::leq(MemoryScope::Cluster, MemoryScope::Outer));
static_assert(!L::leq(MemoryScope::Outer, MemoryScope::Cluster));

static_assert(!L::leq(MemoryScope::System, MemoryScope::Cta));
static_assert(!L::leq(MemoryScope::System, MemoryScope::Thread));
static_assert(!L::leq(MemoryScope::Cta, MemoryScope::Thread));
static_assert(!L::leq(MemoryScope::Outer, MemoryScope::Thread));

static_assert(L::join(MemoryScope::Warp, MemoryScope::Gpu) == MemoryScope::Gpu);
static_assert(L::join(MemoryScope::Inner, MemoryScope::Outer) == MemoryScope::Outer);
static_assert(L::join(MemoryScope::Cta, MemoryScope::Inner) == MemoryScope::System,
              "The only common upper bound of an accelerator scope and an ARM "
              "domain is full-system visibility.");
static_assert(L::join(MemoryScope::Gpu, MemoryScope::Outer) == MemoryScope::System);
static_assert(L::join(MemoryScope::Thread, MemoryScope::Cta) == MemoryScope::Cta, "Thread is the join identity");
static_assert(L::join(MemoryScope::System, MemoryScope::Outer) == MemoryScope::System, "System absorbs in join");

static_assert(L::meet(MemoryScope::Warp, MemoryScope::Gpu) == MemoryScope::Warp);
static_assert(L::meet(MemoryScope::Inner, MemoryScope::Outer) == MemoryScope::Inner);
static_assert(L::meet(MemoryScope::Cta, MemoryScope::Inner) == MemoryScope::Thread,
              "The only common lower bound of an accelerator scope and an ARM "
              "domain is thread-local visibility.");
static_assert(L::meet(MemoryScope::Gpu, MemoryScope::Outer) == MemoryScope::Thread);
static_assert(L::meet(MemoryScope::System, MemoryScope::Outer) == MemoryScope::Outer, "System is the meet identity");
static_assert(L::meet(MemoryScope::Thread, MemoryScope::Cta) == MemoryScope::Thread, "Thread absorbs in meet");

// Working the two sides out by hand, with Gpu on one trunk and Inner
// and Outer on the other:
//
//   (Gpu join Inner) meet Outer = System meet Outer = Outer
//   (Gpu meet Outer) join (Inner meet Outer) = Thread join Inner = Inner
//
// The two sides differ, so the lattice is not distributive.  A single
// chain would be, which is what makes this assertion the guard against
// collapsing the trunks.
static_assert(L::meet(L::join(MemoryScope::Gpu, MemoryScope::Inner), MemoryScope::Outer) == MemoryScope::Outer,
              "The left side of the distributivity test must be Outer.");
static_assert(L::join(L::meet(MemoryScope::Gpu, MemoryScope::Outer), L::meet(MemoryScope::Inner, MemoryScope::Outer))
                  == MemoryScope::Inner,
              "The right side of the distributivity test must be Inner: Inner and "
              "Outer share a trunk, so their meet is Inner and not Thread.");
static_assert(L::meet(L::join(MemoryScope::Gpu, MemoryScope::Inner), MemoryScope::Outer)
                  != L::join(L::meet(MemoryScope::Gpu, MemoryScope::Outer),
                             L::meet(MemoryScope::Inner, MemoryScope::Outer)),
              "MemoryScopeLattice MUST be non-distributive. If this fires, the two "
              "trunks have been collapsed into a single chain, and every admission "
              "rule that relies on cross-trunk incomparability now admits pairs it "
              "must refuse.");

static_assert(crucible::algebra::Lattice<ms::ThreadScope>);
static_assert(crucible::algebra::Lattice<ms::CtaScope>);
static_assert(crucible::algebra::Lattice<ms::OuterScope>);
static_assert(crucible::algebra::BoundedLattice<ms::SystemScope>);
static_assert(std::is_empty_v<ms::ThreadScope::element_type>,
              "At<Thread>::element_type must be empty so that grading a payload "
              "with it collapses to sizeof(payload) — a scope annotation that "
              "costs no bytes at any binding site.");
static_assert(std::is_empty_v<ms::CtaScope::element_type>);
static_assert(std::is_empty_v<ms::OuterScope::element_type>);
static_assert(std::is_empty_v<ms::SystemScope::element_type>);
static_assert(ms::CtaScope::scope == MemoryScope::Cta,
              "At<S>::scope must equal S at the type level, so a consumer reads "
              "the pinned scope without carrying any runtime data.");
static_assert(ms::OuterScope::scope == MemoryScope::Outer);
static_assert(ms::SystemScope::scope == MemoryScope::System);

struct EightByteValue {
    unsigned long long v{0};
};
static_assert(sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, ms::CtaScope, EightByteValue>)
                  == sizeof(EightByteValue),
              "Pinning a Cta scope grade must add zero bytes to an 8-byte "
              "payload.");
static_assert(sizeof(crucible::algebra::Graded<crucible::algebra::ModalityKind::Absolute, ms::SystemScope, int>)
              == sizeof(int));

static_assert(L::name() == std::string_view{"MemoryScopeLattice"});
static_assert(ms::CtaScope::name() == std::string_view{"MemoryScopeLattice::At<Cta>"});
static_assert(ms::InnerScope::name() == std::string_view{"MemoryScopeLattice::At<Inner>"});
static_assert(ms::ThreadScope::name() == std::string_view{"MemoryScopeLattice::At<Thread>"});
static_assert(cal::memory_scope_name(MemoryScope::Gpu) == std::string_view{"Gpu"});

}  // namespace

int main() {
    cal::detail::memory_scope_lattice_self_test::runtime_smoke_test();
    return 0;
}

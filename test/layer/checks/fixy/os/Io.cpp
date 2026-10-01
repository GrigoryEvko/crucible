// The compile-time checks of fixy/os/Io.h.

#include <fixy/os/Io.h>

namespace fixy::io::detail::io_surface_invariants {

static_assert(ring_flag_bits_of(^^ring_flag::IoPoll) == IORING_SETUP_IOPOLL);
static_assert(ring_flag_bits_of(^^ring_flag::SqPoll) == IORING_SETUP_SQPOLL);
static_assert(ring_flag_bits_of(^^ring_flag::SingleIssuer) == IORING_SETUP_SINGLE_ISSUER);
static_assert(ring_flag_bits_of(^^ring_flag::CoopTaskrun) == IORING_SETUP_COOP_TASKRUN);
static_assert(ring_flag_bits_of(^^ring_flag::DeferTaskrun) == IORING_SETUP_DEFER_TASKRUN);

static_assert(EngineIsIoUring<engine::IoUring>);
static_assert(SimpleTransfer<zerocopy::CopyFileRange>);

// The three predicates are fail-closed: a tag they were never told about
// answers false.  These stand-ins are tags that fixy::io does not
// declare, and they witness that shape.
struct FutureEngine final {};
struct FutureZerocopy final {};
struct FutureRingFlag final {};
static_assert(!EngineIsIoUring<FutureEngine>,
              "an engine tag this surface was not told about must be refused, not admitted.");
static_assert(!SimpleTransfer<FutureZerocopy>);
static_assert(!MappedRingFlag<FutureRingFlag>, "a ring flag with no row must have no bits, not zero.");
static_assert(!MappedRingFlag<void>);
static_assert(MappedRingFlag<ring_flag::IoPoll>);
static_assert(!EngineIsIoUring<void>, "an empty pack names no engine, and void must not pass for one.");

static_assert(!is_pow2_(0));
static_assert(is_pow2_(1));
static_assert(is_pow2_(8));
static_assert(!is_pow2_(3));
static_assert(is_pow2_(32768));
static_assert(!is_pow2_(32769));

using A_Engine = ::fixy::atom::io::engine<engine::IoUring>;
using A_Sq8 = ::fixy::atom::io::sq_entries<8>;
using A_Sq7 = ::fixy::atom::io::sq_entries<7>;
using A_Cq16 = ::fixy::atom::io::cq_entries<16>;
using A_Sendfile = ::fixy::atom::io::zerocopy<zerocopy::Sendfile>;
using A_IoPoll = ::fixy::atom::io::ring_flag<ring_flag::IoPoll>;
using A_SqPoll = ::fixy::atom::io::ring_flag<ring_flag::SqPoll>;
using A_FutureFlag = ::fixy::atom::io::ring_flag<FutureRingFlag>;
using A_FutureEngine = ::fixy::atom::io::engine<FutureEngine>;

static_assert(is_engine_atom_v<A_Engine>);
static_assert(!is_engine_atom_v<A_Sendfile>);
static_assert(is_sq_entries_atom_v<A_Sq8>);
static_assert(atom_sq_entries_v<A_Sq8> == 8);
static_assert(is_zerocopy_atom_v<A_Sendfile>);
static_assert(is_ring_flag_atom_v<A_IoPoll>);

static_assert(std::is_same_v<engine_of_t<A_Engine, A_Sq8>, engine::IoUring>);
static_assert(std::is_same_v<engine_of_t<A_FutureEngine, A_Sq8>, FutureEngine>);
static_assert(sq_entries_of_v<A_Engine, A_Sq8> == 8);
static_assert(sq_entries_is_pow2_v<A_Engine, A_Sq8>);
static_assert(!sq_entries_is_pow2_v<A_Engine, A_Sq7>);
static_assert(cq_entries_is_pow2_or_default_v<A_Engine, A_Sq8>);
static_assert(cq_entries_of_v<A_Engine, A_Sq8> == 0, "no cq atom means the kernel default, which is a zero here.");
static_assert(cq_entries_of_v<A_Engine, A_Sq8, A_Cq16> == 16);

// The repeat rule reads the pack.  One flag named two times is a repeat,
// and two different flags fold together.
static_assert(::fixy::atom_pack::RepeatsAnAtomOf<^^::fixy::atom::io::ring_flag, A_IoPoll, A_IoPoll>);
static_assert(!::fixy::atom_pack::RepeatsAnAtomOf<^^::fixy::atom::io::ring_flag, A_IoPoll, A_SqPoll>);
static_assert(fold_ring_flags<A_IoPoll, A_SqPoll>() == (IORING_SETUP_IOPOLL | IORING_SETUP_SQPOLL));
static_assert(all_ring_flags_known_v<A_Engine, A_Sq8, A_IoPoll>);
static_assert(!all_ring_flags_known_v<A_Engine, A_Sq8, A_FutureFlag>);

static_assert(SimpleTransfer<zerocopy_of_t<A_Sendfile>>);
static_assert(!SimpleTransfer<zerocopy_of_t<A_Engine>>, "a pack with no zerocopy atom names no transfer.");

// The row derived from the pack is IO and Block.  This is the pin on
// that equality.
using ExpectedIoRow = eff::Row<eff::Effect::IO, eff::Effect::Block>;
static_assert(std::is_same_v<::fixy::atom_pack::atoms_row_t<A_Engine, A_Sq8, A_IoPoll>, ExpectedIoRow>);
static_assert(std::is_same_v<::fixy::atom_pack::atoms_row_t<A_Sendfile>, ExpectedIoRow>);

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;
using IoOnlyCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO>>;

static_assert(CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8>);
static_assert(CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_Cq16, A_IoPoll>);
static_assert(CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_IoPoll, A_SqPoll>);
static_assert(!CtxFitsIoUringMint<IoOnlyCtx, A_Engine, A_Sq8>,
              "a context without Block must not set up a ring: the call can park.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Sq8>, "a pack with no engine atom names nothing to set up.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine>, "a ring needs a submission-queue size.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq7>, "the submission count must be a power of two.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Engine, A_Sq8>, "two engine atoms in one pack.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_Sq8>, "two submission counts in one pack.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_Cq16, A_Cq16>, "two completion counts in one pack.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_IoPoll, A_IoPoll>);
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_Engine, A_Sq8, A_FutureFlag>,
              "a ring flag with no IORING_SETUP_* row must be refused, not folded to no bits.");
static_assert(!CtxFitsIoUringMint<IoBlockCtx, A_FutureEngine, A_Sq8>);
static_assert(!CtxFitsIoUringMint<IoBlockCtx>);

static_assert(CtxFitsZerocopyTransfer<IoBlockCtx, A_Sendfile>);
static_assert(!CtxFitsZerocopyTransfer<IoOnlyCtx, A_Sendfile>);
static_assert(!CtxFitsZerocopyTransfer<IoBlockCtx, A_Engine>);
static_assert(!CtxFitsZerocopyTransfer<IoBlockCtx, A_Sendfile, A_Sendfile>, "two transfers in one pack.");
static_assert(CtxFitsSendfileTransfer<IoBlockCtx, A_Sendfile> && !CtxFitsCopyRangeTransfer<IoBlockCtx, A_Sendfile>,
              "each form admits its own primitive only, so an offset that one form ignores is never taken.");

// The handle owns a descriptor and three mappings, so it is move-only,
// and the constructor that claims them is private with the mint as its
// sole friend.  The default handle owns nothing, which is why it stays
// public.
static_assert(std::is_default_constructible_v<IoUringRing>);
static_assert(std::is_nothrow_move_constructible_v<IoUringRing>);
static_assert(std::is_nothrow_move_assignable_v<IoUringRing>);
static_assert(!std::is_copy_constructible_v<IoUringRing>);
static_assert(!std::is_copy_assignable_v<IoUringRing>);
static_assert(!std::is_constructible_v<IoUringRing, int, void*, std::size_t, void*, std::size_t, void*, std::size_t,
                                       std::uint32_t, std::uint32_t>,
              "the constructor that claims a descriptor and three mappings must not be public: a caller who never "
              "called io_uring_setup could hand it numbers and the destructor would close and unmap them.");

}  // namespace fixy::io::detail::io_surface_invariants

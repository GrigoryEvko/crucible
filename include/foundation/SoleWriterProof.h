#pragma once

// Proof of the one writer
//   A SoleWriterProof<Owner, Role> proves by type that its holder is the one
//   writer of a counter of the role Role that Owner holds.  Only Owner makes a
//   proof: the constructor is private, and Owner is the one friend.  An owner
//   whose own types keep one writer of the counter makes the proof for that
//   writer, such as a channel whose producer handle holds a linear permission
//   and the claim of its role, or the producer claim of a single-producer
//   state (foundation/effects/Ctx.h), which one thread holds.  The publish of
//   the counter (advance_sole_writer_by_proof of fixy/Mutation.h) then writes
//   from the copy of the writer and loads nothing from the counter: the type
//   carries the check that a checked publish makes at run time.  A proof does
//   not copy or move, so a holder cannot keep it past the call that the owner
//   made it for.
//
// The proof is in foundation, so an owner of each layer can make one.
//
// Two threads that read one proof through a reference to const would both
// publish the counter of one writer.  So a proof is not Shareable
// (foundation/Transfer.h), and a door of concurrent threads refuses a body
// that borrows it.  The proof does not copy or move, so it is Sendable only
// in name: no value of it reaches another thread.

#include <foundation/ByteSeal.h>
#include <foundation/Transfer.h>

namespace foundation {

template <class Owner, class Role>
class [[nodiscard]][[= ::foundation::transfer::not_shareable{}]] SoleWriterProof final {
    constexpr SoleWriterProof() noexcept = default;
    friend Owner;

    // The seal refuses each byte route, such as std::bit_cast, so no bytes
    // become a proof.
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    SoleWriterProof(SoleWriterProof const&) = delete("a proof of the one writer lives for the call of its owner");
    SoleWriterProof& operator=(SoleWriterProof const&) = delete("a proof of the one writer lives for the call of its "
                                                                "owner");
    ~SoleWriterProof() = default;
};

}  // namespace foundation

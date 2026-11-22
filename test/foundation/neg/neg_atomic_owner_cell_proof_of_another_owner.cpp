// Negative-compile test. This file must fail to compile.
//
// The cell of an owner names its writer in its type (the cell of an owner in
// foundation/core/Atomic.h).  The owner of another class makes a proof of the
// same role, but the proof names that class, so the store of the cell has no
// overload for it.
//
// Expected diagnostic: no store_release of the cell takes the proof of the
// other owner.

#include <foundation/SoleWriterProof.h>
#include <foundation/core/Atomic.h>

#include <cstdint>

namespace {

struct Role final {};

class Owner;

using OwnedCell = ::foundation::core::Atomic<std::uint64_t, ::foundation::SoleWriterProof<Owner, Role>>;

class Owner final {
public:
    void store(OwnedCell& cell) noexcept { cell.store_release(::foundation::SoleWriterProof<Owner, Role>{}, 1); }
};

class OtherOwner final {
public:
    void store(OwnedCell& cell) noexcept { cell.store_release(::foundation::SoleWriterProof<OtherOwner, Role>{}, 2); }
};

}  // namespace

int main() {
    OwnedCell cell{};
    Owner{}.store(cell);
    OtherOwner{}.store(cell);
    return cell.load_acquire() == 2 ? 0 : 1;
}

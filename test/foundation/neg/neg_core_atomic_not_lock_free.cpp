// An Atomic holds only a value whose atomic operations take no lock.  A
// value of 16 bytes takes a lock in libatomic on this target, so the value
// gate refuses it.

#include <foundation/core/Atomic.h>

__extension__ typedef unsigned __int128 WideWord;
enum class WidePhase : WideWord {
    idle,
    busy
};

int main() { return ::foundation::core::Atomic<WidePhase>{}.load_acquire() == WidePhase::idle ? 0 : 1; }

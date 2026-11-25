// A LiveList names its one writer in its type.  A second class makes a proof of
// the same role, but the proof names that class, so the append has no overload
// for it.  The append of the first class compiles.

#include <foundation/SoleWriterProof.h>
#include <foundation/core/LiveList.h>
#include <foundation/effects/Effect.h>

struct AppendRole final {};

class FirstWriter;
using Numbers = ::foundation::core::LiveList<int, ::foundation::SoleWriterProof<FirstWriter, AppendRole>>;

class FirstWriter final {
public:
    static void append(Numbers& list) {
        list.append(::foundation::SoleWriterProof<FirstWriter, AppendRole>{},
                    ::foundation::effects::testing::test().alloc, 1);
    }
};

class SecondWriter final {
public:
    static void append(Numbers& list) {
        list.append(::foundation::SoleWriterProof<SecondWriter, AppendRole>{},
                    ::foundation::effects::testing::test().alloc, 2);
    }
};

int main() {
    Numbers list;
    FirstWriter::append(list);
    SecondWriter::append(list);
    return static_cast<int>(list.prefix().size());
}

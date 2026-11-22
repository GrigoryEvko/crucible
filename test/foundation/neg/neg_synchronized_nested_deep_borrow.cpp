// Negative-compile test.  This file must fail to compile.
//
// A nested handle of a class that states synchronized{} holds a reference to
// a plain class whose plain int lies four levels down.  No type tells which
// object the reference names, so the handle can write a plain part of the
// class, and its door bump takes no proof (the nested classes of
// foundation/Transfer.h).  The walk of a borrow reads each level of the class.
//
// Expected diagnostic: a door of a nested class can write the class, and the
// diagnostic names the door bump of the handle.

#include <foundation/Transfer.h>

namespace {

struct Level3 {
    int value = 0;
};
struct Level2 {
    Level3 inner{};
};
struct Level1 {
    Level2 inner{};
};
struct Level0 {
    Level1 inner{};
};

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    class Handle {
        Level0& deep_;

        explicit Handle(Level0& deep) noexcept : deep_{deep} {}
        friend class Counter;

    public:
        void bump() noexcept { ++deep_.inner.inner.inner.value; }
    };

private:
    Level0 levels_{};
};

}  // namespace

static_assert(::foundation::transfer::Synchronized<Counter>);

int main() { return 0; }

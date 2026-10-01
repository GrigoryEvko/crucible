// The contract rule of the tree rejects a P2900 `pre` specifier on a member
// function of a class template.  This is the form that GCC 16 does not keep in
// a header unit or in a precompiled header: an importer instantiates the
// member with no precondition.  The template and its instantiation share the
// specifier, so the rule gives one error.

namespace {

template <class T>
struct Holder {
    T held{};
    [[nodiscard]] constexpr T plus(T increment) const noexcept pre(increment > T{}) { return held + increment; }
};

}  // namespace

int main() { return Holder<int>{}.plus(1) - 1; }

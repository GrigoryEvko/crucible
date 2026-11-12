// The rule specialization-home of the quarantine plugin refuses a template of
// the namespace foundation that a file outside the base declares first.  The
// home of such a template is this file, so a specialization here is at home,
// and the namespace of the base gets a trait that no base file defines.  The
// plugin makes the declaration a compile error in each build.

namespace foundation::forged {

template <typename T>
struct planted_trait {
    static constexpr bool value = false;
};

template <>
struct planted_trait<int> {
    static constexpr bool value = true;
};

}  // namespace foundation::forged

int main() { return foundation::forged::planted_trait<int>::value ? 0 : 1; }

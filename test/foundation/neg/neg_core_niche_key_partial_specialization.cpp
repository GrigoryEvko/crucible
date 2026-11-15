// A partial specialization of niche_key for each instance of a class
// template, with a public constructor, would give each caller a key of the
// niche of each instance.  niche_key has no open mark, so the rule
// specialization-home refuses a partial specialization of it outside
// Choice.h, as it refuses an explicit one.

#include <foundation/core/Choice.h>

template <class T>
struct Boxed {
    T raw{};
};

template <class T>
class foundation::core::niche_key<Boxed<T>> {
public:
    constexpr niche_key() noexcept {}
};

int main() { return sizeof(foundation::core::niche_key<Boxed<int>>) == 1 ? 0 : 1; }

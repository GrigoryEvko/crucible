// A file outside Choice.h writes an explicit specialization of the template
// detail::alternative_of_, which the storage of a sum befriends.  A body of
// the read of an alternative in a different file can give a reference that
// no alternative holds.  The rule specialization-home refuses the
// specialization in each build, because a function template of the tree gets
// an explicit specialization only in its own file.

#include <foundation/core/Choice.h>

struct ProbeStorage {
    int value = 0;
};

template <>
constexpr auto& foundation::core::detail::alternative_of_<0, ProbeStorage>(ProbeStorage& storage) noexcept {
    return storage.value;
}

int main() { return 0; }

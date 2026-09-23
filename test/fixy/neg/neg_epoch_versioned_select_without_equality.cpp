// Two values at one version are one event only when their payloads agree,
// and a payload with no equality cannot show that.  So select_fresher does
// not exist for such a payload, and a conflict at one version cannot pass
// as one event.

#include <fixy/EpochVersioned.h>

struct Reading {
    int celsius = 0;
};

int main() {
    fixy::EpochVersioned<Reading> const first{Reading{1}, fixy::Epoch{4}, fixy::Generation{4}};
    fixy::EpochVersioned<Reading> const second{Reading{2}, fixy::Epoch{4}, fixy::Generation{4}};
    return fixy::select_fresher(first, second).has_value() ? 1 : 0;
}

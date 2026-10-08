// An explicit temporary names its class where the source spells it, and an
// implicit conversion to a parameter of a library type names nothing at the
// call.  utils/assay/guard/legacy/check_plugin.py expects one finding on each line of an explicit
// temporary below, and no finding on the line of each implicit conversion.

#include <string>
#include <vector>

template <class T>
void temporary_sink(T&& value) {
    static_cast<void>(value);
}

void temporary_take(std::vector<int> const& items);
void temporary_take_text(std::string const& text);

struct TemporaryLocal {
    int value = 0;
};

void temporary_use(char const* raw) {
    temporary_sink(std::vector<int>{1, 2});
    temporary_sink(std::string("abc"));
    temporary_sink(std::vector<int>());
    temporary_take({1, 2});
    temporary_take_text(raw);
    temporary_take_text("abc");
    temporary_sink(TemporaryLocal{1});
}

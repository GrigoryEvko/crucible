// A using-declaration in a function body or in a class, and a
// using-directive in a function body, name the library entity that they
// bring into scope.  check_plugin.py expects one finding on each line that
// spells one.  At namespace scope the front end keeps no tree with a
// location, and KNOWN_GAPS pins that gap.

#include <cstring>
#include <string>
#include <utility>

struct UsingHolder : std::string {
    using std::string::size;
};

void using_function(int& left, int& right) {
    using std::swap;
    swap(left, right);
}

template <class T>
void using_template(T& left, T& right) {
    using std::swap;
    swap(left, right);
}

void using_directive() {
    using namespace std::pmr;
}

void using_c_function(char* target, char const* source) {
    using std::memcpy;
    memcpy(target, source, 1);
}

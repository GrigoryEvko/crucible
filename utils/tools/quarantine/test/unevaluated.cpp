// Each library name in an unevaluated operand is a use of that name, and
// check_plugin.py expects one finding on each line that names std::string or
// std::vector below.  Outside a template, the front end keeps only the sizeof.

#include <string>
#include <vector>

unsigned long unevaluated_size() { return sizeof(std::string); }

template <class T>
unsigned long unevaluated_template_size() { return sizeof(std::vector<T>); }

template <class T>
unsigned long unevaluated_alignment() { return alignof(std::vector<T>); }

template <class T>
bool unevaluated_noexcept() { return noexcept(std::vector<T>{}); }

template <class T>
bool unevaluated_trait() { return __is_same(T, std::string); }

template <class T>
bool unevaluated_requires() { return requires(std::vector<T> items) { items.size(); }; }

template <class T>
void unevaluated_static_assert() {
    static_assert(sizeof(std::vector<T>) > 0);
}

template <class T>
void unevaluated_decltype() {
    decltype(std::vector<T>{}) items{};
    static_cast<void>(items);
}

template <class T>
struct UnevaluatedHolder {
    static_assert(sizeof(std::vector<T>) > 0);
    void held() requires(sizeof(std::vector<T>) > 0) {}
};

template <class T>
concept UnevaluatedConcept = sizeof(std::vector<T>) > 0;

template <class T>
    requires(sizeof(std::vector<T>) > 0)
void unevaluated_clause() {}

template <class T>
auto unevaluated_return() -> decltype(std::vector<T>{}) { return {}; }

// The names of library types that are not classes: a typedef, an enumeration
// and an alias template.  The test table admits none of the names that the
// expectations list, so each one is a std_entity finding.  It admits
// <type_traits> and std::tuple_size, so std::remove_cvref_t and
// std::tuple_size<T>::value are no findings.  An entry admits no longer name,
// so std::tuple_size_v is a finding.

#include <cstddef>
#include <new>
#include <tuple>
#include <type_traits>

std::size_t nonclass_count = 0;
std::byte nonclass_byte{};
std::nullptr_t nonclass_null = nullptr;
std::align_val_t nonclass_alignment{};
std::tuple_element_t<0, std::tuple<int>> nonclass_element = 0;
std::remove_cvref_t<const int&> nonclass_admitted = 0;
using NonclassSize = std::size_t;
NonclassSize nonclass_through_alias = 0;

template <class T>
constexpr auto nonclass_tuple_size = std::tuple_size<T>::value;
template <class T>
constexpr auto nonclass_tuple_size_v = std::tuple_size_v<T>;

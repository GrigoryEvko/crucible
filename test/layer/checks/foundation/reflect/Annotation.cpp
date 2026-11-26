// The compile-time checks of foundation/reflect/Annotation.h.

#include <foundation/reflect/Annotation.h>

namespace foundation::reflect {

namespace detail::annotation_checks {

struct FirstMark final {};
struct SecondMark final {};
struct OtherMark final {};

struct Plain {};
struct[[= FirstMark{}]] MarkedOnce {};
struct[[= FirstMark{}]][[= SecondMark{}]] MarkedTwice {};
struct[[= FirstMark{}]][[= FirstMark{}]] MarkedAgain {};
struct DerivedOfMarked : MarkedOnce {};
template <class T>
struct[[= SecondMark{}]] MarkedTemplate {};
enum class[[= OtherMark{}]] MarkedEnum : unsigned char {
    none
};
struct HoldsMarkedMember {
    [[= FirstMark{}]] int marked = 0;
    int plain = 0;
};
[[= OtherMark{}]] inline void marked_function() noexcept {}
[[= SecondMark{}]] inline constexpr int marked_variable = 0;

// An entity with no annotation, with one of the type, with more than one, and
// with one of another type.
static_assert(!carries_annotation(^^Plain, ^^FirstMark));
static_assert(carries_annotation(^^MarkedOnce, ^^FirstMark) && !carries_annotation(^^MarkedOnce, ^^SecondMark));
static_assert(carries_annotation(^^MarkedTwice, ^^FirstMark) && carries_annotation(^^MarkedTwice, ^^SecondMark));
static_assert(carries_annotation(^^MarkedAgain, ^^FirstMark) && !carries_annotation(^^MarkedAgain, ^^OtherMark));

// A derived class does not carry the annotation of its base.  Each kind of
// entity can carry one: an instance of a template, an enumeration, a member, a
// function and a variable.
static_assert(!carries_annotation(^^DerivedOfMarked, ^^FirstMark));
static_assert(carries_annotation(^^MarkedTemplate<int>, ^^SecondMark));
static_assert(!carries_annotation(^^MarkedTemplate<int>, ^^FirstMark));
static_assert(carries_annotation(^^MarkedEnum, ^^OtherMark) && !carries_annotation(^^MarkedEnum, ^^FirstMark));
static_assert(carries_annotation(^^HoldsMarkedMember::marked, ^^FirstMark));
static_assert(!carries_annotation(^^HoldsMarkedMember::plain, ^^FirstMark));
static_assert(carries_annotation(^^marked_function, ^^OtherMark)
              && !carries_annotation(^^marked_function, ^^FirstMark));
static_assert(carries_annotation(^^marked_variable, ^^SecondMark));

// The annotations of one type: their count, and the first one, whose value a
// read gives back.
struct CountMark final {
    int value = 0;
};
struct[[= CountMark{.value = 3}]][[= SecondMark{}]][[= CountMark{.value = 5}]] MarkedWithValues {};
static_assert(typed_annotations_of(^^Plain, ^^CountMark).count == 0);
static_assert(typed_annotations_of(^^Plain, ^^CountMark).first == std::meta::info{});
static_assert(typed_annotations_of(^^MarkedWithValues, ^^CountMark).count == 2);
static_assert(std::meta::extract<CountMark>(typed_annotations_of(^^MarkedWithValues, ^^CountMark).first).value == 3);
static_assert(typed_annotations_of(^^MarkedWithValues, ^^SecondMark).count == 1);

}  // namespace detail::annotation_checks

}  // namespace foundation::reflect

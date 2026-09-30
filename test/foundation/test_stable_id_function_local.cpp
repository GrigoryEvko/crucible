// A stable id is a function of the type, so two different types never
// share one id, and one type has one id in every translation unit.
//
// A class declared in a function body prints the name of the function
// and its own name.  The name of a function does not name each type that
// its template arguments and its parameters name: a class with internal
// linkage prints the same name in each unit.  So local_of<UnitLocal>
// declares one class in the first unit and a different class in the
// second unit, and the two print one name.  The number that each class
// holds shows that the two are different types.  A stable id that read
// the printed name gave the two classes one id, and every class declared
// in a function body is refused for that reason.
//
// The positive control shows that the two units agree on the id of a
// class with a declared name at namespace scope.

#include "stable_id_function_local.h"

#include <cstdio>
#include <cstdlib>

namespace stable_id_function_local {

namespace {

struct UnitLocal {
    static constexpr int unit_number = 1;
};

}  // namespace

}  // namespace stable_id_function_local

int main() {
    namespace sl = ::stable_id_function_local;
    int failures = 0;

    const sl::UnitView here = sl::view_of<sl::UnitLocal>();
    const sl::UnitView there = sl::view_of_second_unit();

    if (here.unit_number == there.unit_number) {
        std::fprintf(stderr,
                     "positive control failed: the two units build one class, number %d, so the test "
                     "compares a class with itself\n",
                     here.unit_number);
        ++failures;
    }
    if (here.named_id == 0 || here.named_id != there.named_id) {
        std::fprintf(stderr,
                     "positive control failed: a named class at namespace scope has the ids 0x%016llx and "
                     "0x%016llx in the two units\n",
                     static_cast<unsigned long long>(here.named_id), static_cast<unsigned long long>(there.named_id));
        ++failures;
    }
    if (here.template_local_id != 0 || there.template_local_id != 0) {
        std::fprintf(stderr,
                     "a class declared in a function body has a stable id: 0x%016llx in the first unit and "
                     "0x%016llx in the second.  The two are different classes, numbers %d and %d.\n",
                     static_cast<unsigned long long>(here.template_local_id),
                     static_cast<unsigned long long>(there.template_local_id), here.unit_number, there.unit_number);
        ++failures;
    }
    if (here.inline_local_id != 0 || there.inline_local_id != 0) {
        std::fprintf(stderr,
                     "a class declared in the body of an inline function has a stable id: 0x%016llx and "
                     "0x%016llx\n",
                     static_cast<unsigned long long>(here.inline_local_id),
                     static_cast<unsigned long long>(there.inline_local_id));
        ++failures;
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

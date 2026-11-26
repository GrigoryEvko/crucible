// The compile-time checks of crucible/ValidatedOp.h.

#include <crucible/ValidatedOp.h>
#include <foundation/reflect/Members.h>

#include <meta>

namespace crucible {

// True when member is a function that gives a MetaRun.
consteval bool gives_a_run(std::meta::info member) {
    return std::meta::is_function(member) && !std::meta::is_constructor(member) && !std::meta::is_destructor(member)
        && std::meta::return_type_of(member) == ^^MetaRun;
}

// records is the one public member of an op that gives a run.  The door of an
// op has no public member that gives one, so its run_, which makes the run of
// records, is private.  The test of the count in the door bounds the run by
// two counts of 16 bits.  The first search is the control of the others.
static_assert(gives_a_run(::foundation::reflect::member_named_in(^^ValidatedOp, std::meta::access_context::current(),
                                                                 "records")));
static_assert(::foundation::reflect::find_member(^^ValidatedOp, std::meta::access_context::current(),
                                                 [](std::meta::info member) {
                                                   return gives_a_run(member)
                                                       && !::foundation::reflect::has_identifier_named(member,
                                                                                                       "records");
                                                 })
              == std::meta::info{});
static_assert(::foundation::reflect::find_member(^^detail::ValidatedOpDoor, std::meta::access_context::current(),
                                                 [](std::meta::info member) { return gives_a_run(member); })
              == std::meta::info{});

// The op keeps its View, so a replay of an op builds no run: a Ref and a View.
static_assert(sizeof(ValidatedOp) == 24);

}  // namespace crucible

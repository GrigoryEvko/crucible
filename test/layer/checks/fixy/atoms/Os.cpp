// The compile-time checks of fixy/atoms/Os.h.

#include <fixy/atoms/Os.h>

namespace fixy::atom {

namespace detail::os_atom_self_test {

static_assert(every_roster_member_is_atom_<os_atom_roster>(),
              "fixy/atoms/Os.h: a member of os_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<os_atom_roster, Axis::SyscallSurface>(),
              "fixy/atoms/Os.h: every OS atom engages Axis::SyscallSurface.");
static_assert(every_roster_member_lifts_<os_atom_roster>(), "fixy/atoms/Os.h: every OS atom lifts to an effect row.");

static_assert(every_roster_member_lifts_to_<io_atom_roster, io_row>());
static_assert(every_roster_member_lifts_to_<fs_atom_roster, fs_row>());
static_assert(every_roster_member_lifts_to_<mmap_atom_roster, mmap_row>());
static_assert(every_roster_member_lifts_to_<leak_atom_roster, leak_row>());

static_assert(every_os_tag_namespace_holds_only_tags_(),
              "fixy/atoms/Os.h: a class declared in one of the ten tag namespaces has state, is not "
              "final, or is an atom.  A tag is an empty final type and nothing else.");

// The walk sees whatever is declared, so it cannot notice a tag that
// was deleted.  This count is what does.  Raise it when a namespace
// gains a tag, and say which one in the commit.
static_assert(os_tag_count_() == 45, "fixy/atoms/Os.h: the ten tag namespaces hold a different number of tags "
                                     "than this pin records.  A new tag raises the count; a tag that "
                                     "disappeared is a deletion somebody has to justify.");

// The ring sizes are readable off the atom.
static_assert(io::sq_entries<8>::value == 8);
static_assert(io::cq_entries<16>::value == 16);

// The leak concept admits the leak atom and nothing else.
using sample_leak = leak::resource<leak_sample_rationale>;
static_assert(IsLeakAtom<sample_leak>);
static_assert(IsLeakAtom<const sample_leak&>);
static_assert(!IsLeakAtom<int>);
static_assert(!IsLeakAtom<mmap::trusted_jit>);
static_assert(!IsLeakAtom<mmap::with_prot<::fixy::mmap::prot::ReadOnly>>);

}  // namespace detail::os_atom_self_test

}  // namespace fixy::atom

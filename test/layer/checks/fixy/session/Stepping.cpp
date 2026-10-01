// The compile-time checks of fixy/session/Stepping.h.

#include <fixy/session/Stepping.h>

namespace fixy::session {

namespace check {

static_assert(std::is_empty_v<Off>, "check::Off must be empty, or [[no_unique_address]] cannot collapse it and a "
                                    "handle under the unchecked policy stops costing exactly its Resource.");

static_assert(!std::is_empty_v<Enforced>, "check::Enforced must carry state.  An empty Enforced would be Off wearing "
                                          "the enforcing name: every handle would report itself consumed and the "
                                          "destructor check would pass for a leaked protocol.");

static_assert(!std::is_empty_v<Cancel>, "check::Cancel must carry state.  An empty Cancel cannot tell a live handle "
                                        "from a consumed one, so it cannot know when to send the cancellation.");

static_assert(!std::is_same_v<Enforced, Cancel>, "check::Enforced and check::Cancel must be different types, "
                                                 "because the policy is part of the handle type.");

static_assert(sizeof(Enforced) == 2 * sizeof(std::source_location),
              "the record index of fixy/session/Watch.h must sit in the padding after the flag, so that the watch "
              "costs a checked handle no byte.");

}  // namespace check

static_assert(AbandonmentPolicy<check::Enforced>);
static_assert(AbandonmentPolicy<check::Cancel>);
static_assert(AbandonmentPolicy<check::Off>);

}  // namespace fixy::session

// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A fusion decision carries the lane of the FUSE phase.  A consumer that
// demands a decision from a later phase refuses it: the two lanes are
// different tags, and no conversion joins them.

#include <crucible/forge/_wip/Phases/Comm.h>

namespace phase = crucible::forge::_wip::phases::comm;

using TiledDecision = ::fixy::Tagged<phase::FusedCommDecision, ::fixy::tags::source::ForgePhase<'F'>>;

inline void consume_tiled(TiledDecision) {}

inline void pass_fused(phase::DeclaredFusedCommDecision fused) { consume_tiled(fused); }

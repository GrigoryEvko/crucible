#pragma once

// One include for the whole fixy surface. Each header below is also
// independently includable when a translation unit needs only part of it.
//
// The macro lets a build target detect that this surface is available.

#define CRUCIBLE_FIXY 1

#include <crucible/fixy/_Default.h>
#include <crucible/fixy/_Dim.h>
#include <crucible/fixy/_Grant.h>
// Reject.h precedes Profile.h rather than following it alphabetically,
// because Profile.h instantiates a concept Reject.h declares.
#include <crucible/fixy/_Reject.h>
#include <crucible/fixy/_Profile.h>

#include <crucible/fixy/_Fn.h>

#include <crucible/fixy/Is.h>
#include <crucible/fixy/_Mach.h>
#include <crucible/fixy/Perm.h>
#include <crucible/fixy/Handle.h>
#include <crucible/fixy/SessContentAddr.h>
#include <crucible/fixy/SessEventLog.h>
#include <crucible/fixy/Struct.h>
#include <crucible/fixy/Wrap.h>

#include <crucible/fixy/Diag.h>
#include <crucible/fixy/_Insights.h>
#include <crucible/fixy/_Source.h>

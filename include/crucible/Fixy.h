#pragma once

// One include for the whole fixy surface. Each header below is also
// independently includable when a translation unit needs only part of it.
//
// The macro lets a build target detect that this surface is available.

#define CRUCIBLE_FIXY 1

#include <crucible/fixy/Default.h>
#include <crucible/fixy/Dim.h>
#include <crucible/fixy/Grant.h>
// Reject.h precedes Profile.h rather than following it alphabetically,
// because Profile.h instantiates a concept Reject.h declares.
#include <crucible/fixy/Reject.h>
#include <crucible/fixy/Profile.h>
#include <crucible/fixy/Rules.h>

#include <crucible/fixy/Fn.h>

#include <crucible/fixy/Bridge.h>
#include <crucible/fixy/Cap.h>
#include <crucible/fixy/Contract.h>
#include <crucible/fixy/Decide.h>
#include <crucible/fixy/Is.h>
#include <crucible/fixy/Mach.h>
#include <crucible/fixy/Perm.h>
#include <crucible/fixy/Handle.h>
#include <crucible/fixy/Pipe.h>
#include <crucible/fixy/Safety.h>
#include <crucible/fixy/Sess.h>
#include <crucible/fixy/SessGlobal.h>
#include <crucible/fixy/Mpst.h>
#include <crucible/fixy/SessDecl.h>
#include <crucible/fixy/SessCT.h>
#include <crucible/fixy/SessContentAddr.h>
#include <crucible/fixy/SessEventLog.h>
#include <crucible/fixy/SessSubtype.h>
#include <crucible/fixy/SessQueue.h>
#include <crucible/fixy/SessDiagnostic.h>
#include <crucible/fixy/SessContext.h>
#include <crucible/fixy/SessGrade.h>
#include <crucible/fixy/SessAssoc.h>
#include <crucible/fixy/SessDelegate.h>
#include <crucible/fixy/SessCheckpoint.h>
#include <crucible/fixy/SessRowExtraction.h>
#include <crucible/fixy/SessView.h>
#include <crucible/fixy/SessCrash.h>
#include <crucible/fixy/SessFederation.h>
#include <crucible/fixy/SessShape.h>
#include <crucible/fixy/Struct.h>
#include <crucible/fixy/Substr.h>
#include <crucible/fixy/Wrap.h>

#include <crucible/fixy/Algebra.h>
#include <crucible/fixy/Diag.h>
#include <crucible/fixy/Eff.h>
#include <crucible/fixy/Insights.h>
#include <crucible/fixy/Modality.h>
#include <crucible/fixy/Source.h>

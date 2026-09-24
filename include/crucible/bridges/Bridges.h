#pragma once

// SessionPersistence.h does not pull Cipher.h itself.  Lifting it to the
// umbrella keeps the whole-surface include unchanged, while direct
// consumers of SessionPersistence.h skip the heavy transitive.
#include <crucible/Cipher.h>
#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/MachineSessionBridge.h>
#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/bridges/SessionPersistence.h>

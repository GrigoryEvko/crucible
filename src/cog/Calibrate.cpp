#include <crucible/cog/Calibrate.h>

namespace crucible::cog {

std::string_view calibration_error_name(CalibrationError error) noexcept {
    switch (error) {
        case CalibrationError::None:                    return "None";
        case CalibrationError::ZeroCog:                 return "ZeroCog";
        case CalibrationError::KindMismatch:            return "KindMismatch";
        case CalibrationError::NonCalibratableCog:      return "NonCalibratableCog";
        case CalibrationError::InvalidIterations:       return "InvalidIterations";
        case CalibrationError::InvalidWarmupIterations:
            return "InvalidWarmupIterations";
        case CalibrationError::InvalidTrimBasisPoints:
            return "InvalidTrimBasisPoints";
        case CalibrationError::InvalidRuntimeBudgetMs:
            return "InvalidRuntimeBudgetMs";
        case CalibrationError::InvalidLatencyQuantiles:
            return "InvalidLatencyQuantiles";
        case CalibrationError::InvalidLatencyCycles:    return "InvalidLatencyCycles";
        case CalibrationError::InvalidThroughput:       return "InvalidThroughput";
        case CalibrationError::InvalidSampleCount:      return "InvalidSampleCount";
        case CalibrationError::EmptyOpcodeSet:          return "EmptyOpcodeSet";
        case CalibrationError::EmptyEntrySet:           return "EmptyEntrySet";
        case CalibrationError::DriftBelowThreshold:     return "DriftBelowThreshold";
        case CalibrationError::BackendUnavailable:      return "BackendUnavailable";
        case CalibrationError::InvalidDriftBasisPoints:
            return "InvalidDriftBasisPoints";
        default:                                        return "<unknown CalibrationError>";
    }
}

}  // namespace crucible::cog

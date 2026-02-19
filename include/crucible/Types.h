#pragma once

#include <cstdint>

namespace crucible {

// Mirror c10::ScalarType ordinals exactly so int8_t casts are compatible
// between the standalone library and the PyTorch Vessel adapter.
enum class ScalarType : int8_t {
  Byte = 0,
  Char = 1,
  Short = 2,
  Int = 3,
  Long = 4,
  Half = 5,
  Float = 6,
  Double = 7,
  ComplexHalf = 8,
  ComplexFloat = 9,
  ComplexDouble = 10,
  Bool = 11,
  BFloat16 = 15,
  Float8_e5m2 = 23,
  Float8_e4m3fn = 24,
  Float8_e5m2fnuz = 25,
  Float8_e4m3fnuz = 26,
  Undefined = -1,
};

// Byte size per dtype.
constexpr uint8_t element_size(ScalarType t) {
  switch (t) {
    case ScalarType::Bool:
    case ScalarType::Byte:
    case ScalarType::Char:
    case ScalarType::Float8_e5m2:
    case ScalarType::Float8_e4m3fn:
    case ScalarType::Float8_e5m2fnuz:
    case ScalarType::Float8_e4m3fnuz:
      return 1;
    case ScalarType::Short:
    case ScalarType::Half:
    case ScalarType::BFloat16:
      return 2;
    case ScalarType::Int:
    case ScalarType::Float:
    case ScalarType::ComplexHalf:
      return 4;
    case ScalarType::Long:
    case ScalarType::Double:
    case ScalarType::ComplexFloat:
      return 8;
    case ScalarType::ComplexDouble:
      return 16;
    default:
      return 0;
  }
}

// Mirror c10::DeviceType ordinals.
enum class DeviceType : int8_t {
  CPU = 0,
  CUDA = 1,
  XLA = 9,
  HIP = 20,
};

// Mirror c10::Layout ordinals.
enum class Layout : int8_t {
  Strided = 0,
  Sparse = 1,
  SparseCsr = 2,
  SparseCsc = 3,
  SparseBsr = 4,
  SparseBsc = 5,
};

} // namespace crucible

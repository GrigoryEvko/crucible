"""Write the PyTorch tables of the storage float test.

The test test_core_storage_float compares the conversions of
include/foundation/core/StorageFloat.h with the results that PyTorch gives.
This script asks PyTorch one time and writes two binary files:

  torch_widen.bin  the binary32 bits that PyTorch gives for each pattern:
                   65536 words of float16, 65536 words of bfloat16, then 256
                   words of float8_e4m3fn and 256 words of float8_e5m2
  torch_round.bin  one row of 12 bytes for each boundary input: the binary32
                   bits of the input, then the float16 bits, the bfloat16
                   bits, the float8_e4m3fn byte, the float8_e5m2 byte and two
                   zero bytes

Each word is little-endian.  Each file starts with a header of 128 ASCII
bytes that names the layout, the PyTorch version and the CPU capability.

PyTorch gives one rounding mode for each format.  float16, bfloat16 and
float8_e5m2 give an infinity for an overflow.  float8_e4m3fn gives the
highest finite value for an overflow and for an infinity, which is the
saturating mode of the OCP 8-bit floating point specification.

The script converts one element at a time with Tensor.to, then converts all
the inputs in one tensor.  It stops with an error when the two runs give
different bits, so the table holds one answer for each input.

Run it with the Python of .venv-torch, from any directory:

  .venv-torch/bin/python -I test/fixy/storage_float/make_torch_tables.py

The files of 2026-11-16 came from PyTorch 2.15.0a0+git8be2400 with the CPU
capability AVX512.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path

import torch

HEADER_BYTES = 128
SAMPLE_SEED = 0x5EED5EED5EED5EED
UNIFORM_SAMPLES = 4096
RANGED_SAMPLES = 4096


@dataclass(frozen=True)
class Format:
    """The parameters of one storage format, and its PyTorch dtype."""

    name: str
    dtype: torch.dtype
    exponent_bits: int
    trailing_bits: int
    has_infinity: bool

    @property
    def bias(self) -> int:
        """Return the exponent bias."""
        return (1 << (self.exponent_bits - 1)) - 1

    @property
    def highest(self) -> int:
        """Return the code of the largest finite magnitude."""
        if self.has_infinity:
            return (((1 << self.exponent_bits) - 2) << self.trailing_bits) | ((1 << self.trailing_bits) - 1)
        return (1 << (self.exponent_bits + self.trailing_bits)) - 2

    def value_of(self, code: int) -> Fraction:
        """Return the exact value of a finite magnitude code."""
        exponent = code >> self.trailing_bits
        trailing = code & ((1 << self.trailing_bits) - 1)
        if exponent == 0:
            return Fraction(trailing) * Fraction(2) ** (1 - self.bias - self.trailing_bits)
        significand = (1 << self.trailing_bits) + trailing
        return Fraction(significand) * Fraction(2) ** (exponent - self.bias - self.trailing_bits)

    def next_above_highest(self) -> Fraction:
        """Return the value one unit above the highest finite value."""
        highest = self.value_of(self.highest)
        return highest + (highest - self.value_of(self.highest - 1))


FORMATS = (
    Format("float16", torch.float16, 5, 10, True),
    Format("bfloat16", torch.bfloat16, 8, 7, True),
    Format("float8_e4m3fn", torch.float8_e4m3fn, 4, 3, False),
    Format("float8_e5m2", torch.float8_e5m2, 5, 2, True),
)


def binary32_bits_of(value: Fraction) -> int:
    """Return the binary32 bits of a value that binary32 holds exactly."""
    bits = struct.unpack("<I", struct.pack("<f", float(value)))[0]
    held = Fraction(struct.unpack("<f", struct.pack("<I", bits))[0])
    if held != value:
        raise ValueError(f"binary32 does not hold {value} exactly")
    return bits


def selected_codes(fmt: Format) -> list[int]:
    """Return the finite codes whose values and ties the table holds.

    The codes are the four lowest and the four highest trailing values of
    each binade, the subnormal binade too, and the highest code.
    Complexity: O(binades).
    """
    top = (1 << fmt.trailing_bits) - 1
    trailing_values = sorted({0, 1, 2, 3, top - 3, top - 2, top - 1, top} & set(range(top + 1)))
    codes = set()
    for exponent in range((fmt.highest >> fmt.trailing_bits) + 1):
        for trailing in trailing_values:
            code = (exponent << fmt.trailing_bits) | trailing
            if code <= fmt.highest:
                codes.add(code)
    codes.add(fmt.highest)
    return sorted(codes)


def boundary_magnitudes(fmt: Format) -> list[int]:
    """Return the binary32 magnitudes at the boundaries of one format.

    Each selected code gives its value.  Each selected code gives the tie
    between its value and the value of the next code, with the binary32
    neighbours of the tie.  The highest code gives the tie with the value one
    unit above it, which decides the overflow.
    """
    magnitudes = []
    for code in selected_codes(fmt):
        magnitudes.append(binary32_bits_of(fmt.value_of(code)))
        above = fmt.next_above_highest() if code == fmt.highest else fmt.value_of(code + 1)
        tie = binary32_bits_of((fmt.value_of(code) + above) / 2)
        magnitudes.extend([tie - 1, tie, tie + 1])
    # One unit above the highest bfloat16 is 2^128, past the binary32 range.
    if fmt.next_above_highest() * 2 < Fraction(2) ** 128:
        above = binary32_bits_of(fmt.next_above_highest())
        magnitudes.extend([above - 1, above, above + 1, binary32_bits_of(2 * fmt.next_above_highest())])
    return magnitudes


def special_magnitudes() -> list[int]:
    """Return the zeros, the binary32 edges, the infinity and each NaN class.

    The NaN classes are each single payload bit, the quiet NaN with no
    payload, and the NaN with each bit of the significand set.
    """
    magnitudes = [0x00000000, 0x00000001, 0x00000002, 0x007FFFFF, 0x00800000, 0x00800001, 0x7F7FFFFF, 0x7F800000]
    magnitudes.extend(0x7F800000 | (1 << position) for position in range(23))
    magnitudes.extend([0x7FC00001, 0x7FE00000, 0x7FD00000, 0x7FBFFFFF, 0x7FFFFFFF, 0x7FC0FFFF, 0x7FA00000])
    return magnitudes


def splitmix64(state: int) -> tuple[int, int]:
    """Return the next state and the next word of a SplitMix64 stream."""
    state = (state + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    word = state
    word = ((word ^ (word >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
    word = ((word ^ (word >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
    return state, word ^ (word >> 31)


def sampled_inputs() -> list[int]:
    """Return the fixed-seed sample: uniform bit patterns, then patterns with
    an exponent field in [96, 160], where the narrow formats hold values."""
    state = SAMPLE_SEED
    inputs = []
    for _ in range(UNIFORM_SAMPLES):
        state, word = splitmix64(state)
        inputs.append(word & 0xFFFFFFFF)
    for _ in range(RANGED_SAMPLES):
        state, word = splitmix64(state)
        sign = (word >> 63) << 31
        exponent = 96 + (word >> 40) % 65
        inputs.append(sign | (exponent << 23) | (word & 0x7FFFFF))
    return inputs


def round_inputs() -> list[int]:
    """Return each input of the round table, with each sign, in a fixed order
    with no duplicate."""
    magnitudes = special_magnitudes()
    for fmt in FORMATS:
        magnitudes.extend(boundary_magnitudes(fmt))
    inputs = []
    for magnitude in magnitudes:
        inputs.extend([magnitude, magnitude | 0x80000000])
    inputs.extend(sampled_inputs())
    return list(dict.fromkeys(inputs))


def to_signed(word: int, bits: int) -> int:
    """Return the signed value of an unsigned word of the given width."""
    return word - (1 << bits) if word >= 1 << (bits - 1) else word


def storage_view_dtype(fmt: Format) -> torch.dtype:
    """Return the integer dtype that views the bits of the format."""
    return torch.int16 if fmt.exponent_bits + fmt.trailing_bits == 15 else torch.uint8


def round_with_torch(inputs: list[int], fmt: Format) -> list[int]:
    """Return the bits of Tensor.to for each binary32 input, one element at a
    time.  Stop when a conversion of all the inputs in one tensor differs."""
    mask = (1 << (1 + fmt.exponent_bits + fmt.trailing_bits)) - 1
    view = storage_view_dtype(fmt)
    single = []
    for word in inputs:
        tensor = torch.tensor([to_signed(word, 32)], dtype=torch.int32).view(torch.float32)
        single.append(int(tensor.to(fmt.dtype).view(view)[0]) & mask)
    together = torch.tensor([to_signed(word, 32) for word in inputs], dtype=torch.int32).view(torch.float32)
    bulk = [int(value) & mask for value in together.to(fmt.dtype).view(view).tolist()]
    if bulk != single:
        raise SystemExit(f"{fmt.name}: a conversion of one element and of all elements give different bits")
    return single


def widen_with_torch(fmt: Format) -> list[int]:
    """Return the binary32 bits of Tensor.to(float32) for each pattern of the
    format, one element at a time.  Stop when a conversion of all the patterns
    in one tensor differs."""
    width = 1 + fmt.exponent_bits + fmt.trailing_bits
    view = storage_view_dtype(fmt)
    patterns = [to_signed(word, width) if view == torch.int16 else word for word in range(1 << width)]
    single = []
    for pattern in patterns:
        tensor = torch.tensor([pattern], dtype=view).view(fmt.dtype)
        single.append(int(tensor.to(torch.float32).view(torch.int32)[0]) & 0xFFFFFFFF)
    together = torch.tensor(patterns, dtype=view).view(fmt.dtype)
    bulk = [int(value) & 0xFFFFFFFF for value in together.to(torch.float32).view(torch.int32).tolist()]
    if bulk != single:
        raise SystemExit(f"{fmt.name}: a widen of one element and of all elements give different bits")
    return single


def header_of(layout: str) -> bytes:
    """Return the ASCII header that names the layout and the source."""
    text = f"{layout}; torch {torch.__version__}; cpu {torch.backends.cpu.get_cpu_capability()}"
    if len(text) >= HEADER_BYTES:
        raise SystemExit(f"the header '{text}' has more than {HEADER_BYTES - 1} characters")
    return text.ljust(HEADER_BYTES - 1).encode("ascii") + b"\n"


def write_widen_table(path: Path) -> None:
    """Write torch_widen.bin."""
    words = []
    for fmt in FORMATS:
        words.extend(widen_with_torch(fmt))
    payload = struct.pack(f"<{len(words)}I", *words)
    path.write_bytes(header_of("widen v1: f16 bf16 e4m3fn e5m2, u32 each") + payload)


def write_round_table(path: Path) -> int:
    """Write torch_round.bin, and return its count of rows."""
    inputs = round_inputs()
    columns = [round_with_torch(inputs, fmt) for fmt in FORMATS]
    rows = bytearray()
    for index, word in enumerate(inputs):
        f16, bf16, e4m3, e5m2 = (column[index] for column in columns)
        rows += struct.pack("<IHHBBH", word, f16, bf16, e4m3, e5m2, 0)
    path.write_bytes(header_of("round v1: u32 in, u16 f16, u16 bf16, u8 e4m3fn, u8 e5m2, u16 0") + bytes(rows))
    return len(inputs)


def main() -> int:
    """Write the two tables into the directory of this script, or into --out."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent)
    arguments = parser.parse_args()
    write_widen_table(arguments.out / "torch_widen.bin")
    rows = write_round_table(arguments.out / "torch_round.bin")
    print(f"wrote {arguments.out}/torch_widen.bin and {arguments.out}/torch_round.bin with {rows} rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())

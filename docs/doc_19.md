# AX Documentation 19

AX Language — native production reference.

## Overview
Document 19 covers a single-responsibility part of AX for AI/ML and AAA games on Linux ARM64.

## Design
- Formula-first, AOT compiled to native ARM64.
- Zero-GC deterministic memory, ownership + arenas.
- Dependent tensor shape checking.
- Hardware: @cuda @vulkan @neon @sve @maxpower @autodiff

## Usage
See examples/ and scripts/build_arm64.sh. Build with cmake.

## Example
```ax
fn main() -> i32:
    x: Tensor[f32, 4] = tensor([1.0, 2.0, 3.0, 4.0])
    print(x * 2.0 + 1.0)
    return 0
```

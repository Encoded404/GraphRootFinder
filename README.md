a app for finding the root of a function with different methods.

## Prerequisites

- CMake ≥ 3.28
- a C++20-capable compiler with module support
- vcpkg available and `VCPKG_ROOT` set for the default preset

## VCPKG installation
you can follow the guide [here](https://github.com/microsoft/vcpkg) for installation.

## Clone

```bash
git clone <repo-url>
cd GraphRootFinder
```

## Configure

Use the provided preset:

```bash
cmake -S . -B build --preset default
```

Useful options:

- `-DBUILD_TESTING=OFF` to skip tests
- `-DENABLE_LOGGING=OFF` to compile out logging macros
- `-DCLANG_TIDY_ENABLED=OFF` to skip clang-tidy setup

If you want a manual configure step instead of the preset:

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DBUILD_TESTING=ON
```

## Build

```bash
cmake --build build --config Debug   # or Release/RelWithDebInfo
```

## Tests

```bash
ctest --test-dir build -C Debug
```

## Clang-Tidy

Set `-DCLANG_TIDY_ENABLED=ON` to enable static analysis when `clang-tidy` is available and a `.clang-tidy` file exists at the project root. Set it to `OFF` to skip clang-tidy configuration.

## update from upstream
if you want to pull from the upstream repo you can run the following commands in a terminal

```bash
git fetch upstream
git rebase upstream/main # or merge, or change the main to pull only a tag or a commit etc.
```
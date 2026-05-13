# C++26 static reflection

Minimal notes and sample code for WG21 P2996.

## Requirements

[clang-p2996](https://github.com/bloomberg/clang-p2996/tree/p2996) — experimental Clang for P2996.

## Build

- Build `clang++` compiler and set `LLVM_ROOT` to the directory that contains `bin/clang++`.
- Execute below command to generate executable.

```sh
cmake -S . -B build -G Ninja  #   Or "cmake.generator": "Ninja" in .vscode/setting.json
cmake --build build
```


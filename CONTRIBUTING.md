# Contributing to Nemulator

Thanks for your interest in Nemulator! This project is open and community-friendly.
Here's how to help.

---

## 🚦 Our Values

- **Clean-room & legitimate** — Nemulator is intended for homebrew, preservation,
  and testing **user-owned** software. We do **not** host or redistribute
  proprietary Nintendo keys, firmware, or copyrighted game assets.
- **Correctness-first** — every subsystem is validated by automated tests. Please
  keep the **29 test suites green**.
- **HLE-first, budget-aware** — we emulate at the Horizon OS level where sensible
  and we engineer hard for the Xbox Developer Mode **5 GiB memory budget**.

---

## 🛠 Getting Started

1. **Fork** the repo and clone it.
2. **Build** — see [`docs/XBOX_BUILD.md`](docs/XBOX_BUILD.md) or:
   ```bash
   mkdir -p build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   cmake --build . -j
   ctest              # 29/29 should pass
   ```
3. **Read the docs** — start with [`docs/OVERVIEW.md`](docs/OVERVIEW.md), then
   the subsystem design docs (GPU, Kernel, CPU, Memory, JIT, Audio, Input) that
   match your area.

---

## 🧭 Picking an Issue

- Check the **Issues** tab. Look for `good first issue` / `help wanted` labels.
- For an emulator like this, high-impact areas:
  - **Shader translation** — implement more Maxwell SASS opcode families
    (see `src/core/gpu/shader/`).
  - **IPC services** — flesh out a Horizon service's command handlers
    (see `src/core/kernel/ipc/`).
  - **Syscalls** — complete the Horizon SVC surface (`src/core/kernel/svc.cpp`).
  - **5 GiB budget** — memory dedup, cache pressure (see `docs/OPTIMIZATION_PLAYBOOK.md`).
  - **Docs & tests** — more coverage is always welcome.

---

## 🧪 Before You Submit

- **Run the full suite**: `cd build && ctest` — **all 29 must pass**.
- **Both toolchains** matter: Linux (GCC) *and* Windows/Xbox (MinGW-w64)
  cross-builds should be clean. Build with `cmake --build build-win -j2`.
- **Style**: C++20, `u8`/`s8`/`u32`/`s64` typedefs (not raw `int`),
  `NEMU_LOG_*` logging, namespace layout `nemu::core::…`.
  Keep changes **KISS/DRY** — match the surrounding code, no dead code.
- Add/update a **unit test** in `tests/unit/` for any behavior change.

---

## 📝 Pull Request Checklist

- [ ] Branch off `main`, descriptive title & body.
- [ ] Builds clean on Linux **and** the Windows cross-target.
- [ ] `ctest` — 29/29 green.
- [ ] Tests added/updated for new behavior.
- [ ] Rebased / no merge conflicts.

---

## 🔒 Reporting Vulnerabilities

Open a **Security Advisory** (or contact the maintainer privately) rather than
a public issue. See [`SECURITY.md`](SECURITY.md). Do **not** publish exploit
details before a fix lands.

---

## 🗣 Community

- Use **GitHub Discussions** for questions and ideas.
- Use **Issues** for bugs and feature requests.

Thank you for helping make Nemulator great! 🎮
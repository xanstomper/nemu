# Security Policy

Nemulator values the security of its users and the broader emulation community.

---

## Reporting a Vulnerability

Please **do not open a public issue** for security vulnerabilities. Instead:

- **Preferred:** open a [private Security Advisory](https://github.com/xanstomper/nemu/security/advisories/new)
  on the repository.
- **Alternative:** email the maintainer privately (see your local `git` config
  or the repo admin).

We ask that you **refrain from disclosing** the vulnerability details publicly
until a fix has been released, out of consideration for users who build
third-party content.

---

## Supported

| Version | Status |
| :--- | :--- |
| `main` (latest) | ✅ Supported — please test against the latest commit |

---

## Scope

Our focus areas for security review:

- **Memory safety** of the JIT, fastmem, and host-pointer translation
  (`src/core/cpu/`, `src/core/memory/`).
- **Kernel/HLE robustness** — malformed guest syscalls, IPC, and service
  commands should not crash the host (`src/core/kernel/`).
- **Format parsers** — NCA/NSO/NRO/PFS0/RomFS parsing against hostile or
  corrupt inputs (`src/core/loader/`, `src/core/filesystem/`).
- **Sandboxing** of the virtual filesystem / save paths.

## Bug bounty

This project does not currently offer a paid bug bounty. We are, however,
grateful for responsible disclosure and will credit reporters in release notes
when requested.

---

*Legality: Nemulator is a clean-room, legitimate-use emulator. We do not
facilitate piracy; please test with software you own.*
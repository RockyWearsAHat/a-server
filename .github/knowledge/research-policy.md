# Spec-First Research Policy

> **MANDATORY** for all emulator development and hardware-related work.

## Primary Sources (use FIRST)

These are the ONLY sources that can be treated as authoritative truth:

### GBA Hardware

- **GBATEK** (Martin Korth) — Definitive GBA/NDS/DSi technical reference
  - CPU (ARM7TDMI), memory map, I/O registers, timing, DMA, sound, video
  - https://problemkaputt.de/gbatek.htm
- **ARM7TDMI Technical Reference Manual** (ARM Ltd.) — CPU instruction encoding and behavior
- **GBA Programming Manual** (Nintendo internal dev docs, where available)

### PS1 Hardware

- **NOCASH PSX Specifications** (Martin Korth) — Definitive PS1 hardware reference
  - CPU (R3000A), GPU, GTE, SPU, DMA, timers, CDROM, memory map, I/O registers
  - https://problemkaputt.de/psx-spx.htm
- **MIPS R3000A Architecture Manual** (MIPS Technologies) — CPU instruction set
- **Sony PS1 developer documentation** (internal SDK docs, where available)
- **LSI Logic L64360 datasheet** (GTE hardware reference)

### Nintendo Switch

- **ARM Architecture Reference Manual ARMv8-A** (ARM Ltd.)
- **Nintendo Switch SDK documentation** (where available)

## Secondary Sources (verify against primary)

- Community wikis (copetti.org, wiki.higan.dev, etc.)
- Academic papers on hardware reverse-engineering
- Community test ROM results (useful as cross-checks, NOT as spec authority)

## No Hardware Verification

We do not have access to original hardware for testing. Do NOT propose "hardware validation", "testing on real hardware", or "needs hardware verification" as a resolution strategy — ever. The hardware was designed to perform in a specific, documented way. Our job is to find and implement that exact specification.

Official technical documentation — developer manuals, SDK guides, hardware reference sheets, datasheets — written for the people who designed for or manufactured the hardware is the ONLY path to 100% accuracy. R&D's job is to find these documents, not to propose hardware testing we cannot perform.

"Needs hardware testing" is never a valid conclusion. "Needs the official spec section for X" is.

## Prohibited as Primary Sources

**Do NOT use other emulator implementations as authoritative references:**

- mGBA, VBA, VBA-M (GBA)
- PCSX-R, Duckstation, Mednafen, XEBRA (PS1)
- yuzu, Ryujinx (Switch)

Our emulators are **independent implementations** built from official specs. We must NOT:

- Copy behavioral quirks from other emulators
- Assume another emulator's behavior is correct
- Use "mGBA does X" as justification for a design choice
- Import bug-for-bug compatibility with other emulators
- Use other emulators as a substitute for official documentation

If another emulator's source code is consulted for research leads, the finding MUST be verified against the primary spec before implementation. Other people's emulators may be wrong, incomplete, or less accurate than ours — they are never authoritative.

## Research Workflow

1. **Check cached knowledge first**: Look in `.github/knowledge/` and `/memories/repo/` before researching
2. **Start from official docs**: GBATEK, NOCASH PSX, ARM manuals
3. **Verify community claims**: Any community-sourced information must be confirmed against official specs
4. **Document findings**: Write new knowledge to `.github/knowledge/` so it's available next time
5. **Cite sources**: When documenting hardware behavior, note which spec section the behavior comes from

## Knowledge Integrity Rules

- Knowledge docs must honestly state what works and what doesn't
- Do NOT describe the emulator as "mature" or "highly accurate" without evidence from real game testing
- Gap analysis must be specific: name the games, describe the symptoms, identify the subsystem
- Use diagnostic logging (`AIO_PS1_GPU_DIAG=1`, etc.) to investigate before speculating about root causes

### Code over comments

- When determining whether a feature works, read the **code body** — not the comments, not the method name, not the header declaration.
- A comment saying `// handles video frame reception` on an empty method is a lie, not documentation. It is worse than no comment because it actively misleads.
- Stubs, empty handlers, signals that are never emitted, and endpoints that return hardcoded responses are NOT working features. Document them as "not yet implemented".
- When reviewing or auditing code, if comments contradict the code's actual behavior, the comment is the defect. Flag it for correction.
- This rule applies to ALL subsystems, not just emulators.

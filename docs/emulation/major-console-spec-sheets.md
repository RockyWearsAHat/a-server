# Major Console Spec Sheets for Emulator Implementation

Last updated: 2026-04-15

## Scope and intent

This is a manufacturer-complete, implementation-oriented baseline for major console makers. It does not claim NDA-level silicon secrets; it captures the highest-confidence public hardware facts and the exact references to use when building cycle-accurate emulators.

Use this as the top-level map. For production emulator work, create one deep-dive design doc per console family (CPU core, memory map, interrupts, DMA, video, audio, IO, media, timing tests).

## Source confidence tiers

- Tier 1: Official vendor manuals, SDK docs, CPU/GPU datasheets, patent filings.
- Tier 2: Primary technical references with broad expert validation (GBATEK, PSX-SPX, fullsnes, NESdev wiki/specs, MAME source + driver notes).
- Tier 3: Aggregated specs (Wikipedia, console databases). Use for orientation, not final arbitration.

## Nintendo

| Console | Core CPU / clock | Main memory | Graphics / audio anchors | Media | Emulator-critical notes | Primary references |
| --- | --- | --- | --- | --- | --- | --- |
| NES / Famicom | Ricoh 2A03 (6502-derived), ~1.79 MHz NTSC | 2 KB RAM (base) | 2C02 PPU, APU in 2A03 | Cartridge (mappers) | Mapper behavior, PPU cycle timing, OAM DMA, DMC timing | NESdev wiki (T2), 6502 docs (T1), cart board docs (T2) |
| SNES / Super Famicom | Ricoh 5A22 (65C816-based), ~3.58 MHz nominal | 128 KB WRAM | PPU1/PPU2, SPC700 + DSP | Cartridge (coprocessors) | DMA/HDMA timing, PPU mode timing, enhancement chips (SA-1/SFX) | fullsnes/GBATEK SNES (T2), 65C816 docs (T1) |
| N64 | NEC VR4300 (MIPS III), ~93.75 MHz | 4 MB RDRAM (8 MB with Expansion Pak) | RSP + RDP, SGI pipeline | Cartridge | RDP combiner accuracy, VI timing, microcode variants | N64brew + VR4300 manual (T2/T1), SGI docs (T1/T2) |
| GameCube | IBM PowerPC "Gekko" ~485 MHz | 24 MB 1T-SRAM + 16 MB DRAM | ATI "Flipper" | Optical miniDVD | DSP/audio pipeline, GX FIFO behavior, texture cache effects | YAGCD/TGC docs (T2), IBM PPC manuals (T1) |
| Wii | IBM PowerPC "Broadway" ~729 MHz | 24 MB + 64 MB | ATI "Hollywood" | Optical disc | IOS/Starlet interaction, timing of PPC<->IO subsystems | WiiBrew docs (T2), PPC manuals (T1) |
| Wii U | IBM PowerPC tri-core "Espresso" | 2 GB DDR3 | AMD Latte + embedded subsystems | Optical + eMMC | OS/service emulation boundaries, GPU command behavior | Wii U reverse-engineering docs (T2), PPC manuals (T1) |
| Game Boy / GB Color | Sharp LR35902 ~4.19 MHz / 8.39 MHz (CGB double-speed) | 8 KB WRAM (GB), 32 KB WRAM (CGB) | DMG/CGB PPU, APU | Cartridge | PPU mode timing, OAM corruption edge cases, MBC quirks | Pan Docs (T2), LR35902 docs (T2) |
| GBA | ARM7TDMI ~16.78 MHz | 256 KB EWRAM + 32 KB IWRAM | LCD controller + DMA + PSG/FIFO | Cartridge | Per-cycle memory waitstates, DMA start points, IRQ edge timing | GBATEK (T2), ARM7TDMI TRM (T1) |
| DS | ARM946E-S + ARM7TDMI | 4 MB main RAM | Dual 2D engines + 3D core | Cartridge | ARM9/ARM7 synchronization, VRAM banking, IPC timing | GBATEK NDS (T2), ARM docs (T1) |
| 3DS | ARM11 MPCore + ARM9 security core | 128 MB FCRAM (retail baseline) | PICA200 | Cartridge + digital | GPU command stream + service layer behavior | 3DBrew docs (T2), ARM docs (T1) |
| Switch | NVIDIA Tegra X1 (ARMv8) | 4 GB LPDDR4 | Maxwell-based GPU | Cartridge + digital | Accuracy often requires HLE+LLE hybrid; kernel/service fidelity dominates | NVIDIA/ARM docs (T1), Switchbrew (T2) |

## Sega

| Console | Core CPU / clock | Main memory | Graphics / audio anchors | Media | Emulator-critical notes | Primary references |
| --- | --- | --- | --- | --- | --- | --- |
| Master System / Mark III | Z80 ~3.58 MHz | 8 KB RAM | VDP (TMS9918-family lineage), PSG | Cartridge | VDP timing, region differences, mapper behavior | SMS Power docs (T2), Z80 docs (T1) |
| Genesis / Mega Drive | Motorola 68000 ~7.67 MHz + Z80 | 64 KB main + 64 KB VRAM | VDP, YM2612 + PSG | Cartridge | 68K<->Z80 bus arbitration, VDP DMA timing, YM2612 ladder effects | Sega technical overviews (T2), 68000/Z80 docs (T1) |
| Sega CD / Mega-CD | Adds 68000 sub-CPU ~12.5 MHz | Extra RAM + PCM RAM | CD subsystem + PCM | CD-ROM | Main/sub CPU sync, CD seek latency, PCM + FM mixing | Sega CD docs (T2), 68000 docs (T1) |
| 32X | Dual SH-2 + Genesis base | Shared frame buffers + SDRAM | Dual VDP path | Cartridge | SH-2 synchronization, Genesis passthrough contention | 32X hardware docs (T2), SH-2 manuals (T1) |
| Saturn | Dual SH-2 + many co-processors | 2 MB main + 1.5 MB VRAM class split | VDP1/VDP2 + SCSP | CD-ROM | Multi-bus timing, dual-CPU ordering, VDP1 command timing | Sega Saturn docs (T2), SH-2 docs (T1) |
| Dreamcast | Hitachi SH-4 ~200 MHz | 16 MB RAM + 8 MB VRAM + 2 MB AICA | PowerVR2 + AICA | GD-ROM | Tile-based deferred rendering behavior, AICA DMA/stream timing | Dreamcast hardware manuals (T2), SH-4 docs (T1) |
| Game Gear | Z80 ~3.58 MHz | 8 KB RAM | Portable SMS-like VDP + PSG | Cartridge | LCD timing and SMS compatibility path | SMS/GG technical docs (T2) |

## Sony

| Console | Core CPU / clock | Main memory | Graphics / audio anchors | Media | Emulator-critical notes | Primary references |
| --- | --- | --- | --- | --- | --- | --- |
| PlayStation (PS1) | MIPS R3000A-compatible ~33.8688 MHz | 2 MB RAM | GPU + GTE + SPU | CD-ROM | DMA timing, GTE stall behavior, GPU drawing rules, SPU reverb/noise/FM | PSX-SPX (T2), MIPS R3000 manuals (T1) |
| PlayStation 2 | MIPS-based Emotion Engine ~294 MHz + IOP | 32 MB RDRAM | GS + VU0/VU1 + SPU2 | DVD/CD | VU timing, GIF/DMAC ordering, FP edge behavior | PS2 Linux/EE docs (T2), MIPS manuals (T1) |
| PSP | MIPS R4000-family core up to 333 MHz | 32/64 MB depending model | Integrated GPU + audio engine | UMD + digital | Kernel syscall layer and GPU command semantics | PSP SDK/re docs (T2) |
| PlayStation 3 | Cell Broadband Engine | 256 MB XDR + 256 MB GDDR3 | RSX + SPU/PPU complex | Blu-ray + digital | SPU scheduling and RSX sync; heavy HLE practicality | IBM Cell docs (T1), RSX reverse docs (T2) |
| PS Vita | ARM Cortex-A9 quad + SGX543MP4+ | 512 MB RAM + 128 MB VRAM | SGX543 + audio DSP path | Cartridge + digital | OS/service emulation and GPU driver contracts | Vita reverse docs (T2), ARM docs (T1) |
| PlayStation 4 | x86-64 Jaguar 8-core | 8 GB GDDR5 | AMD GCN | Blu-ray + digital | Kernel/HLE and GNM/GNMX graphics contract fidelity | AMD docs (T1), Orbis reverse docs (T2) |
| PlayStation 5 | x86-64 Zen 2 8-core | 16 GB GDDR6 | AMD RDNA2 class + IO complex | Ultra HD Blu-ray + digital | SSD IO decompression path and modern GPU API behavior | AMD docs (T1), public platform talks (T2) |

## Microsoft

| Console | Core CPU / clock | Main memory | Graphics / audio anchors | Media | Emulator-critical notes | Primary references |
| --- | --- | --- | --- | --- | --- | --- |
| Xbox (2001) | Intel Pentium III-derived ~733 MHz | 64 MB DDR | NVIDIA NV2A + MCPX audio | DVD + HDD | NV2A fixed-function quirks, MCPX timing, D3D8-era behavior | Xbox dev/re docs (T2), Intel/NVIDIA docs (T1) |
| Xbox 360 | IBM PowerPC tri-core ~3.2 GHz | 512 MB GDDR3 unified | ATI Xenos + eDRAM | DVD + HDD | Xenon memory ordering and Xenos eDRAM resolve behavior | IBM PPC docs (T1), Xenos analyses (T2) |
| Xbox One | AMD x86-64 APU | 8 GB DDR3 (+ ESRAM on base/X) | AMD GCN | Blu-ray + HDD | ESRAM behavior, OS partition model, API contract fidelity | AMD docs (T1), public platform talks (T2) |
| Xbox Series X|S | AMD Zen 2 + RDNA2 | 10 GB (S) / 16 GB (X) GDDR6 | RDNA2 + hardware decompression | Blu-ray (X) + SSD | Velocity architecture behavior and modern API emulation | AMD docs (T1), public architecture talks (T2) |

## Atari

| Console | Core CPU / clock | Main memory | Graphics / audio anchors | Media | Emulator-critical notes | Primary references |
| --- | --- | --- | --- | --- | --- | --- |
| Atari 2600 | MOS 6507 ~1.19 MHz NTSC | 128 bytes RAM | TIA + RIOT | Cartridge | Cycle-exact TIA timing is mandatory for many games | Stella docs (T2), 6502 docs (T1) |
| Atari 5200 | 6502C ~1.79 MHz | 16 KB RAM | ANTIC + GTIA + POKEY | Cartridge | ANTIC display list timing and analog controller behavior | Atari technical docs (T2) |
| Atari 7800 | 6502C ~1.79 MHz + Maria | 4 KB RAM (+ cartridge RAM cases) | Maria + TIA compatibility | Cartridge | Maria/TIA coexistence and DMA windows | Atari docs (T2) |
| Atari Lynx | 65C02 + Mikey/Suzy custom chips | 64 KB RAM | Suzy blitter + Mikey audio/timers | Cartridge | Blitter math + display timing and rotation support | Lynx hardware docs (T2), 65C02 docs (T1) |
| Atari Jaguar | Motorola 68000 + Tom/Jerry RISC cores | 2 MB RAM | Custom object processor + DSP | Cartridge / Jaguar CD | Multi-processor bus arbitration and object processor timing | Jaguar docs (T2), 68000 docs (T1) |

## NEC / Hudson

| Console | Core CPU / clock | Main memory | Graphics / audio anchors | Media | Emulator-critical notes | Primary references |
| --- | --- | --- | --- | --- | --- | --- |
| PC Engine / TurboGrafx-16 | HuC6280 ~7.16 MHz (switchable rates) | 8 KB RAM | HuC6270 VDC + HuC6260 VCE + PSG | HuCard, CD add-ons | VDC interrupt timing, CD subsystem latency | pcenginefx docs (T2), HuC62xx docs (T2) |
| SuperGrafx | HuC6280 + dual VDC path | 32 KB RAM + expanded VRAM | HuC6270 x2 + priority controller | HuCard + CD via adapter | Layer priority and dual-VDC timing | SuperGrafx hardware refs (T2) |
| PC-FX | NEC V810 | 2 MB main (baseline class) | HuC627x-era descendants + video path | CD-ROM | Video decode path and bus timing | PC-FX docs (T2), V810 manuals (T1) |

## SNK

| Console | Core CPU / clock | Main memory | Graphics / audio anchors | Media | Emulator-critical notes | Primary references |
| --- | --- | --- | --- | --- | --- | --- |
| Neo Geo AES/MVS | 68000 ~12 MHz + Z80 | 64 KB main + VRAM class memory | SNK custom sprite hardware + YM2610 | Cartridge | Raster effects and 68K/Z80 sync, sprite fetch limits | MAME driver + NeoGeo docs (T2), 68000/Z80 manuals (T1) |
| Neo Geo CD | Same core class as AES with CD subsystem | RAM expanded for CD workflows | Same core graphics/audio class | CD-ROM | Long CD load timing and cache behavior | Neo Geo CD docs (T2) |
| Neo Geo Pocket / Color | Toshiba TLCS-900H + Z80 | 12 KB class internal + mapped cart RAM | Custom 2D + PSG | Cartridge | CPU timing and monochrome/color model differences | ngpc technical docs (T2) |

## Bandai / Apple-Bandai

| Console | Core CPU / clock | Main memory | Graphics / audio anchors | Media | Emulator-critical notes | Primary references |
| --- | --- | --- | --- | --- | --- | --- |
| WonderSwan / Color | NEC V30MZ | 64 KB class + VRAM | Tile/sprite 2D engine, PSG | Cartridge | Low-power timing modes, orientation handling | WonderSwan docs (T2), V30 docs (T1) |
| Apple Bandai Pippin | PowerPC 603 class | 6 MB base class | Apple multimedia pipeline | CD-ROM | MacOS derivative environment and CD boot model | Apple Pippin docs (T2), PPC manuals (T1) |

## Philips / Magnavox and early US majors

| Console | Core CPU / clock | Main memory | Graphics / audio anchors | Media | Emulator-critical notes | Primary references |
| --- | --- | --- | --- | --- | --- | --- |
| Magnavox Odyssey2 | Intel 8048 family MCU | 64 bytes internal + external RAM path | Character/sprite hybrid | Cartridge | BIOS+cart interaction and keyboard matrix behavior | Odyssey2 docs (T2), 8048 docs (T1) |
| Philips CD-i | 68070 class CPU | Model-dependent RAM | CD-i video/audio ASICs | CD-ROM | CD-RTOS behavior and video decoder timing | Philips CD-i docs (T1/T2) |
| ColecoVision | Z80A ~3.58 MHz | 1 KB RAM | TMS9928A + SN76489A | Cartridge | VDP timing and Coleco BIOS mapping | Coleco technical docs (T2) |
| Intellivision | GI CP1610 ~0.894 MHz | 1.5 KB STIC RAM class + exec ROMs | STIC + PSG | Cartridge | STIC timing and controller matrix semantics | Intellivision docs (T2) |
| Fairchild Channel F | Fairchild F8 | 64 bytes + cart/program memory | Minimal raster hardware | Cartridge | Timing and unusual controller semantics | Channel F docs (T2), F8 docs (T1) |
| Bally Astrocade | Z80 | 4 KB RAM base | Custom video + audio ASICs | Cartridge | Pixel packing modes and magic register behavior | Astrocade docs (T2) |

## Panasonic / 3DO

| Console | Core CPU / clock | Main memory | Graphics / audio anchors | Media | Emulator-critical notes | Primary references |
| --- | --- | --- | --- | --- | --- | --- |
| 3DO Interactive Multiplayer (Panasonic FZ-1 class) | ARM60 ~12.5 MHz | 2 MB DRAM + 1 MB VRAM class | Custom CEL engines + DSP path | CD-ROM | CEL engine command semantics and DSP scheduling | 3DO portfolio docs (T2), ARM60 docs (T1) |

## Practical implementation order (recommended)

1. 8/16-bit cartridge consoles first (NES, GB/C, SMS, Genesis, SNES, PC Engine).
2. 32-bit disc era next (PS1, Saturn, N64, Jaguar, 3DO).
3. 6th generation and later after solid HLE infrastructure (PS2, GameCube, Xbox onward).

## What "factory accurate" requires in practice

- Deterministic timing model: per-cycle or verified event-accurate scheduling where cycle-level is impossible.
- Bus contention and DMA arbitration: modeled, not approximated away.
- Interrupt edge behavior: exact masking, pending, and acknowledgement semantics.
- Video pipeline corner cases: pixel FIFO, sprite limits, fetch stalls, interlace/field behavior.
- Audio non-idealities where audible: mixer saturation, envelope steps, interpolation filters, timer drift.
- Hardware test ROM conformance: pass published suites before game-based confidence claims.

## Baseline source index used for this document

- https://problemkaputt.de/gbatek.htm
- https://problemkaputt.de/psx-spx.htm
- https://www.nesdev.org/
- https://en.wikipedia.org/wiki/List_of_home_video_game_consoles
- https://en.wikipedia.org/wiki/Nintendo_video_game_consoles
- https://en.wikipedia.org/wiki/List_of_Sega_video_game_consoles
- https://en.wikipedia.org/wiki/PlayStation_technical_specifications
- https://en.wikipedia.org/wiki/Xbox
- https://en.wikipedia.org/wiki/Neo_Geo
- https://en.wikipedia.org/wiki/PC_Engine_SuperGrafx
- https://en.wikipedia.org/wiki/Second_generation_of_video_game_consoles
- Existing repository knowledge notes in .github/knowledge/

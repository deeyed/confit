---
doc_type: inventory
status: draft
authority: informative
last_verified: 2026-06-24
---

# Delos Config Inventory

이 문서는 Delos 실제 `config/` tree를 만들기 전, Confit fixture로 먼저 검증할 compile-time 설정 후보를
정리한다. 실제 Delos source, CMake, QStar 파일은 수정하지 않고 읽기만 했으며, 실험 입력은
`tools/confit/tests/fixtures/realish/delos/` 아래 TOML mirror로 둔다.

## 읽은 표면

- `CMakeLists.txt`: `DELOS_BUILD_PROFILE`, build/work/target/results/docs output root, clang/objcopy/readelf/objdump, hardware smoke cache option.
- `qstar.lua`: `src/debug`, `src/arch/sim`, `src/board/sim/delos-sim`, `targets/sim`, board and test labels.
- `targets/sim/CMakeLists.txt`, `targets/sim/sim.qst`: `delos-sim-dsh`, `delos-sim`, `build/target/debug/sim/dsh/delos-sim`, `build/target/delos-sim`.
- `docs/contracts/debug/ddc-debug-console.md`: DDC/DSH release boundary, simulator target ownership, hosted stdio transport boundary.
- `src/board/sim/delos-sim/state_provider.c`, `state_provider.h`: `host-sim`, `sim:dsh`, `delos-sim-dsh 0.1.0`, one task slot, trace capacity 4, `sim-uart0`.
- `src/debug/debug.qst`: DDC/DSH debug library is not linked into MCU images by default.

## Namespace

Delos mirror option ids use `delos.*` only. The current groups are:

- `delos.build.*`: build profile, output root, generated config root.
- `delos.generated.*`: generated header/report/CMake/QStar artifact switches.
- `delos.profile.*`: hidden profile markers for debug/release conflict checks.
- `delos.internal.*`: hidden aggregate markers used by dependency explanations.
- `delos.debug.*`: DDC, DSH, observability, release diagnostic dump boundary.
- `delos.sim.*`: host simulator transport and simulator-sized sample values.
- `delos.dcg.*`, `delos.scheduler.*`, `delos.trace.*`: runtime capacity and accounting knobs.
- `delos.hal.*`, `delos.memory.*`: board inventory and target memory metadata.
- `delos.target.*`: target kind, arch, board, CPU, output artifact identity.

## Initial Practical Set

The `sim-dsh` profile is the first migration target because it exercises the debug console while remaining host-side.
The fixture resolves:

- DDC enabled.
- DSH enabled and requiring DDC.
- Hosted stdio DDC transport enabled and requiring DSH.
- Release profile marker inactive, so DSH release conflict stays inactive.
- Simulator board `host-sim`, target kind `sim:dsh`, CPU `host`.
- One scheduler task slot and trace capacity 4, matching the current simulator state provider fixture.
- Generated config root `build/generated/config/delos/sim-dsh`.
- Primary output name `delos-sim` and convenience entry `build/target/delos-sim`.

## Not Mirrored Yet

- Actual Delos `config/` directory creation.
- Runtime source inclusion or macro adoption.
- CMake/QStar consumption of generated artifacts.
- Hardware flash backend, serial port, and HIL cache variables.
- Per-board memory maps beyond the coarse `delos.memory.flash_origin` candidate.

Those changes belong to a later application round after the Confit mirror, golden output, and compatibility rules are
stable.

---
doc_type: inventory
status: draft
authority: informative
last_verified: 2026-06-24
---

# Parus Config Inventory

이 문서는 Parus가 Kconfig를 쓰지 않고 Confit으로 이동할 때 필요한 compile-time 설정 후보를
정리한다. 별도의 Parus worktree는 읽기 전용 참고로만 사용했고, 실제
Parus source, CMake, QStar, `configs/` 파일은 수정하지 않았다. 실험 입력은
`tools/confit/tests/fixtures/realish/parus/` 아래 TOML mirror로 둔다.

## 읽은 표면

- `configs/Kconfig`: `PARUS_BOARD_*`, `PARUS_ARCH_*`, `PARUS_BOOT_PATH_*`,
  `PARUS_HARDWARE_FIRST`, `PARUS_SERIAL_EARLY`, generated output directory symbols.
- `configs/boards/qemu-virt-aarch64/defconfig`: QEMU virt AArch64, direct-DTB, hardware-first,
  early serial, QEMU pairing.
- `configs/boards/rpi5/defconfig` and `configs/fragments/rpi5-direct-dtb-alive.config`:
  RPi5 board identity, Ribon/LBPB default, direct-DTB alive override, direct-DTB fallback policy.
- `docs/contracts/config/config-system-contract.md`: source input, generated output, Kconfiglib
  resolver boundary, precedence order.
- `docs/contracts/config/generated-config-abi.md`: `parus/autoconf.h`, CMake variables, JSON ABI.
- `docs/contracts/config/boot-path-selection-policy.md`: board identity and boot handoff path
  separation.
- `docs/platforms/qemu-virt-aarch64/bringup-contract.md`: QEMU direct-DTB entry and boot smoke
  marker levels.
- `docs/platforms/rpi5/direct-dtb-alive-package.md`: RPi5 package-check lane and hardware-support
  non-claim.
- `docs/contracts/executor/rt-executor.md` and `docs/contracts/kcg/kcg-core-model.md`: KCG/RT
  Executor direction, with no finalized fabric ABI.
- `targets/arm64/arm64.qst`, `tests/qemu/qemu.qst`, `tests/package/package.qst`: QStar labels,
  image paths, package paths, smoke labels.

## Namespace

Parus mirror option ids use `parus.*` only:

- `parus.config.*`: board profile, defconfig, resolved config output location.
- `parus.generated.*`: generated include/config/QStar artifact switches.
- `parus.policy.*`: hardware-first and Kairon v1 reference-only policy.
- `parus.boot.*`: boot handoff path, direct-DTB/Ribon flags, fallback, serial, stage limit.
- `parus.arch.*`, `parus.board.*`, `parus.target.*`: architecture, board, output artifact identity.
- `parus.test.*`: QEMU pairing and smoke level.
- `parus.kcg.*`, `parus.executor.*`: experimental executor/KCG contract candidates.

## Initial Practical Set

The `qemu-aarch64` profile is the first Parus mirror target because it maps directly to the existing
QEMU virt AArch64 boot-smoke lane. It resolves:

- Config profile `qemu-virt-aarch64`.
- Board `qemu-virt-aarch64`, architecture `arm64`.
- Direct-DTB boot handoff enabled, Ribon/LBPB disabled.
- Hardware-first policy and early serial diagnostics enabled.
- Smoke level `boot-smoke`, matching the EB2-level QEMU contract.
- Primary QStar label `//targets/arm64:parus_qemu_virt_aarch64_entry_elf`.
- Primary image path `build/target/debug/arm64/qemu-virt-aarch64/parus-qemu-virt-aarch64-entry.img`.
- KCG and RT Executor compile-time candidates present but disabled.

The fixture also contains `rpi5-direct-dtb` as a package-check candidate. This is not a hardware-support
claim; it mirrors the direct-DTB alive package preparation lane only.

## Apply-Ready Placement

When Parus adopts Confit for real, the intended project root can be a Parus-local config tree such as:

```text
configs/confit/
  project.toml
  options/
  profiles/
  targets/
```

Generated artifacts should stay under the build tree, for example:

```text
build/work/cmake/qemu-aarch64/generated/config/
  config.h
  config.cmake
  config/
    config.qsm
  config.qst  # deprecated compatibility artifact
  config.report.json
  config.explain.txt
  config.graph.json
  config.inputs.json
```

The existing `parus/autoconf.h` ABI can later be mapped either by renaming the generated header path or
by adding a Parus-specific install step. This mirror does not change that ABI yet.

## Not Mirrored Yet

- Actual Parus `configs/` migration.
- `parus/autoconf.h` compatibility header generation.
- CMake/QStar consumption of Confit outputs.
- KCG/RT Executor ABI, shared-memory rings, DCG wire format, or Delos fabric.
- RPi5 hardware success. Package-check and QEMU smoke are not hardware evidence.

# Confit

Confit은 Parus의 configure-time 선택 compiler다. Config v5의 source-local option,
menu, choice, constraint와 ARCH-scoped KERNCONF를 bounded하게 해석하고 immutable
configuration generation을 출판한다. `[target]`, `[profile]`, `[selection]`, product와
provider graph는 문법이 아니다. Makefile, C source 목록, link 순서, test 목록,
generator action과 Five-GEN action graph도 해석하거나 소유하지 않는다.

Production 흐름은 `confit configure --arch <ARCH> --kernconf <name>` 한 번과 이후의
`bmake` 소비 단계로 분리된다.
설정 입력이 바뀌지 않은 ordinary build에서는 resolver를 다시 실행하지 않는다. 이전
manifest와 generated graph 형식의 생성기·호환 reader는 제공하지 않는다.

독립 검증:

```text
bmake CONFIT_OBJROOT=/absolute/output \
  CONFIT_BMAKE_TOOL=/absolute/bmake \
  CONFIT_HOST_CC=/absolute/c-compiler check-host
```

## 사람용 configuration workflow

CLI와 TUI는 `confit_v5_evaluate()`의 동일 reason graph와
`confit_v5_workflow_minimal()`의 동일 serializer를 소비한다. 별도 frontend가 option을
자동 enable하거나 마지막 writer를 우선하는 규칙은 없다.

```text
confit search --repository /repo --arch arm64 --kernconf qemu_virt_dev --query iommu
confit explain --repository /repo --arch arm64 --kernconf qemu_virt_dev --symbol DRIVER_IOMMU_SMMUV3
confit why-unavailable --repository /repo --arch arm64 --kernconf qemu_virt_dev --symbol OPTION
confit list-new --repository /repo --arch arm64 --kernconf qemu_virt_dev
confit oldconfig --repository /repo --arch arm64 --kernconf qemu_virt_dev
confit save-minimal --repository /repo --arch arm64 --kernconf qemu_virt_dev --output /absolute/file
confit diff --repository /repo --arch arm64 --kernconf left --other right
confit tui --repository /repo --out /object/gen/config-v5 --arch arm64 \
  --kernconf qemu_virt_dev --transaction menuconfig-1
```

TUI는 shallow menu와 안정된 row 순서, 고정 detail pane을 표시하며 색상에 의존하지
않는다. `/query`, `j`/`k` 또는 화살표, `set SYMBOL VALUE`, `preview`, `apply`, `cancel`만
해석한다. 입력과 화면 크기는 bounded하며 다른 escape sequence와 command string은
실행하지 않는다. Preview와 Cancel은 selected generation을 바꾸지 않고 Apply만 기존
generation transaction을 통해 atomic publish한다. Unknown 또는 제거된 KERNCONF symbol은
호환 alias 없이 catalog load 단계에서 실패한다.

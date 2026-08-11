---
doc_type: language-spec
status: accepted
authority: normative
last_verified: 2026-08-11
---

# generic project schema v2와 selectable schema v3 source

Confit project는 canonical root의 `config/project.toml`에서 시작하며 `[project].schema_version = 2`를
명시한다. Project, option, profile, target, import와 constraint field는 schema-owned allowlist를 따른다.
Unknown field, duplicate canonical path, absolute/parent path, symlink escape, case collision, malformed UTF-8
및 bounded input limit 초과는 fail-closed한다.

Target은 typed `values`, machine/toolchain/image tuple과 required high-level feature만
표현한다. Selectable profile은 `config/selections/<target>/<profile>.toml`에서 root feature와
ambiguous provider만 소유한다. Source list, compiler flag, generated artifact path 또는
physical device attach rule은 selection schema가 아니다.

Target schema v3의 `[build]`에는 ISA/ABI/CPU/entry, toolchain ID, closed compile
tuple, linker/package와 Kernel·World·Image artifact role만 있다. Target-wide
`private_includes`는 제거되었고 unknown field로 거부된다. Board와 mandatory
architecture implementation의 경계는 다음 exact facade edge로 대체한다.

```toml
[support]
provider_owner = "sys.arch.arm64.support"
consumer_owner = "sys.board.arm64.qemu_virt"
role = "architecture.facade.v1"
facade_include_root = "sys/include/parus/arch/arm64"
required_kapi = "parus.arch.arm64.support.v1"
```

`facade_include_root`는 `sys/include/parus/`의 relative descendant여야 하며
consumer는 target의 `expected_component`와 exact-match한다. 이 edge는 선택된 public
facade이지 아키텍처/board/provider private header의 전역 노출이 아니다. Selection
closure 안에서 `provider_owner`가 유일한 selected component를 가리키고 그 component가
`required_kapi`를 게시하며, consumer가 같은 KAPI를 요구할 때만 target plan이 성립한다.

QEMU machine을 게시하는 target은 `[machine]`에 다음 required closed
evidence tuple을 함께 둔다.

```toml
trust_profile = "qemu-executable-v1"
resource_identity = "arm64-qemu-virt-machine-v1"
evidence_transport = "qemu-fwcfg-challenge-v1"
evidence_protocol = "parus-qemu-terminal-v1"
evidence_max_bytes = 65536
```

Unknown evidence/trust vocabulary, zero/unbounded evidence size, executable digest/version
누락은 fail-closed다. 이 tuple은 trusted configured QEMU 전제의 QEMU 증거이며
malicious hypervisor attestation이나 physical-hardware 증거가 아니다.

## selectable component manifest v3

`component.toml`은 optional kernel/World composition에만 존재한다. Mandatory nucleus,
target와 test는 component kind가 아니다. Manifest에는 source, flag, command, hardware
match와 Makefile path를 넣지 않으며 sibling restricted `Makefile`만 유효하다.

```toml
schema_version = 3

[component]
id = "driver.audio.pci.cmi8738"
kind = "kernel_provider"
summary = "CMI8738 PCI audio provider"
owner = "vendor.example.audio"

[selection]
requires = ["subsystem.audio@1", "transport.pci@1"]
provides = ["audio.device@1"]
conflicts = []
default = false

[interfaces]
kapi_requires = ["parus.audio.endpoint.v1"]
kapi_provides = []
```

허용 kind는 `kernel_feature`, `kernel_provider`, `world_feature`, `world_service` 네 개다.
Feature는 `@N`, KAPI는 `.vN`으로 양의 version을 명시한다. Profile은 transitive
component ID를 열거하지 않으며 candidate가 여러 개인 feature는 `[providers]`에서 exact
component를 지정한다. Catalog order와 first-match는 권위가 아니다.

Catalog는 component 512개, 전체 edge 4096개, depth 32, list별 128개, manifest 128 KiB,
atom 127 bytes, path 1024 bytes로 제한한다. Duplicate ID/provider, missing provider,
self-edge, cycle, symlink/path escape와 limit 초과는 partial catalog 없이 실패한다.
Selection은 root/feature/KAPI/provider reason과 manifest source 위치를 immutable
artifact에 보존한다.

Sibling selectable Makefile은 exact `PARUS_MK_API = 3`, bounded `SRCS +=`와 kind별 public
include 하나만 가진다. Mandatory nucleus는 `component.toml` 없이 parent
`KERN_SUBDIRS +=`/`parus.kernsubdir.mk` 또는 leaf `KERN_UNIT`, `SRCS`, `KERN_USES`,
`KAPI_EXPORTS`/`parus.kernunit.mk` grammar를 사용한다. Test는 owner-local `TEST_ID`,
`TEST_OWNER`, lane/evidence/timeout, optional target/machine/receipt와 `TEST_SRCS`를
`parus.test.mk`로 닫는다. Condition, modifier, recipe, glob, raw flag, extra include와 source
escape는 catalog 단계에서 거부한다. Source는 owner directory 아래 existing regular
file이어야 하고 한 graph에서 owner가 중복될 수 없다.

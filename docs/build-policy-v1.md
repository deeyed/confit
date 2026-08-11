---
doc_type: build-policy-candidate
status: experimental
authority: candidate
last_verified: 2026-08-11
---

# Five-GEN build policy/action ABI v1 candidate

## 상태와 activation 경계

이 문서는 Confit이 생성하는 `build.policy`, restricted
`build.policy.mk`와 C action ABI의 candidate contract다. Confit source·unit corpus에서는
현재 구현되었지만 Parus parent의 production build authority는 아니다. Parus가
exact Confit gitlink, selected target descriptor, action consumer와 accepted contract를 한 commit에서
hard-cut하기 전에는 이 ABI를 activation proof로 사용하지 않는다. Old action이나
target-private include를 위한 compatibility adapter는 만들지 않는다.

## selected policy

`build.policy` v1은 다음을 canonical ordered text로 결속한다.

- profile, configuration digest, target ID/digest와 edge-table identity
- ToolGEN, EnvGEN, KernGEN, WorldGEN, ImageGEN, test의 exact one-owner domain
- closed action kind, input/output role와 domain-edge table
- ISA/ABI/CPU/entry와 target toolchain ID/triple
- compiler, archiver, linker, optional DTC의 absolute path, SHA-256와 version
- selected board consumer가 요구하고 selected architecture provider가 유일하게
  게시하는 exact public facade/KAPI edge
- Kernel·World·Image artifact role/path
- optional QEMU executable path/digest/version, resource identity, evidence transport/protocol/limit

`build.policy.mk`는 policy ABI, policy/configuration/target digest와 edge table ID만
노출한다. Make에서 domain edge를 다시 정의하거나 path/tool authority를
추가할 수 없다.

## action authoring view와 wire

Authoring API의 one action은 exactly one domain, one kind, one source owner, current
configuration/target/policy digest, one exact trusted tool identity, bounded input/output와
1 GiB 이하 action quota를 갖는다. Input은 role, relative path, SHA-256와 owner를;
output은 role, relative path와 effect-before-write maximum을 갖는다. Absolute output,
`.`/`..`, duplicate endpoint, unknown enum, forbidden cross-domain edge, missing owner,
unsealed tool은 serialization 전에 거부된다.

Wire는 pointer-free little-endian `PBACTN01` version 1이다. Header에 domain/kind/owner
cardinality가 각각 exactly one임을 반복 봉인하고, no-follow flag, bounded count,
total size와 zero reserved bytes를 갖는다. Action-ID field를 zero placeholder로 둔
canonical descriptor 전체의 digest가 action ID이므로 byte-order
변경, trailing/truncation, stale policy/configuration/target seal, unknown role/tool은
allocation 없이 fail-closed된다. Wire 검증 성공은 실제 filesystem effect를
허가하지 않으며, downstream descriptor-rooted writer가 owner/role/quota를 다시
소비해야 한다.

## QEMU evidence

QEMU action은 `qemu-executable-v1` exact tool seal과
`qemu-fwcfg-challenge-v1` 입력, `parus-qemu-terminal-v1` terminal receipt, bounded
evidence bytes와 image/resource identity를 동시에 사용한다. 이는 static/replayed
transcript와 wrong-image 결속을 거부하기 위한 transport contract이다.

QEMU executable/hypervisor는 trusted evidence prerequisite다. Challenge와 image를 볼 수 있는
malicious configured QEMU를 guest real execution과 구분하는 attestation은 비주장이며,
QEMU 증거를 physical hardware 증거로 승격하지 않는다.

## conformance와 비주장

Confit unit corpus는 deterministic A/B policy/action bytes, 기존 ISA의 새 target
instance, World role, architecture facade, QEMU ingress와 malicious cardinality/enum/edge/path,
digest/version, byte-order, size, duplicate, quota corpus를 검사한다. `novel64`같은
신규 ISA fixture는 serializer genericity만 보이며 compiler/linker, boot, QEMU backend이나
real-runtime 지원을 주장하지 않는다.

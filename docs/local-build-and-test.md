---
doc_type: developer-guide
status: accepted
authority: normative
last_verified: 2026-08-09
---

# local build와 test

Confit의 canonical host build는 reviewed `20240909` feature baseline 이상인 direct
`bmake`다. output root는 source tree 밖의 미리 만든 canonical absolute directory여야 한다.

```sh
bmake -r -C tools/confit -f Makefile \
  CONFIT_OBJROOT=/private/tmp/confit-check \
  CONFIT_BMAKE_TOOL=/absolute/path/to/bmake check-host
```

`check-host`는 host binary와 bmake가 직접 열거한 v2 unit/fuzz/publication C test,
CLI/ABI identity를 실행한다. Test는 active fixture만 사용하며 output은 supplied
`CONFIT_OBJROOT` 또는 temporary test root에만 만든다.

Confit 단독 success는 target kernel compile, QEMU, package 또는 physical hardware evidence가 아니다.
Parus root에서는 direct `bmake` target이 각 evidence lane을 분리한다.

새 test는 positive result와 corresponding fail-closed input을 함께 추가해야 한다. golden은 canonical
snapshot/ABI output일 때만 허용하며 historical backend output을 보관하지 않는다.

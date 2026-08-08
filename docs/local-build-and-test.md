---
doc_type: developer-guide
status: accepted
authority: normative
last_verified: 2026-08-09
---

# local build와 test

Confit의 canonical host build는 pinned BSD `bmake 20240909`다. output root는 source tree 밖의
absolute path여야 한다.

```sh
<pinned-bmake> -r -C tools/confit -f Makefile \
  CONFIT_OBJROOT=/private/tmp/confit-check check
```

`check`은 explicit source manifest 검사, host binary build, v2 unit/fuzz suite, CLI/ABI identity와
sealed-bundle integration test를 실행한다. Test는 active fixture만 사용하며 output은 supplied
`CONFIT_OBJROOT` 또는 temporary test root에만 만든다.

Confit 단독 success는 target kernel compile, QEMU, package 또는 physical hardware evidence가 아니다.
Parus root에서는 `tools/build/bootstrap/parus-bmake check-all`이 각 evidence lane을 분리한다.

새 test는 positive result와 corresponding fail-closed input을 함께 추가해야 한다. golden은 canonical
snapshot/ABI output일 때만 허용하며 historical backend output을 보관하지 않는다.

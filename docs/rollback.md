---
doc_type: operations-guide
status: accepted
authority: operational
last_verified: 2026-08-09
---

# sealed generation rollback

Confit은 source project 또는 sibling repository를 write하지 않는다. Failed generation은
partial staging을 제거하고 selected directory alias를 갱신하지 않는다. Consumer action은
current configure 성공 뒤 alias가 가리키는 exact generation만 고정하며 실패한 configure 뒤
이전 generation을 자동 재사용하지 않는다.

완료된 generation을 폐기해야 하면 project source가 아닌 caller-owned output root에서 exact digest
directory를 inventory하고, 다른 configured child 또는 result provenance가 참조하지 않는지 확인한 뒤
소유자가 제거한다. Broad source-tree cleanup, automatic downgrade 또는 old artifact fallback은 금지된다.

Source TOML 변경의 rollback은 normal version-control review로 수행한다. incident report에는 project root,
input digest, requested output root, first diagnostic, selected generation과 consumer action provenance를
기록한다.

#include <stdint.h>

/* 이 translation unit은 compiler 이름을 추측하지 않고 Confit이 요구하는 C17
 * integer width와 language mode를 실제 host compile로 검증한다. */
_Static_assert(sizeof(uint8_t) == 1U, "Confit requires an 8-bit byte type");
_Static_assert(sizeof(uint32_t) == 4U, "Confit requires a 32-bit integer type");
_Static_assert(sizeof(uint64_t) == 8U, "Confit requires a 64-bit integer type");

int confit_host_c17_capability_probe(void) { return 1; }

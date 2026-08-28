#ifndef CONFIT_STATUS_H
#define CONFIT_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Confit 작업 결과를 나타내는 작은 numeric status code다.
 *
 * 값은 schema 6 계약의 CLI exit code 정책과 정확히 맞춘다.
 */
typedef enum ConfitStatus {
  /** 성공. */
  CONFIT_OK = 0,
  /** 명령행 또는 API 사용법이 잘못되었거나 아직 구현되지 않았다. */
  CONFIT_ERR_USAGE = 2,
  /** 입력 parse, schema 또는 값 검증에 실패했다. */
  CONFIT_ERR_VALIDATION = 3,
  /** 선택된 configuration이 없거나 현재 입력과 일치하지 않는다. */
  CONFIT_ERR_STALE = 4,
  /** 허용된 host I/O가 실패했다. */
  CONFIT_ERR_IO = 5,
  /** terminal을 안전하게 사용할 수 없거나 복구하지 못했다. */
  CONFIT_ERR_TERMINAL = 6,
  /** Confit 내부 invariant가 깨졌다. */
  CONFIT_ERR_INTERNAL = 70,
} ConfitStatus;

/**
 * @brief status가 성공인지 확인한다.
 *
 * @param status 검사할 status code.
 * @return `CONFIT_OK`이면 1, 그렇지 않으면 0.
 */
int confit_status_is_ok(ConfitStatus status);

/**
 * @brief status code에 대응하는 CLI exit code를 반환한다.
 *
 * @param status 변환할 status code.
 * @return CLI가 사용할 exit code. 알 수 없는 값은 internal error code로 변환된다.
 */
int confit_status_exit_code(ConfitStatus status);

/**
 * @brief status code의 안정적인 diagnostic 이름을 반환한다.
 *
 * @param status 이름을 조회할 status code.
 * @return 정적 저장 기간을 가진 ASCII 문자열.
 */
const char *confit_status_name(ConfitStatus status);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_STATUS_H */

#ifndef CONFIT_STATUS_H
#define CONFIT_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Confit 작업 결과를 나타내는 작은 numeric status code다.
 *
 * 값은 CLI exit code 정책과 맞춘다. Dependency 오류와 conflict 오류는
 * 현재 같은 exit code 4를 공유하며, 이후 graph checker가 세부 diagnostic을
 * 추가해 사용자가 실제 원인을 구분할 수 있게 한다.
 */
typedef enum ConfitStatus {
  /** 성공. */
  CONFIT_OK = 0,
  /** 명령행 인자 또는 API 인자가 잘못되었다. */
  CONFIT_ERR_INVALID_ARGUMENT = 1,
  /** 입력 파일 parse에 실패했다. */
  CONFIT_ERR_PARSE = 2,
  /** schema 검증에 실패했다. */
  CONFIT_ERR_SCHEMA = 3,
  /** dependency 또는 conflict 검증에 실패했다. */
  CONFIT_ERR_DEPENDENCY = 4,
  /** dependency 또는 conflict 검증에 실패했다. */
  CONFIT_ERR_CONFLICT = 4,
  /** cross-project compatibility 검증에 실패했다. */
  CONFIT_ERR_COMPATIBILITY = 5,
  /** generated artifact 생성에 실패했다. */
  CONFIT_ERR_GENERATION = 6,
  /** Confit 내부 invariant가 깨졌다. */
  CONFIT_ERR_INTERNAL = 7,
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

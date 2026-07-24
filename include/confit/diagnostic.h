#ifndef CONFIT_DIAGNOSTIC_H
#define CONFIT_DIAGNOSTIC_H

#include <stddef.h>

#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief diagnostic의 사용자 노출 severity다. */
typedef enum ConfitDiagnosticSeverity {
  CONFIT_DIAGNOSTIC_SEVERITY_NONE = 0,
  CONFIT_DIAGNOSTIC_SEVERITY_NOTE,
  CONFIT_DIAGNOSTIC_SEVERITY_WARNING,
  CONFIT_DIAGNOSTIC_SEVERITY_ERROR,
} ConfitDiagnosticSeverity;

/** @brief primary diagnostic에 연결되는 immutable source span 설명이다. */
typedef struct ConfitDiagnosticRelatedSpan {
  const char *path;
  size_t line;
  size_t column;
  const char *note;
} ConfitDiagnosticRelatedSpan;

/** @brief 자동 적용하지 않는 수정 후보의 안정된 transport shape다. */
typedef struct ConfitDiagnosticFixCandidate {
  const char *option_id;
  const char *value_text;
  const char *note;
} ConfitDiagnosticFixCandidate;

/**
 * @brief Confit 오류 위치와 사람이 읽을 수 있는 메시지를 담는 record다.
 *
 * `path`와 `message`는 호출자가 소유한 문자열을 빌려서 참조한다. 이 구조체는
 * heap allocation을 수행하지 않으며, CLI나 report layer가 필요할 때만 사람이
 * 읽는 문자열로 format한다.
 */
typedef struct ConfitDiagnostic {
  /** 오류 종류. `CONFIT_OK`이면 활성 diagnostic이 없다. */
  ConfitStatus status;
  /** repo 또는 project root 기준 입력 경로. 없으면 `NULL`. */
  const char *path;
  /** 1-based line number. 위치 정보가 없으면 0. */
  size_t line;
  /** 1-based column number. 위치 정보가 없으면 0. */
  size_t column;
  /** 사람이 읽을 수 있는 짧은 오류 메시지. 없으면 `NULL`. */
  const char *message;
  /** machine-readable stable diagnostic code. 없으면 `NULL`. */
  const char *code;
  /** CLI/JSON에 공통으로 노출할 severity다. */
  ConfitDiagnosticSeverity severity;
  /** primary diagnostic의 causal related source span 목록이다. */
  const ConfitDiagnosticRelatedSpan *related;
  size_t related_count;
  /** 자동 적용하지 않는 수정 후보 목록이다. */
  const ConfitDiagnosticFixCandidate *fix_candidates;
  size_t fix_candidate_count;
} ConfitDiagnostic;

/**
 * @brief diagnostic record를 성공 상태로 초기화한다.
 *
 * @param diagnostic 초기화할 diagnostic record.
 */
void confit_diagnostic_init(ConfitDiagnostic *diagnostic);

/**
 * @brief diagnostic record를 성공 상태로 되돌린다.
 *
 * @param diagnostic 정리할 diagnostic record.
 */
void confit_diagnostic_clear(ConfitDiagnostic *diagnostic);

/**
 * @brief diagnostic record에 오류 status와 위치를 기록한다.
 *
 * @param diagnostic 갱신할 diagnostic record.
 * @param status 기록할 오류 status. `CONFIT_OK`도 허용한다.
 * @param path 입력 경로. 없으면 `NULL`.
 * @param line 1-based line number. 없으면 0.
 * @param column 1-based column number. 없으면 0.
 * @param message 짧은 오류 메시지. 없으면 `NULL`.
 */
void confit_diagnostic_set(ConfitDiagnostic *diagnostic, ConfitStatus status,
                           const char *path, size_t line, size_t column,
                           const char *message);

/** @brief code/severity/related/fix 후보까지 가진 detailed diagnostic을 기록한다. */
void confit_diagnostic_set_detail(
    ConfitDiagnostic *diagnostic, ConfitStatus status,
    ConfitDiagnosticSeverity severity, const char *code, const char *path,
    size_t line, size_t column, const char *message,
    const ConfitDiagnosticRelatedSpan *related, size_t related_count,
    const ConfitDiagnosticFixCandidate *fix_candidates,
    size_t fix_candidate_count);

/**
 * @brief 활성 오류 diagnostic이 있는지 확인한다.
 *
 * @param diagnostic 검사할 diagnostic record.
 * @return 실패 status가 기록되어 있으면 1, 아니면 0.
 */
int confit_diagnostic_has_error(const ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_DIAGNOSTIC_H */

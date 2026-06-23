#ifndef CONFIT_VERSION_H
#define CONFIT_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Confit major version이다.
 */
#define CONFIT_VERSION_MAJOR 0

/**
 * @brief Confit minor version이다.
 */
#define CONFIT_VERSION_MINOR 1

/**
 * @brief Confit patch version이다.
 */
#define CONFIT_VERSION_PATCH 0

/**
 * @brief 현재 구현 라벨이다.
 */
#define CONFIT_VERSION_LABEL "round1"

/**
 * @brief CLI가 출력할 안정적인 version 문자열을 반환한다.
 *
 * @return 정적 저장 기간을 가진 ASCII 문자열.
 */
const char *confit_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_VERSION_H */

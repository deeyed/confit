#ifndef CONFIT_VERSION_H
#define CONFIT_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Confit major version이다.
 */
#define CONFIT_VERSION_MAJOR 1

/**
 * @brief Confit minor version이다.
 */
#define CONFIT_VERSION_MINOR 0

/**
 * @brief Confit patch version이다.
 */
#define CONFIT_VERSION_PATCH 0

/**
 * @brief schema 6 release-candidate label이다.
 */
#define CONFIT_VERSION_LABEL "rc1"

/**
 * @brief semver prerelease까지 포함한 release 문자열이다.
 */
#define CONFIT_VERSION_RELEASE "1.0.0-rc1"

/**
 * @brief CLI와 generated artifact에 기록할 display 문자열이다.
 */
#define CONFIT_VERSION_DISPLAY "confit " CONFIT_VERSION_RELEASE

/** @brief 동결된 public contract와 구현의 schema 번호다. */
#define CONFIT_SCHEMA_CONTRACT_VERSION 6

/** @brief vendored TOML parser revision이다. */
#define CONFIT_TOMLC17_REVISION "R260618 (7813bdd)"

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

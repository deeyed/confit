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
#define CONFIT_VERSION_MINOR 6

/**
 * @brief Confit patch version이다.
 */
#define CONFIT_VERSION_PATCH 0

/**
 * @brief configure-once Config v5 제품 라벨이다.
 */
#define CONFIT_VERSION_LABEL "config-v5"

/**
 * @brief semver prerelease까지 포함한 release 문자열이다.
 */
#define CONFIT_VERSION_RELEASE "0.6.0-config-v5"

/**
 * @brief CLI와 generated artifact에 기록할 display 문자열이다.
 */
#define CONFIT_VERSION_DISPLAY "confit " CONFIT_VERSION_RELEASE

/** @brief 이 binary가 유일하게 받아들이는 source schema다. */
#define CONFIT_SUPPORTED_SCHEMA_VERSIONS "config-v5"

/** @brief sealed tool-neutral generated-artifact identity. */
#define CONFIT_ARTIFACT_ABI_V5 "confit-artifact-v5"

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

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
#define CONFIT_VERSION_MINOR 2

/**
 * @brief Confit patch version이다.
 */
#define CONFIT_VERSION_PATCH 0

/**
 * @brief 현재 release candidate 라벨이다.
 */
#define CONFIT_VERSION_LABEL "rc1"

/**
 * @brief semver prerelease까지 포함한 release 문자열이다.
 */
#define CONFIT_VERSION_RELEASE "0.2.0-rc1"

/**
 * @brief CLI와 generated artifact에 기록할 display 문자열이다.
 */
#define CONFIT_VERSION_DISPLAY "confit " CONFIT_VERSION_RELEASE

/** @brief this binary can dispatch these independent schema semantics. */
#define CONFIT_SUPPORTED_SCHEMA_VERSIONS "1, 2"

/** @brief immutable v2 resolver identity written to generated artifacts. */
#define CONFIT_RESOLVER_ABI_V2 "confit-resolver-v2"

/** @brief immutable v2 generated-artifact identity. */
#define CONFIT_ARTIFACT_ABI_V2 "confit-artifact-v2"

/** @brief vendored TOML revision used by the v2 parser adapter. */
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

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
#define CONFIT_VERSION_MINOR 3

/**
 * @brief Confit patch version이다.
 */
#define CONFIT_VERSION_PATCH 0

/**
 * @brief bmake 단일 artifact ABI 전환 라벨이다.
 */
#define CONFIT_VERSION_LABEL "bmake.1"

/**
 * @brief semver prerelease까지 포함한 release 문자열이다.
 */
#define CONFIT_VERSION_RELEASE "0.3.0-bmake.1"

/**
 * @brief CLI와 generated artifact에 기록할 display 문자열이다.
 */
#define CONFIT_VERSION_DISPLAY "confit " CONFIT_VERSION_RELEASE

/** @brief this binary can dispatch these independent schema semantics. */
#define CONFIT_SUPPORTED_SCHEMA_VERSIONS "2"

/** @brief immutable v2 resolver identity written to generated artifacts. */
#define CONFIT_RESOLVER_ABI_V2 "confit-resolver-v2"

/** @brief sealed tool-neutral generated-artifact identity. */
#define CONFIT_ARTIFACT_ABI_V3 "confit-artifact-v3"

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

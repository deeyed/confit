#ifndef CONFIT_TARGET_PLAN_H
#define CONFIT_TARGET_PLAN_H

#include <stddef.h>

#include "confit/component_catalog.h"
#include "confit/diagnostic.h"
#include "confit/schema_v2.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 선택 target과 toolchain descriptor를 결속한 immutable build tuple이다.
 *
 * 모든 문자열과 list는 record가 소유한다. Executable field는 discovery가 끝난
 * canonical directory에 requested basename을 보존한 absolute invocation path이고
 * source descriptor에는 executable basename만 허용된다.
 */
typedef struct ConfitTargetPlan {
  char *target_id;
  char *isa;
  char *abi;
  char *cpu_profile;
  char *entry_profile;
  char *toolchain_id;
  char *toolchain_kind;
  char *target_triple;
  char *compiler_path;
  char *archiver_path;
  char *linker_path;
  char *resource_include_path;
  char *sysroot_path;
  char *link_emulation;
  char *linker_script;
  char *image_kind;
  char *package_profile;
  char *machine_profile;
  char *machine_runner;
  char *machine_architecture;
  char *machine_executable;
  char *machine_executable_path;
  char *machine_executable_sha256;
  char *machine_executable_version;
  char *machine_name;
  char *machine_cpu;
  char *machine_serial;
  char *machine_artifact;
  size_t machine_memory_mib;
  char *expected_component;
  char *expected_capability;
  char *output_stem;
  char *required_profile;
  char *dts_path;
  char *dtc_path;
  char *package_source;
  char *user_artifact_profile;
  char **private_include_paths;
  size_t private_include_count;
  size_t max_image_bytes;
  char *target_descriptor_path;
  char *toolchain_descriptor_path;
} ConfitTargetPlan;

/**
 * @brief exact target와 toolchain TOML을 closed schema로 읽고 executable을 봉인한다.
 */
ConfitStatus confit_target_plan_load(const ConfitV2Project *project,
                                     const char *target_id,
                                     ConfitTargetPlan *out_plan,
                                     ConfitDiagnostic *diagnostic);

/** @brief target plan의 모든 owned allocation을 해제한다. */
void confit_target_plan_clear(ConfitTargetPlan *plan);

/**
 * @brief expected component/capability가 실제 selected closure와 일치하는지 검사한다.
 */
ConfitStatus confit_target_plan_validate_selection(
    const ConfitTargetPlan *plan, const ConfitComponentCatalog *catalog,
    const ConfitComponentClosure *closure, ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_TARGET_PLAN_H */

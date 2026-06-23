#include <stdint.h>
#include <string.h>

#include "confit/model.h"
#include "confit/status.h"

static int expect_status(ConfitStatus status) { return status == CONFIT_OK; }

int main(void) {
  ConfitProject *project;
  ConfitOption *option;
  ConfitOption *found;
  ConfitChoice *choice;
  ConfitProfile *profile;
  ConfitTarget *target;
  ConfitValue value;
  ConfitValue copy;

  if (strcmp(confit_option_type_name(CONFIT_OPTION_TYPE_BOOL), "bool") != 0) {
    return 1;
  }
  if (strcmp(confit_option_type_name(CONFIT_OPTION_TYPE_FLOAT), "float") != 0) {
    return 2;
  }
  if (strcmp(confit_value_kind_name(CONFIT_VALUE_ENUM), "enum") != 0) {
    return 3;
  }

  confit_value_init(&value);
  confit_value_init(&copy);
  confit_value_set_bool(&value, 7);
  if (value.kind != CONFIT_VALUE_BOOL || value.as.bool_value != 1) {
    return 4;
  }
  confit_value_set_int(&value, -42);
  if (value.kind != CONFIT_VALUE_INT || value.as.int_value != -42) {
    return 5;
  }
  confit_value_set_uint(&value, UINT64_C(65535));
  if (value.kind != CONFIT_VALUE_UINT || value.as.uint_value != UINT64_C(65535)) {
    return 6;
  }
  if (!expect_status(confit_value_set_string(&value, "debug"))) {
    return 7;
  }
  if (!expect_status(confit_value_copy(&copy, &value))) {
    return 8;
  }
  if (copy.kind != CONFIT_VALUE_STRING ||
      strcmp(copy.as.string_value, "debug") != 0 ||
      copy.as.string_value == value.as.string_value) {
    return 9;
  }
  confit_value_clear(&copy);

  if (!expect_status(confit_value_set_enum(&value, "host-sim"))) {
    return 10;
  }
  if (!expect_status(confit_value_copy(&copy, &value))) {
    return 11;
  }
  if (copy.kind != CONFIT_VALUE_ENUM ||
      strcmp(copy.as.string_value, "host-sim") != 0 ||
      copy.as.string_value == value.as.string_value) {
    return 12;
  }
  confit_value_clear(&copy);

  project = confit_project_create();
  if (project == 0) {
    return 13;
  }
  if (!expect_status(
          confit_project_set_identity(project, "delos", "0.1.0", 1U))) {
    confit_project_free(project);
    return 14;
  }
  if (strcmp(project->name, "delos") != 0 || project->schema_version != 1U) {
    confit_project_free(project);
    return 15;
  }

  if (!expect_status(confit_project_add_option(project, &option))) {
    confit_project_free(project);
    return 16;
  }
  if (!expect_status(confit_option_set_identity(
          option, "delos.debug.ddc", CONFIT_OPTION_TYPE_BOOL))) {
    confit_project_free(project);
    return 17;
  }
  if (!expect_status(confit_option_set_metadata(
          option, "Enable DDC", "debug", "Include DDC command parser."))) {
    confit_project_free(project);
    return 18;
  }
  if (!expect_status(confit_option_add_tag(option, "debug")) ||
      !expect_status(confit_option_add_tag(option, "host-tooling"))) {
    confit_project_free(project);
    return 19;
  }
  confit_value_set_bool(&value, 1);
  if (!expect_status(confit_option_set_default(option, &value))) {
    confit_project_free(project);
    return 20;
  }
  found = confit_project_find_option(project, "delos.debug.ddc");
  if (found == 0 || found->default_value.as.bool_value != 1 ||
      found->tag_count != 2U) {
    confit_project_free(project);
    return 21;
  }

  if (!expect_status(confit_project_add_choice(project, &choice))) {
    confit_project_free(project);
    return 22;
  }
  if (!expect_status(confit_choice_set_identity(
          choice, "delos.target.board", "host-sim"))) {
    confit_project_free(project);
    return 23;
  }
  if (!expect_status(confit_choice_add_option(choice, "host-sim")) ||
      !expect_status(confit_choice_add_option(choice, "qemu-mps2-an500"))) {
    confit_project_free(project);
    return 24;
  }
  if (choice->option_count != 2U ||
      strcmp(choice->default_option, "host-sim") != 0) {
    confit_project_free(project);
    return 25;
  }

  if (!expect_status(confit_project_add_profile(project, &profile))) {
    confit_project_free(project);
    return 26;
  }
  if (!expect_status(confit_profile_set_identity(profile, "sim-dsh", "debug"))) {
    confit_project_free(project);
    return 27;
  }
  if (!expect_status(
          confit_profile_add_value(profile, "delos.debug.ddc", &value,
                                   "config/profiles/sim-dsh.toml"))) {
    confit_project_free(project);
    return 28;
  }
  if (profile->value_count != 1U ||
      strcmp(profile->values[0].option_id, "delos.debug.ddc") != 0 ||
      profile->values[0].value.as.bool_value != 1) {
    confit_project_free(project);
    return 29;
  }

  if (!expect_status(confit_project_add_target(project, &target))) {
    confit_project_free(project);
    return 30;
  }
  if (!expect_status(confit_target_set_identity(
          target, "host-sim", "host", "host-sim", "portability-probe"))) {
    confit_project_free(project);
    return 31;
  }
  if (!expect_status(confit_value_set_enum(&value, "host-sim"))) {
    confit_project_free(project);
    return 32;
  }
  if (!expect_status(
          confit_target_add_value(target, "delos.target.board", &value,
                                  "config/targets/host-sim.toml"))) {
    confit_project_free(project);
    return 33;
  }
  if (target->value_count != 1U ||
      target->values[0].value.kind != CONFIT_VALUE_ENUM ||
      strcmp(target->values[0].value.as.string_value, "host-sim") != 0) {
    confit_project_free(project);
    return 34;
  }

  confit_value_clear(&value);
  confit_project_free(project);
  return 0;
}

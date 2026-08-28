#ifndef CONFIT_EMITTER_H
#define CONFIT_EMITTER_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/model.h"
#include "confit/resolver.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The complete set of inert schema 6 consumer projections. */
typedef enum ConfitEmitterKind {
  CONFIT_EMITTER_INVALID = 0,
  CONFIT_EMITTER_MAKE,
  CONFIT_EMITTER_C_HEADER,
  CONFIT_EMITTER_JSON,
} ConfitEmitterKind;

/**
 * @brief Closed emitter request prepared by a controller such as the CLI.
 *
 * Every member must be zero or one and at least one projection must be
 * requested.  Selection is a set: it never creates ordering or precedence.
 */
typedef struct ConfitEmitRequest {
  int emit_make;
  int emit_c_header;
  int emit_json;
} ConfitEmitRequest;

/** @brief Borrowed view of one complete, inert emitted data artifact. */
typedef struct ConfitEmittedArtifactView {
  ConfitEmitterKind kind;
  const char *role;
  const char *name;
  const unsigned char *bytes;
  size_t size;
  int printable;
} ConfitEmittedArtifactView;

/** @brief Opaque owner of an all-or-nothing emitter result. */
typedef struct ConfitEmission ConfitEmission;

/**
 * @brief Emit only the requested deterministic consumer projections.
 *
 * The result contains no rules, source membership, commands, hooks, or build
 * graph.  Make output supports bool, int, hex, and enum only; the presence of
 * any string symbol makes a Make request fail as a whole.  On failure
 * `*out_emission` is null and no partial artifact is returned.
 */
ConfitStatus confit_emit(const ConfitResolution *resolution,
                         const ConfitEmitRequest *request,
                         const ConfitAllocator *allocator,
                         ConfitEmission **out_emission,
                         ConfitDiagnostic *diagnostic);

void confit_emission_destroy(ConfitEmission *emission);
size_t confit_emission_artifact_count(const ConfitEmission *emission);

/** @brief Borrow one artifact in the fixed Make, C-header, JSON order. */
int confit_emission_artifact_at(const ConfitEmission *emission, size_t index,
                                ConfitEmittedArtifactView *out_view);

/** @brief Borrow one requested artifact by its closed emitter kind. */
int confit_emission_find_artifact(const ConfitEmission *emission,
                                  ConfitEmitterKind kind,
                                  ConfitEmittedArtifactView *out_view);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_EMITTER_H */

#include "confit/snapshot.h"

#include <stdint.h>
#include <string.h>

#include "confit/digest.h"
#include "confit/emitter.h"
#include "confit/input.h"
#include "confit/limits.h"
#include "confit/version.h"
#include "../host/host_internal.h"
#include "snapshot_internal.h"

typedef struct ConfitSnapshotBuffer {
  unsigned char *bytes;
  size_t size;
  size_t capacity;
  ConfitAllocator allocator;
} ConfitSnapshotBuffer;

typedef struct ConfitSnapshotArtifact {
  const char *role;
  const char *name;
  const unsigned char *bytes;
  size_t size;
  int printable;
  char digest[65];
  ConfitSnapshotBuffer owned;
} ConfitSnapshotArtifact;

typedef struct ConfitSnapshotBuild {
  ConfitAllocator allocator;
  ConfitSnapshotArtifact artifacts[CONFIT_LIMIT_SNAPSHOT_ARTIFACTS];
  size_t artifact_count;
  size_t total_bytes;
  ConfitSnapshotBuffer seal;
  char digest[65];
} ConfitSnapshotBuild;

typedef struct ConfitSnapshotSealEntry {
  char role[CONFIT_LIMIT_SNAPSHOT_ROLE_BYTES + 1U];
  char name[CONFIT_LIMIT_SNAPSHOT_ARTIFACT_NAME_BYTES + 1U];
  int printable;
  size_t size;
  char digest[65];
} ConfitSnapshotSealEntry;

typedef struct ConfitSnapshotManifestPath {
  char path[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
} ConfitSnapshotManifestPath;

static const char kInvalidArgument[] = "invalid snapshot argument";
static const char kInvalidAllocator[] = "allocator capability is incomplete";
static const char kInvalidRequest[] =
    "snapshot inputs do not describe one resolved project";
static const char kInvalidArtifact[] = "snapshot artifact is invalid or duplicated";
static const char kTooLarge[] = "snapshot byte or artifact limit is exceeded";
static const char kOutOfMemory[] = "failed to allocate a bounded snapshot";
static const char kSerializeFailed[] = "failed to serialize canonical snapshot data";
static const char kInjectedFailure[] = "controlled snapshot publication failure";
static const char kCandidateMismatch[] = "snapshot candidate bytes do not match the seal";
static const char kExistingMismatch[] = "existing content-addressed snapshot is corrupt";
static const char kSelectedInvalid[] = "selected snapshot record is missing or invalid";
static const char kSealInvalid[] = "selected snapshot seal is invalid";
static const char kArtifactInvalid[] = "sealed snapshot artifact is missing or corrupt";
static const char kManifestInvalid[] = "snapshot input manifest is invalid";
static const char kInputStale[] = "manifest-listed configuration input is stale";
static const char kArtifactUnavailable[] = "snapshot artifact is not sealed for output";
static const char kPathBuffer[] = "verified artifact path buffer is too small";
static const char kLedgerLimit[] = "snapshot read ledger capacity is exceeded";

static int confit_snapshot_resolve_allocator(const ConfitAllocator *requested,
                                             ConfitAllocator *resolved);
static int confit_snapshot_artifact_name_is_valid(const char *name);
static int confit_snapshot_role_is_valid(const char *role);
static int confit_snapshot_is_core_name(const char *name);
static int confit_snapshot_request_is_consistent(
    const ConfitSnapshotPublishRequest *request);
static void confit_snapshot_build_init(ConfitSnapshotBuild *build,
                                       const ConfitAllocator *allocator);
static void confit_snapshot_build_destroy(ConfitSnapshotBuild *build);
static ConfitStatus confit_snapshot_build_prepare(
    const ConfitSnapshotPublishRequest *request,
    const ConfitAllocator *allocator, ConfitSnapshotBuild *build,
    ConfitDiagnostic *diagnostic);

static ConfitStatus confit_snapshot_fail(ConfitDiagnostic *diagnostic,
                                         ConfitStatus status,
                                         const char *path,
                                         const char *message) {
  confit_diagnostic_set(diagnostic, status, path, 0U, 0U, message);
  return status;
}

static int confit_snapshot_join_artifact_path(
    char *out, size_t out_size, const char *directory,
    const char *name) {
  const size_t directory_size = directory != 0 ? strlen(directory) : 0U;
  const size_t name_size = name != 0 ? strlen(name) : 0U;
  if (out == 0 || directory_size == 0U || name_size == 0U ||
      directory_size > CONFIT_LIMIT_SOURCE_PATH_BYTES ||
      name_size > CONFIT_LIMIT_SNAPSHOT_ARTIFACT_NAME_BYTES ||
      directory_size + 1U + name_size + 1U > out_size)
    return 0;
  memcpy(out, directory, directory_size);
  out[directory_size] = '/';
  memcpy(out + directory_size + 1U, name, name_size);
  out[directory_size + 1U + name_size] = '\0';
  return confit_host_relative_path_is_valid(out);
}

static int confit_snapshot_exact_file_matches(
    ConfitHostRoot *root, const char *path, const unsigned char *bytes,
    size_t size, const ConfitAllocator *allocator) {
  ConfitHostFile *file = 0;
  ConfitHostBuffer buffer;
  ConfitDiagnostic diagnostic;
  int matches = 0;
  confit_host_buffer_init(&buffer);
  confit_diagnostic_init(&diagnostic);
  if (confit_host_file_open(root, path, &file, &diagnostic) == CONFIT_OK &&
      confit_host_file_read(file, CONFIT_LIMIT_SNAPSHOT_BYTES, allocator,
                            &buffer, &diagnostic) == CONFIT_OK &&
      buffer.size == size &&
      (size == 0U || memcmp(buffer.bytes, bytes, size) == 0))
    matches = 1;
  confit_host_buffer_destroy(&buffer);
  confit_host_file_destroy(file);
  return matches;
}

static int confit_snapshot_bundle_matches(
    ConfitHostRoot *root, const char *directory,
    const ConfitSnapshotBuild *build, const ConfitAllocator *allocator) {
  char path[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  size_t index;
  if (!confit_snapshot_join_artifact_path(path, sizeof(path), directory,
                                          "snapshot.seal") ||
      !confit_snapshot_exact_file_matches(root, path, build->seal.bytes,
                                          build->seal.size, allocator))
    return 0;
  for (index = 0U; index < build->artifact_count; ++index) {
    if (!confit_snapshot_join_artifact_path(
            path, sizeof(path), directory, build->artifacts[index].name) ||
        !confit_snapshot_exact_file_matches(
            root, path, build->artifacts[index].bytes,
            build->artifacts[index].size, allocator))
      return 0;
  }
  return 1;
}

static ConfitStatus confit_snapshot_controlled_failure(
    ConfitSnapshotFailurePoint requested, ConfitSnapshotFailurePoint current,
    ConfitDiagnostic *diagnostic) {
  if (requested != current) return CONFIT_OK;
  return confit_snapshot_fail(diagnostic, CONFIT_ERR_IO, 0,
                              kInjectedFailure);
}

ConfitStatus confit_snapshot_publish_with_failure(
    ConfitHostRoot *output_root,
    const ConfitSnapshotPublishRequest *request,
    const ConfitAllocator *allocator,
    ConfitSnapshotFailurePoint failure_point,
    ConfitSnapshotPublication *out_publication,
    ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitSnapshotBuild build;
  ConfitHostDirectoryTransaction *transaction = 0;
  ConfitHostLock lock;
  ConfitDiagnostic release_diagnostic;
  char final_directory[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  char selected[CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES + 2U];
  const char *candidate_directory;
  size_t index;
  int directory_created = 0;
  int lock_held = 0;
  ConfitStatus status;
  if (out_publication == 0) return confit_snapshot_fail(
      diagnostic, CONFIT_ERR_USAGE, 0, kInvalidArgument);
  memset(out_publication, 0, sizeof(*out_publication));
  if (output_root == 0 || !confit_snapshot_request_is_consistent(request) ||
      failure_point < CONFIT_SNAPSHOT_FAILURE_NONE ||
      failure_point > CONFIT_SNAPSHOT_FAILURE_BEFORE_SELECTED)
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                kInvalidRequest);
  if (!confit_snapshot_resolve_allocator(allocator, &resolved))
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                kInvalidAllocator);
  confit_snapshot_build_init(&build, &resolved);
  status = confit_snapshot_build_prepare(request, &resolved, &build,
                                         diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  confit_host_lock_init(&lock);
  status = confit_host_lock_acquire(output_root, ".confit-snapshot.lock",
                                    &lock, diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  lock_held = 1;
  status = confit_snapshot_controlled_failure(
      failure_point, CONFIT_SNAPSHOT_FAILURE_AFTER_LOCK, diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  status = confit_host_directory_transaction_begin(
      output_root, "snapshots", &transaction, diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  candidate_directory =
      confit_host_directory_transaction_relative_path(transaction);
  if (candidate_directory == 0) {
    status = confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                  kCandidateMismatch);
    goto cleanup;
  }
  status = confit_snapshot_controlled_failure(
      failure_point, CONFIT_SNAPSHOT_FAILURE_AFTER_CANDIDATE, diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  for (index = 0U; index < build.artifact_count; ++index) {
    status = confit_host_directory_transaction_write(
        transaction, build.artifacts[index].name,
        build.artifacts[index].bytes, build.artifacts[index].size, 0444U,
        diagnostic);
    if (status != CONFIT_OK) goto cleanup;
  }
  status = confit_host_directory_transaction_write(
      transaction, "snapshot.seal", build.seal.bytes, build.seal.size,
      0444U, diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  status = confit_snapshot_controlled_failure(
      failure_point, CONFIT_SNAPSHOT_FAILURE_AFTER_CORE_FILES, diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  status = confit_host_directory_transaction_seal(transaction, 0555U,
                                                  diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  if (!confit_snapshot_bundle_matches(output_root, candidate_directory,
                                      &build, &resolved)) {
    status = confit_snapshot_fail(diagnostic, CONFIT_ERR_IO,
                                  candidate_directory, kCandidateMismatch);
    goto cleanup;
  }
  status = confit_host_directory_transaction_publish(
      transaction, build.digest, &directory_created, diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  confit_host_directory_transaction_destroy(transaction);
  transaction = 0;
  if (strlen("snapshots/") + strlen(build.digest) + 1U >
      sizeof(final_directory)) {
    status = confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                  kCandidateMismatch);
    goto cleanup;
  }
  memcpy(final_directory, "snapshots/", strlen("snapshots/"));
  memcpy(final_directory + strlen("snapshots/"), build.digest,
         strlen(build.digest) + 1U);
  if (!confit_snapshot_bundle_matches(output_root, final_directory, &build,
                                      &resolved)) {
    status = confit_snapshot_fail(diagnostic, CONFIT_ERR_IO, final_directory,
                                  directory_created ? kCandidateMismatch :
                                                      kExistingMismatch);
    goto cleanup;
  }
  status = confit_snapshot_controlled_failure(
      failure_point, CONFIT_SNAPSHOT_FAILURE_AFTER_DIRECTORY_PUBLICATION,
      diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  status = confit_snapshot_controlled_failure(
      failure_point, CONFIT_SNAPSHOT_FAILURE_BEFORE_SELECTED, diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  memcpy(selected, build.digest, CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES);
  selected[CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES] = '\n';
  selected[CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES + 1U] = '\0';
  status = confit_host_atomic_replace(
      output_root, "selected", selected,
      CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES + 1U, 0444U, diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  memcpy(out_publication->digest, build.digest, sizeof(build.digest));
  out_publication->reused_existing = !directory_created;

cleanup:
  if (status != CONFIT_OK)
    (void)confit_diagnostic_stabilize_path(diagnostic);
  confit_host_directory_transaction_destroy(transaction);
  if (lock_held) {
    confit_diagnostic_init(&release_diagnostic);
    if (confit_host_lock_release(&lock, &release_diagnostic) != CONFIT_OK &&
        status == CONFIT_OK) {
      status = confit_snapshot_fail(diagnostic, CONFIT_ERR_IO, 0,
                                    release_diagnostic.message);
      memset(out_publication, 0, sizeof(*out_publication));
    }
  }
  confit_snapshot_build_destroy(&build);
  return status;
}

ConfitStatus confit_snapshot_publish(
    ConfitHostRoot *output_root,
    const ConfitSnapshotPublishRequest *request,
    const ConfitAllocator *allocator,
    ConfitSnapshotPublication *out_publication,
    ConfitDiagnostic *diagnostic) {
  return confit_snapshot_publish_with_failure(
      output_root, request, allocator, CONFIT_SNAPSHOT_FAILURE_NONE,
      out_publication, diagnostic);
}

static int confit_snapshot_digest_is_valid(const char *digest) {
  size_t index;
  if (digest == 0 || strlen(digest) != CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES)
    return 0;
  for (index = 0U; index < CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES; ++index) {
    if (!((digest[index] >= '0' && digest[index] <= '9') ||
          (digest[index] >= 'a' && digest[index] <= 'f')))
      return 0;
  }
  return 1;
}

static int confit_snapshot_decimal(const char *text, size_t *out_value) {
  size_t value = 0U;
  size_t index;
  if (text == 0 || out_value == 0 || text[0] == '\0' ||
      (text[0] == '0' && text[1] != '\0'))
    return 0;
  for (index = 0U; text[index] != '\0'; ++index) {
    const unsigned digit = (unsigned)(text[index] - '0');
    if (text[index] < '0' || text[index] > '9' ||
        value > (SIZE_MAX - digit) / 10U)
      return 0;
    value = value * 10U + digit;
  }
  *out_value = value;
  return 1;
}

static int confit_snapshot_has_embedded_nul(const ConfitHostBuffer *buffer) {
  size_t index;
  for (index = 0U; index < buffer->size; ++index) {
    if (buffer->bytes[index] == '\0') return 1;
  }
  return 0;
}

static ConfitStatus confit_snapshot_read_file(
    ConfitHostRoot *root, const char *path, size_t maximum,
    const ConfitAllocator *allocator, ConfitHostBuffer *out_buffer,
    ConfitDiagnostic *diagnostic, const char *failure_message) {
  ConfitHostFile *file = 0;
  ConfitStatus status;
  status = confit_host_file_open(root, path, &file, diagnostic);
  if (status == CONFIT_OK)
    status = confit_host_file_read(file, maximum, allocator, out_buffer,
                                   diagnostic);
  confit_host_file_destroy(file);
  if (status != CONFIT_OK)
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE, path,
                                failure_message);
  return CONFIT_OK;
}

static int confit_snapshot_split_line(char *line, char **fields,
                                      size_t field_count) {
  size_t index = 0U;
  char *cursor = line;
  if (line == 0 || fields == 0 || field_count == 0U) return 0;
  fields[index++] = cursor;
  while (*cursor != '\0') {
    if (*cursor == '\t') {
      if (index >= field_count) return 0;
      *cursor = '\0';
      fields[index++] = cursor + 1;
    }
    cursor += 1U;
  }
  return index == field_count;
}

static ConfitStatus confit_snapshot_parse_seal(
    ConfitHostBuffer *buffer, ConfitSnapshotSealEntry *entries,
    size_t *out_count, ConfitDiagnostic *diagnostic) {
  static const char header[] = "confit-snapshot-seal-v1\n";
  char *cursor;
  char *end;
  size_t count = 0U;
  int saw_inputs = 0;
  int saw_provenance = 0;
  int saw_resolved = 0;
  int saw_user = 0;
  if (buffer == 0 || entries == 0 || out_count == 0 ||
      buffer->size < sizeof(header) - 1U ||
      memcmp(buffer->bytes, header, sizeof(header) - 1U) != 0 ||
      confit_snapshot_has_embedded_nul(buffer))
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                                "snapshot.seal", kSealInvalid);
  cursor = (char *)buffer->bytes + sizeof(header) - 1U;
  end = (char *)buffer->bytes + buffer->size;
  while (cursor < end) {
    ConfitSnapshotSealEntry *entry;
    char *fields[5];
    char *newline = (char *)memchr(cursor, '\n', (size_t)(end - cursor));
    if (newline == 0 || newline == cursor ||
        count >= CONFIT_LIMIT_SNAPSHOT_ARTIFACTS)
      return confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                                  "snapshot.seal", kSealInvalid);
    *newline = '\0';
    if (!confit_snapshot_split_line(cursor, fields, 5U) ||
        !confit_snapshot_role_is_valid(fields[0]) ||
        !confit_snapshot_artifact_name_is_valid(fields[1]) ||
        !((strcmp(fields[2], "0") == 0) ||
          (strcmp(fields[2], "1") == 0)) ||
        !confit_snapshot_decimal(fields[3], &entries[count].size) ||
        entries[count].size > CONFIT_LIMIT_SNAPSHOT_BYTES ||
        !confit_snapshot_digest_is_valid(fields[4]))
      return confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                                  "snapshot.seal", kSealInvalid);
    entry = &entries[count];
    memcpy(entry->role, fields[0], strlen(fields[0]) + 1U);
    memcpy(entry->name, fields[1], strlen(fields[1]) + 1U);
    entry->printable = fields[2][0] == '1';
    memcpy(entry->digest, fields[4], sizeof(entry->digest));
    if (count > 0U && strcmp(entries[count - 1U].name, entry->name) >= 0)
      return confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                                  "snapshot.seal", kSealInvalid);
    if (strcmp(entry->name, "inputs.manifest") == 0) {
      if (strcmp(entry->role, "inputs") != 0 || !entry->printable)
        goto invalid_core;
      saw_inputs = 1;
    } else if (strcmp(entry->name, "provenance.json") == 0) {
      if (strcmp(entry->role, "provenance") != 0 || !entry->printable)
        goto invalid_core;
      saw_provenance = 1;
    } else if (strcmp(entry->name, "resolved-values.json") == 0) {
      if (strcmp(entry->role, "resolved-values") != 0)
        goto invalid_core;
      saw_resolved = 1;
    } else if (strcmp(entry->name, "user-values.toml") == 0) {
      if (strcmp(entry->role, "user-values") != 0 || !entry->printable)
        goto invalid_core;
      saw_user = 1;
    } else if (confit_snapshot_is_core_name(entry->name)) {
      goto invalid_core;
    }
    count += 1U;
    cursor = newline + 1U;
  }
  if (!saw_inputs || !saw_provenance || !saw_resolved || !saw_user)
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                                "snapshot.seal", kSealInvalid);
  *out_count = count;
  return CONFIT_OK;

invalid_core:
  return confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                              "snapshot.seal", kSealInvalid);
}

static ConfitStatus confit_snapshot_verify_artifacts(
    ConfitHostRoot *output_root, const char *directory,
    const ConfitSnapshotSealEntry *entries, size_t entry_count,
    size_t initial_bytes, const ConfitAllocator *allocator,
    ConfitHostBuffer *out_manifest, ConfitDiagnostic *diagnostic) {
  char path[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  size_t total = initial_bytes;
  size_t index;
  if (out_manifest == 0 || out_manifest->bytes != 0 ||
      out_manifest->size != 0U)
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kManifestInvalid);
  for (index = 0U; index < entry_count; ++index) {
    ConfitHostBuffer buffer;
    char digest[65];
    ConfitStatus status;
    confit_host_buffer_init(&buffer);
    if (!confit_snapshot_join_artifact_path(path, sizeof(path), directory,
                                            entries[index].name))
      return confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                  kArtifactInvalid);
    status = confit_snapshot_read_file(
        output_root, path, CONFIT_LIMIT_SNAPSHOT_BYTES, allocator, &buffer,
        diagnostic, kArtifactInvalid);
    if (status != CONFIT_OK) return status;
    if (buffer.size != entries[index].size ||
        total > CONFIT_LIMIT_SNAPSHOT_BYTES ||
        buffer.size > CONFIT_LIMIT_SNAPSHOT_BYTES - total) {
      confit_host_buffer_destroy(&buffer);
      return confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE, path,
                                  kArtifactInvalid);
    }
    confit_sha256_bytes(buffer.bytes, buffer.size, digest);
    if (strcmp(digest, entries[index].digest) != 0) {
      confit_host_buffer_destroy(&buffer);
      return confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE, path,
                                  kArtifactInvalid);
    }
    total += buffer.size;
    if (strcmp(entries[index].name, "inputs.manifest") == 0) {
      *out_manifest = buffer;
      confit_host_buffer_init(&buffer);
    }
    confit_host_buffer_destroy(&buffer);
  }
  if (out_manifest->bytes == 0)
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                                "inputs.manifest", kManifestInvalid);
  return CONFIT_OK;
}

void confit_snapshot_read_ledger_init(ConfitSnapshotReadLedger *ledger,
                                      ConfitSnapshotReadRecord *records,
                                      size_t capacity) {
  if (ledger == 0) return;
  ledger->records = records;
  ledger->capacity = records != 0 ? capacity : 0U;
  ledger->count = 0U;
}

static int confit_snapshot_toml_path(const char *path) {
  const size_t size = path != 0 ? strlen(path) : 0U;
  return size >= 5U && confit_host_relative_path_is_valid(path) &&
         strcmp(path + size - 5U, ".toml") == 0;
}

static ConfitStatus confit_snapshot_record_input(
    ConfitSnapshotReadLedger *ledger, const char *path, size_t size,
    ConfitDiagnostic *diagnostic) {
  if (ledger == 0) return CONFIT_OK;
  if (ledger->count >= ledger->capacity)
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, path,
                                kLedgerLimit);
  memcpy(ledger->records[ledger->count].path, path, strlen(path) + 1U);
  ledger->records[ledger->count].byte_count = size;
  ledger->count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_snapshot_verify_manifest(
    ConfitHostRoot *project_root, const char *expected_entry_path,
    ConfitHostBuffer *manifest, const ConfitAllocator *allocator,
    ConfitSnapshotReadLedger *ledger, ConfitDiagnostic *diagnostic) {
  static const char header[] = "confit-inputs-v1\n";
  ConfitSnapshotManifestPath *paths = 0;
  char *cursor;
  char *end;
  size_t line_count = 0U;
  size_t count = 0U;
  size_t total = 0U;
  size_t index;
  int saw_user = 0;
  ConfitStatus status = CONFIT_OK;
  if (manifest == 0 || project_root == 0 ||
      !confit_snapshot_toml_path(expected_entry_path) ||
      manifest->size < sizeof(header) - 1U ||
      memcmp(manifest->bytes, header, sizeof(header) - 1U) != 0 ||
      confit_snapshot_has_embedded_nul(manifest))
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                                "inputs.manifest", kManifestInvalid);
  for (index = sizeof(header) - 1U; index < manifest->size; ++index)
    if (manifest->bytes[index] == '\n') line_count += 1U;
  if (line_count == 0U ||
      line_count > CONFIT_LIMIT_SOURCE_FRAGMENTS + 1U ||
      (ledger != 0 && ledger->capacity < line_count))
    return confit_snapshot_fail(
        diagnostic, ledger != 0 && ledger->capacity < line_count
                        ? CONFIT_ERR_INTERNAL
                        : CONFIT_ERR_STALE,
        "inputs.manifest",
        ledger != 0 && ledger->capacity < line_count ? kLedgerLimit :
                                                       kManifestInvalid);
  if (line_count > SIZE_MAX / sizeof(*paths))
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kOutOfMemory);
  paths = (ConfitSnapshotManifestPath *)allocator->allocate(
      allocator->context, line_count * sizeof(*paths));
  if (paths == 0)
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kOutOfMemory);
  cursor = (char *)manifest->bytes + sizeof(header) - 1U;
  end = (char *)manifest->bytes + manifest->size;
  while (cursor < end) {
    char *fields[4];
    char *newline = (char *)memchr(cursor, '\n', (size_t)(end - cursor));
    size_t declared_size;
    ConfitHostBuffer input;
    char digest[65];
    size_t previous;
    if (newline == 0 || newline == cursor || count >= line_count) {
      status = confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                                    "inputs.manifest", kManifestInvalid);
      goto cleanup;
    }
    *newline = '\0';
    if (!confit_snapshot_split_line(cursor, fields, 4U) ||
        !confit_snapshot_decimal(fields[1], &declared_size) ||
        declared_size > CONFIT_LIMIT_TOML_FILE_BYTES ||
        !confit_snapshot_digest_is_valid(fields[2]) ||
        !confit_snapshot_toml_path(fields[3]) ||
        !((count == 0U && strcmp(fields[0], "entry") == 0) ||
          (count > 0U && !saw_user &&
           (strcmp(fields[0], "fragment") == 0 ||
            strcmp(fields[0], "user") == 0)))) {
      status = confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                                    "inputs.manifest", kManifestInvalid);
      goto cleanup;
    }
    if (count == 0U && strcmp(fields[3], expected_entry_path) != 0) {
      status = confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE, fields[3],
                                    kManifestInvalid);
      goto cleanup;
    }
    if (strcmp(fields[0], "user") == 0) saw_user = 1;
    for (previous = 0U; previous < count; ++previous) {
      if (strcmp(paths[previous].path, fields[3]) == 0) {
        status = confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                                      fields[3], kManifestInvalid);
        goto cleanup;
      }
    }
    memcpy(paths[count].path, fields[3], strlen(fields[3]) + 1U);
    confit_host_buffer_init(&input);
    status = confit_snapshot_read_file(
        project_root, fields[3], CONFIT_LIMIT_TOML_FILE_BYTES, allocator,
        &input, diagnostic, kInputStale);
    if (status != CONFIT_OK) goto cleanup;
    confit_sha256_bytes(input.bytes, input.size, digest);
    if (input.size != declared_size || strcmp(digest, fields[2]) != 0 ||
        total > CONFIT_LIMIT_TOTAL_INPUT_BYTES ||
        input.size > CONFIT_LIMIT_TOTAL_INPUT_BYTES - total) {
      confit_host_buffer_destroy(&input);
      status = confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE, fields[3],
                                    kInputStale);
      goto cleanup;
    }
    total += input.size;
    status = confit_snapshot_record_input(ledger, fields[3], input.size,
                                          diagnostic);
    confit_host_buffer_destroy(&input);
    if (status != CONFIT_OK) goto cleanup;
    count += 1U;
    cursor = newline + 1U;
    if (saw_user && cursor < end) {
      status = confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                                    "inputs.manifest", kManifestInvalid);
      goto cleanup;
    }
  }
  if (count != line_count)
    status = confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                                  "inputs.manifest", kManifestInvalid);

cleanup:
  allocator->deallocate(allocator->context, paths);
  return status;
}

ConfitStatus confit_snapshot_verify_observed(
    const ConfitSnapshotVerifyRequest *request,
    const ConfitAllocator *allocator,
    ConfitSnapshotReadLedger *ledger,
    char *out_artifact_relative_path,
    size_t out_artifact_relative_path_size,
    ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitHostBuffer selected;
  ConfitHostBuffer seal;
  ConfitHostBuffer manifest;
  ConfitSnapshotSealEntry entries[CONFIT_LIMIT_SNAPSHOT_ARTIFACTS];
  char digest[65];
  char directory[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  char seal_path[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  char verified_path[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  size_t entry_count = 0U;
  size_t index;
  int requested_found = 0;
  ConfitStatus status;
  confit_host_buffer_init(&selected);
  confit_host_buffer_init(&seal);
  confit_host_buffer_init(&manifest);
  if (out_artifact_relative_path != 0 &&
      out_artifact_relative_path_size != 0U)
    out_artifact_relative_path[0] = '\0';
  if (request == 0 || request->project_root == 0 ||
      request->output_root == 0 ||
      !confit_snapshot_toml_path(request->expected_entry_path) ||
      ((request->artifact_name == 0) !=
       (out_artifact_relative_path == 0 &&
        out_artifact_relative_path_size == 0U)) ||
      (request->artifact_name != 0 &&
       (out_artifact_relative_path == 0 ||
        out_artifact_relative_path_size == 0U ||
        !confit_snapshot_artifact_name_is_valid(request->artifact_name))) ||
      (ledger != 0 &&
       (ledger->count != 0U ||
        (ledger->records == 0 && ledger->capacity != 0U))))
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                kInvalidArgument);
  if (!confit_snapshot_resolve_allocator(allocator, &resolved))
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                kInvalidAllocator);
  status = confit_snapshot_read_file(
      request->output_root, "selected",
      CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES + 1U, &resolved, &selected,
      diagnostic, kSelectedInvalid);
  if (status != CONFIT_OK) goto cleanup;
  if (selected.size != CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES + 1U ||
      selected.bytes[CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES] != '\n') {
    status = confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE, "selected",
                                  kSelectedInvalid);
    goto cleanup;
  }
  memcpy(digest, selected.bytes, CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES);
  digest[CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES] = '\0';
  if (!confit_snapshot_digest_is_valid(digest) ||
      strlen("snapshots/") + strlen(digest) + 1U > sizeof(directory)) {
    status = confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE, "selected",
                                  kSelectedInvalid);
    goto cleanup;
  }
  memcpy(directory, "snapshots/", strlen("snapshots/"));
  memcpy(directory + strlen("snapshots/"), digest, strlen(digest) + 1U);
  if (!confit_snapshot_join_artifact_path(seal_path, sizeof(seal_path),
                                          directory, "snapshot.seal")) {
    status = confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                  kSealInvalid);
    goto cleanup;
  }
  status = confit_snapshot_read_file(
      request->output_root, seal_path, CONFIT_LIMIT_SNAPSHOT_BYTES, &resolved,
      &seal, diagnostic, kSealInvalid);
  if (status != CONFIT_OK) goto cleanup;
  confit_sha256_bytes(seal.bytes, seal.size, verified_path);
  if (strcmp(verified_path, digest) != 0) {
    status = confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE, seal_path,
                                  kSealInvalid);
    goto cleanup;
  }
  status = confit_snapshot_parse_seal(&seal, entries, &entry_count,
                                      diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  status = confit_snapshot_verify_artifacts(
      request->output_root, directory, entries, entry_count, seal.size,
      &resolved, &manifest, diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  status = confit_snapshot_verify_manifest(
      request->project_root, request->expected_entry_path, &manifest,
      &resolved, ledger, diagnostic);
  if (status != CONFIT_OK) goto cleanup;
  if (request->artifact_name != 0) {
    if (strcmp(request->artifact_name, "snapshot.seal") == 0) {
      memcpy(verified_path, seal_path, strlen(seal_path) + 1U);
      requested_found = 1;
    } else {
      for (index = 0U; index < entry_count; ++index) {
        if (strcmp(entries[index].name, request->artifact_name) != 0)
          continue;
        if (!entries[index].printable) {
          status = confit_snapshot_fail(
              diagnostic, CONFIT_ERR_STALE, request->artifact_name,
              kArtifactUnavailable);
          goto cleanup;
        }
        if (!confit_snapshot_join_artifact_path(
                verified_path, sizeof(verified_path), directory,
                request->artifact_name)) {
          status = confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                        kArtifactInvalid);
          goto cleanup;
        }
        requested_found = 1;
        break;
      }
    }
    if (!requested_found) {
      status = confit_snapshot_fail(diagnostic, CONFIT_ERR_STALE,
                                    request->artifact_name,
                                    kArtifactUnavailable);
      goto cleanup;
    }
    if (strlen(verified_path) + 1U > out_artifact_relative_path_size) {
      status = confit_snapshot_fail(diagnostic, CONFIT_ERR_USAGE,
                                    request->artifact_name, kPathBuffer);
      goto cleanup;
    }
    memcpy(out_artifact_relative_path, verified_path,
           strlen(verified_path) + 1U);
  }
  status = CONFIT_OK;

cleanup:
  if (status != CONFIT_OK)
    (void)confit_diagnostic_stabilize_path(diagnostic);
  confit_host_buffer_destroy(&manifest);
  confit_host_buffer_destroy(&seal);
  confit_host_buffer_destroy(&selected);
  return status;
}

ConfitStatus confit_snapshot_verify(
    const ConfitSnapshotVerifyRequest *request,
    const ConfitAllocator *allocator,
    char *out_artifact_relative_path,
    size_t out_artifact_relative_path_size,
    ConfitDiagnostic *diagnostic) {
  return confit_snapshot_verify_observed(
      request, allocator, 0, out_artifact_relative_path,
      out_artifact_relative_path_size, diagnostic);
}

static int confit_snapshot_resolve_allocator(const ConfitAllocator *requested,
                                             ConfitAllocator *resolved) {
  if (resolved == 0) return 0;
  if (requested == 0) {
    confit_allocator_default(resolved);
    return 1;
  }
  if (!confit_allocator_is_valid(requested)) return 0;
  *resolved = *requested;
  return 1;
}

static void confit_snapshot_buffer_init(ConfitSnapshotBuffer *buffer,
                                        const ConfitAllocator *allocator) {
  memset(buffer, 0, sizeof(*buffer));
  buffer->allocator = *allocator;
}

static void confit_snapshot_buffer_destroy(ConfitSnapshotBuffer *buffer) {
  if (buffer == 0) return;
  if (buffer->bytes != 0)
    buffer->allocator.deallocate(buffer->allocator.context, buffer->bytes);
  memset(buffer, 0, sizeof(*buffer));
}

static int confit_snapshot_buffer_reserve(ConfitSnapshotBuffer *buffer,
                                          size_t extra) {
  unsigned char *replacement;
  size_t required;
  size_t capacity;
  if (buffer == 0 || buffer->size > CONFIT_LIMIT_SNAPSHOT_BYTES ||
      extra > CONFIT_LIMIT_SNAPSHOT_BYTES - buffer->size)
    return 0;
  required = buffer->size + extra;
  if (required <= buffer->capacity) return 1;
  capacity = buffer->capacity == 0U ? 256U : buffer->capacity;
  while (capacity < required) {
    if (capacity > CONFIT_LIMIT_SNAPSHOT_BYTES / 2U) {
      capacity = required;
      break;
    }
    capacity *= 2U;
  }
  if (capacity > CONFIT_LIMIT_SNAPSHOT_BYTES) return 0;
  replacement = (unsigned char *)buffer->allocator.allocate(
      buffer->allocator.context, capacity + 1U);
  if (replacement == 0) return 0;
  if (buffer->size != 0U)
    memcpy(replacement, buffer->bytes, buffer->size);
  if (buffer->bytes != 0)
    buffer->allocator.deallocate(buffer->allocator.context, buffer->bytes);
  buffer->bytes = replacement;
  buffer->capacity = capacity;
  buffer->bytes[buffer->size] = '\0';
  return 1;
}

static int confit_snapshot_buffer_append(ConfitSnapshotBuffer *buffer,
                                         const void *bytes, size_t size) {
  if ((bytes == 0 && size != 0U) ||
      !confit_snapshot_buffer_reserve(buffer, size))
    return 0;
  if (size != 0U) memcpy(buffer->bytes + buffer->size, bytes, size);
  buffer->size += size;
  buffer->bytes[buffer->size] = '\0';
  return 1;
}

static int confit_snapshot_buffer_text(ConfitSnapshotBuffer *buffer,
                                       const char *text) {
  return text != 0 &&
         confit_snapshot_buffer_append(buffer, text, strlen(text));
}

static int confit_snapshot_buffer_unsigned(ConfitSnapshotBuffer *buffer,
                                           uint64_t value, unsigned base) {
  static const char digits[] = "0123456789abcdef";
  char reverse[32];
  char forward[32];
  size_t count = 0U;
  size_t index;
  do {
    reverse[count++] = digits[value % base];
    value /= base;
  } while (value != 0U);
  for (index = 0U; index < count; ++index)
    forward[index] = reverse[count - index - 1U];
  return confit_snapshot_buffer_append(buffer, forward, count);
}

static int confit_snapshot_json_string(ConfitSnapshotBuffer *buffer,
                                       const char *text, size_t size) {
  static const char hex[] = "0123456789abcdef";
  size_t index;
  if (text == 0 || !confit_snapshot_buffer_text(buffer, "\"")) return 0;
  for (index = 0U; index < size; ++index) {
    const unsigned char byte = (unsigned char)text[index];
    const char *escape = 0;
    char unicode[6];
    if (byte == (unsigned char)'\"') escape = "\\\"";
    else if (byte == (unsigned char)'\\') escape = "\\\\";
    else if (byte == (unsigned char)'\b') escape = "\\b";
    else if (byte == (unsigned char)'\f') escape = "\\f";
    else if (byte == (unsigned char)'\n') escape = "\\n";
    else if (byte == (unsigned char)'\r') escape = "\\r";
    else if (byte == (unsigned char)'\t') escape = "\\t";
    if (escape != 0) {
      if (!confit_snapshot_buffer_text(buffer, escape)) return 0;
    } else if (byte < 0x20U || byte == 0x7FU) {
      unicode[0] = '\\';
      unicode[1] = 'u';
      unicode[2] = '0';
      unicode[3] = '0';
      unicode[4] = hex[byte >> 4U];
      unicode[5] = hex[byte & 0x0fU];
      if (!confit_snapshot_buffer_append(buffer, unicode, sizeof(unicode)))
        return 0;
    } else if (!confit_snapshot_buffer_append(buffer, text + index, 1U)) {
      return 0;
    }
  }
  return confit_snapshot_buffer_text(buffer, "\"");
}

static ConfitStatus confit_snapshot_make_resolved_json(
    const ConfitResolution *resolution, ConfitSnapshotBuffer *buffer,
    ConfitDiagnostic *diagnostic) {
  ConfitEmitRequest request;
  ConfitEmission *emission = 0;
  ConfitEmittedArtifactView artifact;
  ConfitStatus status;
  memset(&request, 0, sizeof(request));
  request.emit_json = 1;
  status = confit_emit(resolution, &request, &buffer->allocator, &emission,
                       diagnostic);
  if (status != CONFIT_OK) return status;
  if (!confit_emission_find_artifact(emission, CONFIT_EMITTER_JSON,
                                     &artifact) ||
      strcmp(artifact.role, "resolved-values") != 0 ||
      strcmp(artifact.name, "resolved-values.json") != 0 ||
      !confit_snapshot_buffer_append(buffer, artifact.bytes, artifact.size)) {
    confit_emission_destroy(emission);
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kSerializeFailed);
  }
  confit_emission_destroy(emission);
  return CONFIT_OK;
}

static int confit_snapshot_artifact_name_is_valid(const char *name) {
  size_t index;
  size_t size;
  if (name == 0) return 0;
  size = strlen(name);
  if (size == 0U || size > CONFIT_LIMIT_SNAPSHOT_ARTIFACT_NAME_BYTES ||
      !((name[0] >= 'A' && name[0] <= 'Z') ||
        (name[0] >= 'a' && name[0] <= 'z') ||
        (name[0] >= '0' && name[0] <= '9')))
    return 0;
  for (index = 0U; index < size; ++index) {
    const unsigned char byte = (unsigned char)name[index];
    if (!((byte >= 'A' && byte <= 'Z') ||
          (byte >= 'a' && byte <= 'z') ||
          (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' ||
          byte == '-'))
      return 0;
  }
  return 1;
}

static int confit_snapshot_role_is_valid(const char *role) {
  size_t index;
  size_t size;
  if (role == 0) return 0;
  size = strlen(role);
  if (size == 0U || size > CONFIT_LIMIT_SNAPSHOT_ROLE_BYTES ||
      role[0] < 'a' || role[0] > 'z')
    return 0;
  for (index = 0U; index < size; ++index) {
    const unsigned char byte = (unsigned char)role[index];
    if (!((byte >= 'a' && byte <= 'z') ||
          (byte >= '0' && byte <= '9') || byte == '-'))
      return 0;
  }
  return 1;
}

static int confit_snapshot_is_core_name(const char *name) {
  return strcmp(name, "inputs.manifest") == 0 ||
         strcmp(name, "provenance.json") == 0 ||
         strcmp(name, "resolved-values.json") == 0 ||
         strcmp(name, "snapshot.seal") == 0 ||
         strcmp(name, "user-values.toml") == 0;
}

static int confit_snapshot_request_is_consistent(
    const ConfitSnapshotPublishRequest *request) {
  const ConfitCatalog *catalog;
  size_t assignment_count = 0U;
  size_t user_origins = 0U;
  size_t index;
  const ConfitAssignment *assignments;
  if (request == 0 || request->project == 0 || request->resolution == 0 ||
      request->resolved_values_printable < 0 ||
      request->resolved_values_printable > 1)
    return 0;
  catalog = confit_schema_project_catalog(request->project);
  if (catalog == 0 || confit_resolution_catalog(request->resolution) != catalog)
    return 0;
  assignments = confit_user_config_assignments(request->user_config,
                                                &assignment_count);
  for (index = 0U;
       index < confit_resolution_value_count(request->resolution); ++index) {
    const ConfitResolvedValue *resolved = 0;
    size_t assignment_index;
    int found = 0;
    if (!confit_resolution_value_at(request->resolution, index, &resolved) ||
        resolved == 0)
      return 0;
    if (resolved->origin != CONFIT_ORIGIN_USER) continue;
    user_origins += 1U;
    for (assignment_index = 0U; assignment_index < assignment_count;
         ++assignment_index) {
      if (strcmp(assignments[assignment_index].symbol, resolved->symbol) == 0) {
        if (!confit_value_equal(&assignments[assignment_index].value,
                                &resolved->effective_value))
          return 0;
        found = 1;
        break;
      }
    }
    if (!found) return 0;
  }
  return user_origins == assignment_count;
}

static void confit_snapshot_build_init(ConfitSnapshotBuild *build,
                                       const ConfitAllocator *allocator) {
  memset(build, 0, sizeof(*build));
  build->allocator = *allocator;
  confit_snapshot_buffer_init(&build->seal, allocator);
}

static void confit_snapshot_build_destroy(ConfitSnapshotBuild *build) {
  size_t index;
  if (build == 0) return;
  for (index = 0U; index < build->artifact_count; ++index)
    confit_snapshot_buffer_destroy(&build->artifacts[index].owned);
  confit_snapshot_buffer_destroy(&build->seal);
  memset(build, 0, sizeof(*build));
}

static ConfitStatus confit_snapshot_add_artifact(
    ConfitSnapshotBuild *build, const char *role, const char *name,
    const unsigned char *bytes, size_t size, int printable,
    ConfitSnapshotBuffer *owned, ConfitDiagnostic *diagnostic) {
  ConfitSnapshotArtifact *artifact;
  size_t index;
  if (build == 0 || !confit_snapshot_role_is_valid(role) ||
      !confit_snapshot_artifact_name_is_valid(name) ||
      (bytes == 0 && size != 0U) || size > CONFIT_LIMIT_SNAPSHOT_BYTES ||
      (printable != 0 && printable != 1) ||
      build->artifact_count >= CONFIT_LIMIT_SNAPSHOT_ARTIFACTS ||
      build->total_bytes > CONFIT_LIMIT_SNAPSHOT_BYTES ||
      size > CONFIT_LIMIT_SNAPSHOT_BYTES - build->total_bytes)
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_VALIDATION, name,
                                kTooLarge);
  for (index = 0U; index < build->artifact_count; ++index) {
    if (strcmp(build->artifacts[index].name, name) == 0)
      return confit_snapshot_fail(diagnostic, CONFIT_ERR_VALIDATION, name,
                                  kInvalidArtifact);
  }
  artifact = &build->artifacts[build->artifact_count++];
  memset(artifact, 0, sizeof(*artifact));
  artifact->role = role;
  artifact->name = name;
  artifact->bytes = bytes;
  artifact->size = size;
  artifact->printable = printable;
  if (owned != 0) {
    artifact->owned = *owned;
    memset(owned, 0, sizeof(*owned));
    artifact->bytes = artifact->owned.bytes;
  }
  confit_sha256_bytes(artifact->bytes, artifact->size, artifact->digest);
  build->total_bytes += size;
  return CONFIT_OK;
}

static void confit_snapshot_sort_artifacts(ConfitSnapshotBuild *build) {
  size_t index;
  for (index = 1U; index < build->artifact_count; ++index) {
    ConfitSnapshotArtifact value = build->artifacts[index];
    size_t cursor = index;
    while (cursor > 0U &&
           strcmp(build->artifacts[cursor - 1U].name, value.name) > 0) {
      build->artifacts[cursor] = build->artifacts[cursor - 1U];
      cursor -= 1U;
    }
    build->artifacts[cursor] = value;
  }
}

static ConfitStatus confit_snapshot_make_user_values(
    const ConfitResolution *resolution, const ConfitAllocator *allocator,
    ConfitSnapshotBuffer *buffer, ConfitDiagnostic *diagnostic) {
  size_t size = 0U;
  ConfitStatus status;
  confit_snapshot_buffer_init(buffer, allocator);
  status = confit_user_config_format_minimal(resolution, 0, 0U, &size,
                                             diagnostic);
  if (status != CONFIT_OK) return status;
  if (!confit_snapshot_buffer_reserve(buffer, size))
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kOutOfMemory);
  status = confit_user_config_format_minimal(
      resolution, (char *)buffer->bytes, size + 1U, &buffer->size, diagnostic);
  if (status != CONFIT_OK) return status;
  return CONFIT_OK;
}

static ConfitStatus confit_snapshot_append_manifest_input(
    ConfitSnapshotBuffer *buffer, const char *kind,
    const ConfitInputImage *image, ConfitDiagnostic *diagnostic) {
  const unsigned char *bytes;
  const char *digest;
  const char *path;
  size_t size = 0U;
  if (buffer == 0 || kind == 0 || image == 0 ||
      (path = confit_input_image_path(image)) == 0 ||
      (digest = confit_input_image_digest(image)) == 0 ||
      (bytes = confit_input_image_bytes(image, &size)) == 0 ||
      (bytes == 0 && size != 0U))
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kSerializeFailed);
  if (!confit_snapshot_buffer_text(buffer, kind) ||
      !confit_snapshot_buffer_text(buffer, "\t") ||
      !confit_snapshot_buffer_unsigned(buffer, (uint64_t)size, 10U) ||
      !confit_snapshot_buffer_text(buffer, "\t") ||
      !confit_snapshot_buffer_text(buffer, digest) ||
      !confit_snapshot_buffer_text(buffer, "\t") ||
      !confit_snapshot_buffer_text(buffer, path) ||
      !confit_snapshot_buffer_text(buffer, "\n"))
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_VALIDATION, path,
                                kTooLarge);
  return CONFIT_OK;
}

static ConfitStatus confit_snapshot_make_manifest(
    const ConfitSnapshotPublishRequest *request,
    const ConfitAllocator *allocator, ConfitSnapshotBuffer *buffer,
    ConfitDiagnostic *diagnostic) {
  const ConfitSourceGraph *graph =
      confit_schema_project_source_graph(request->project);
  const ConfitInputImage *user_input =
      confit_user_config_input(request->user_config);
  size_t total;
  size_t index;
  if (graph == 0 || confit_source_graph_node_count(graph) == 0U)
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kSerializeFailed);
  total = confit_source_graph_total_bytes(graph);
  if (user_input != 0 &&
      confit_input_image_accumulate(total, user_input, &total, diagnostic) !=
          CONFIT_OK)
    return CONFIT_ERR_VALIDATION;
  confit_snapshot_buffer_init(buffer, allocator);
  if (!confit_snapshot_buffer_text(buffer, "confit-inputs-v1\n"))
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kOutOfMemory);
  for (index = 0U; index < confit_source_graph_node_count(graph); ++index) {
    ConfitSourceNodeView node;
    if (!confit_source_graph_node_at(graph, index, &node))
      return confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                  kSerializeFailed);
    if (confit_snapshot_append_manifest_input(
            buffer, index == 0U ? "entry" : "fragment", node.input,
            diagnostic) != CONFIT_OK)
      return diagnostic != 0 ? diagnostic->status : CONFIT_ERR_INTERNAL;
    if (user_input != 0 &&
        strcmp(confit_input_image_path(user_input), node.path) == 0)
      return confit_snapshot_fail(diagnostic, CONFIT_ERR_VALIDATION,
                                  node.path, kInvalidRequest);
  }
  if (user_input != 0)
    return confit_snapshot_append_manifest_input(buffer, "user", user_input,
                                                 diagnostic);
  return CONFIT_OK;
}

static ConfitStatus confit_snapshot_make_provenance(
    const ConfitAllocator *allocator, ConfitSnapshotBuffer *buffer,
    ConfitDiagnostic *diagnostic) {
  const char *version = confit_version_string();
  confit_snapshot_buffer_init(buffer, allocator);
  if (!confit_snapshot_buffer_text(
          buffer, "{\"format\":\"confit-provenance-v1\","
                  "\"schema_version\":6,\"confit_version\":") ||
      !confit_snapshot_json_string(buffer, version, strlen(version)) ||
      !confit_snapshot_buffer_text(buffer, "}\n"))
    return confit_snapshot_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kOutOfMemory);
  return CONFIT_OK;
}

static ConfitStatus confit_snapshot_make_seal(ConfitSnapshotBuild *build,
                                               ConfitDiagnostic *diagnostic) {
  size_t index;
  if (!confit_snapshot_buffer_text(&build->seal,
                                   "confit-snapshot-seal-v1\n"))
    goto failed;
  for (index = 0U; index < build->artifact_count; ++index) {
    const ConfitSnapshotArtifact *artifact = &build->artifacts[index];
    if (!confit_snapshot_buffer_text(&build->seal, artifact->role) ||
        !confit_snapshot_buffer_text(&build->seal, "\t") ||
        !confit_snapshot_buffer_text(&build->seal, artifact->name) ||
        !confit_snapshot_buffer_text(&build->seal,
                                     artifact->printable ? "\t1\t" :
                                                           "\t0\t") ||
        !confit_snapshot_buffer_unsigned(&build->seal,
                                         (uint64_t)artifact->size, 10U) ||
        !confit_snapshot_buffer_text(&build->seal, "\t") ||
        !confit_snapshot_buffer_text(&build->seal, artifact->digest) ||
        !confit_snapshot_buffer_text(&build->seal, "\n"))
      goto failed;
  }
  if (build->total_bytes > CONFIT_LIMIT_SNAPSHOT_BYTES ||
      build->seal.size > CONFIT_LIMIT_SNAPSHOT_BYTES - build->total_bytes)
    goto failed;
  build->total_bytes += build->seal.size;
  confit_sha256_bytes(build->seal.bytes, build->seal.size, build->digest);
  return CONFIT_OK;

failed:
  return confit_snapshot_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                              kTooLarge);
}

static ConfitStatus confit_snapshot_build_prepare(
    const ConfitSnapshotPublishRequest *request,
    const ConfitAllocator *allocator, ConfitSnapshotBuild *build,
    ConfitDiagnostic *diagnostic) {
  ConfitSnapshotBuffer user_values;
  ConfitSnapshotBuffer resolved_values;
  ConfitSnapshotBuffer manifest;
  ConfitSnapshotBuffer provenance;
  size_t index;
  ConfitStatus status;
  confit_snapshot_buffer_init(&user_values, allocator);
  confit_snapshot_buffer_init(&resolved_values, allocator);
  confit_snapshot_buffer_init(&manifest, allocator);
  confit_snapshot_buffer_init(&provenance, allocator);
  status = confit_snapshot_make_user_values(request->resolution, allocator,
                                            &user_values, diagnostic);
  if (status != CONFIT_OK) goto fail;
  confit_snapshot_buffer_init(&resolved_values, allocator);
  status = confit_snapshot_make_resolved_json(request->resolution,
                                              &resolved_values, diagnostic);
  if (status != CONFIT_OK) goto fail;
  status = confit_snapshot_make_manifest(request, allocator, &manifest,
                                         diagnostic);
  if (status != CONFIT_OK) goto fail;
  status = confit_snapshot_make_provenance(allocator, &provenance, diagnostic);
  if (status != CONFIT_OK) goto fail;
  status = confit_snapshot_add_artifact(
      build, "inputs", "inputs.manifest", manifest.bytes, manifest.size, 1,
      &manifest, diagnostic);
  if (status != CONFIT_OK) goto fail;
  status = confit_snapshot_add_artifact(
      build, "provenance", "provenance.json", provenance.bytes,
      provenance.size, 1, &provenance, diagnostic);
  if (status != CONFIT_OK) goto fail;
  status = confit_snapshot_add_artifact(
      build, "resolved-values", "resolved-values.json",
      resolved_values.bytes, resolved_values.size,
      request->resolved_values_printable, &resolved_values, diagnostic);
  if (status != CONFIT_OK) goto fail;
  status = confit_snapshot_add_artifact(
      build, "user-values", "user-values.toml", user_values.bytes,
      user_values.size, 1, &user_values, diagnostic);
  if (status != CONFIT_OK) goto fail;
  if (request->optional_artifact_count >
          CONFIT_LIMIT_SNAPSHOT_ARTIFACTS - build->artifact_count ||
      (request->optional_artifact_count != 0U &&
       request->optional_artifacts == 0)) {
    status = confit_snapshot_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                                  kTooLarge);
    goto fail;
  }
  for (index = 0U; index < request->optional_artifact_count; ++index) {
    const ConfitSnapshotArtifactSpec *optional =
        &request->optional_artifacts[index];
    if (optional->name == 0 || optional->role == 0 ||
        confit_snapshot_is_core_name(optional->name)) {
      status = confit_snapshot_fail(diagnostic, CONFIT_ERR_VALIDATION,
                                    optional->name, kInvalidArtifact);
      goto fail;
    }
    status = confit_snapshot_add_artifact(
        build, optional->role, optional->name,
        (const unsigned char *)optional->bytes, optional->size,
        optional->printable, 0, diagnostic);
    if (status != CONFIT_OK) goto fail;
  }
  confit_snapshot_sort_artifacts(build);
  return confit_snapshot_make_seal(build, diagnostic);

fail:
  confit_snapshot_buffer_destroy(&provenance);
  confit_snapshot_buffer_destroy(&manifest);
  confit_snapshot_buffer_destroy(&resolved_values);
  confit_snapshot_buffer_destroy(&user_values);
  return status;
}

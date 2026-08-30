#include "confit/migration.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/digest.h"
#include "confit/limits.h"
#include "confit/snapshot.h"
#include "migration_internal.h"
#include "snapshot_internal.h"

typedef struct ConfitMigrationBuffer {
  unsigned char *bytes;
  size_t size;
  size_t capacity;
  ConfitAllocator allocator;
} ConfitMigrationBuffer;

typedef struct ConfitCatalogSummaryEntry {
  char symbol[CONFIT_LIMIT_SYMBOL_BYTES + 1U];
  ConfitValueKind kind;
  char prompt_digest[65];
  char help_digest[65];
  char default_digest[65];
  char domain_digest[65];
  char dependency_digest[65];
  const char *prompt;
} ConfitCatalogSummaryEntry;

typedef struct ConfitMigrationChangeRecord {
  const char *symbol;
  unsigned changes;
  ConfitValueKind current_kind;
  const char *current_prompt;
} ConfitMigrationChangeRecord;

struct ConfitMigrationReview {
  ConfitAllocator allocator;
  const ConfitCatalog *current_catalog;
  ConfitCatalogSummaryEntry *previous;
  size_t previous_count;
  ConfitCatalogSummaryEntry *current;
  size_t current_count;
  ConfitMigrationChangeRecord *changes;
  size_t change_count;
  int has_semantic_changes;
};

static const char kSummaryHeader[] = "confit-catalog-summary-v1\n";
static const char kInvalidArgument[] = "invalid configuration review argument";
static const char kInvalidAllocator[] = "allocator capability is incomplete";
static const char kInvalidSummary[] = "selected catalog summary is invalid";
static const char kTooLarge[] = "selected catalog summary exceeds a public limit";
static const char kOutOfMemory[] = "failed to allocate a bounded configuration review";
static const char kInternal[] = "catalog summary invariant failed";

static ConfitStatus confit_migration_fail(ConfitDiagnostic *diagnostic,
                                           ConfitStatus status,
                                           const char *path,
                                           const char *message) {
  confit_diagnostic_set(diagnostic, status, path, 0U, 0U, message);
  return status;
}

static int confit_migration_allocator(const ConfitAllocator *requested,
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

static void confit_migration_buffer_init(ConfitMigrationBuffer *buffer,
                                         const ConfitAllocator *allocator) {
  memset(buffer, 0, sizeof(*buffer));
  buffer->allocator = *allocator;
}

static void confit_migration_buffer_destroy(ConfitMigrationBuffer *buffer) {
  if (buffer == 0) return;
  if (buffer->bytes != 0)
    buffer->allocator.deallocate(buffer->allocator.context, buffer->bytes);
  memset(buffer, 0, sizeof(*buffer));
}

static int confit_migration_buffer_reserve(ConfitMigrationBuffer *buffer,
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

static int confit_migration_buffer_append(ConfitMigrationBuffer *buffer,
                                          const void *bytes, size_t size) {
  if ((bytes == 0 && size != 0U) ||
      !confit_migration_buffer_reserve(buffer, size))
    return 0;
  if (size != 0U) memcpy(buffer->bytes + buffer->size, bytes, size);
  buffer->size += size;
  buffer->bytes[buffer->size] = '\0';
  return 1;
}

static int confit_migration_buffer_text(ConfitMigrationBuffer *buffer,
                                        const char *text) {
  return text != 0 &&
         confit_migration_buffer_append(buffer, text, strlen(text));
}

static const char *confit_migration_kind_name(ConfitValueKind kind) {
  switch (kind) {
  case CONFIT_VALUE_BOOL:
    return "bool";
  case CONFIT_VALUE_INT:
    return "int";
  case CONFIT_VALUE_HEX:
    return "hex";
  case CONFIT_VALUE_STRING:
    return "string";
  case CONFIT_VALUE_ENUM:
    return "enum";
  case CONFIT_VALUE_INVALID:
  default:
    return 0;
  }
}

static ConfitValueKind confit_migration_parse_kind(const char *text) {
  if (strcmp(text, "bool") == 0) return CONFIT_VALUE_BOOL;
  if (strcmp(text, "int") == 0) return CONFIT_VALUE_INT;
  if (strcmp(text, "hex") == 0) return CONFIT_VALUE_HEX;
  if (strcmp(text, "string") == 0) return CONFIT_VALUE_STRING;
  if (strcmp(text, "enum") == 0) return CONFIT_VALUE_ENUM;
  return CONFIT_VALUE_INVALID;
}

static int confit_migration_digest_is_valid(const char *text) {
  size_t index;
  if (text == 0 || strlen(text) != 64U) return 0;
  for (index = 0U; index < 64U; ++index)
    if (!((text[index] >= '0' && text[index] <= '9') ||
          (text[index] >= 'a' && text[index] <= 'f')))
      return 0;
  return 1;
}

static ConfitStatus confit_migration_value_digest(
    const ConfitValue *value, const ConfitAllocator *allocator,
    char digest[65], ConfitDiagnostic *diagnostic) {
  char canonical[CONFIT_LIMIT_STRING_BYTES + 64U];
  size_t size = 0U;
  ConfitStatus status;
  (void)allocator;
  status = confit_value_format_canonical(value, canonical, sizeof(canonical),
                                         &size, diagnostic);
  if (status == CONFIT_OK) confit_sha256_bytes(canonical, size, digest);
  return status;
}

static ConfitStatus confit_migration_domain_digest(
    const ConfitConfigView *view, const ConfitAllocator *allocator,
    char digest[65], ConfitDiagnostic *diagnostic) {
  ConfitMigrationBuffer buffer;
  size_t index;
  ConfitStatus status = CONFIT_OK;
  confit_migration_buffer_init(&buffer, allocator);
  if (view->has_range) {
    char minimum[65];
    char maximum[65];
    status = confit_migration_value_digest(view->range_minimum, allocator,
                                           minimum, diagnostic);
    if (status == CONFIT_OK)
      status = confit_migration_value_digest(view->range_maximum, allocator,
                                             maximum, diagnostic);
    if (status == CONFIT_OK &&
        (!confit_migration_buffer_text(&buffer, "range:") ||
         !confit_migration_buffer_text(&buffer, minimum) ||
         !confit_migration_buffer_text(&buffer, ":") ||
         !confit_migration_buffer_text(&buffer, maximum)))
      status = confit_migration_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                     kOutOfMemory);
  } else if (view->kind == CONFIT_VALUE_ENUM) {
    if (!confit_migration_buffer_text(&buffer, "enum:"))
      status = confit_migration_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                     kOutOfMemory);
    for (index = 0U; status == CONFIT_OK && index < view->enum_value_count;
         ++index) {
      char atom[65];
      confit_sha256_bytes(view->enum_values[index],
                          strlen(view->enum_values[index]), atom);
      if (!confit_migration_buffer_text(&buffer, atom) ||
          !confit_migration_buffer_text(&buffer, ":"))
        status = confit_migration_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                       kOutOfMemory);
    }
  } else if (!confit_migration_buffer_text(&buffer, "none")) {
    status = confit_migration_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                   kOutOfMemory);
  }
  if (status == CONFIT_OK)
    confit_sha256_bytes(buffer.bytes, buffer.size, digest);
  confit_migration_buffer_destroy(&buffer);
  return status;
}

static ConfitStatus confit_migration_entry_from_view(
    const ConfitConfigView *view, const ConfitAllocator *allocator,
    ConfitCatalogSummaryEntry *entry, ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  if (view == 0 || entry == 0 || view->symbol == 0 || view->prompt == 0 ||
      view->help == 0 || view->default_value == 0 ||
      strlen(view->symbol) > CONFIT_LIMIT_SYMBOL_BYTES)
    return confit_migration_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                 kInternal);
  memset(entry, 0, sizeof(*entry));
  memcpy(entry->symbol, view->symbol, strlen(view->symbol) + 1U);
  entry->kind = view->kind;
  entry->prompt = view->prompt;
  confit_sha256_bytes(view->prompt, strlen(view->prompt), entry->prompt_digest);
  confit_sha256_bytes(view->help, strlen(view->help), entry->help_digest);
  if (view->choice_group != 0) {
    char choice[CONFIT_LIMIT_CHOICE_GROUP_BYTES + 8U];
    const int written = snprintf(choice, sizeof(choice), "choice:%s",
                                 view->choice_group);
    if (written <= 0 || (size_t)written >= sizeof(choice))
      return confit_migration_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                   kInternal);
    confit_sha256_bytes(choice, (size_t)written, entry->dependency_digest);
  } else {
    confit_sha256_bytes(
        view->dependency_text != 0 ? view->dependency_text : "",
        view->dependency_text != 0 ? strlen(view->dependency_text) : 0U,
        entry->dependency_digest);
  }
  status = confit_migration_value_digest(view->default_value, allocator,
                                         entry->default_digest, diagnostic);
  if (status == CONFIT_OK)
    status = confit_migration_domain_digest(view, allocator,
                                            entry->domain_digest, diagnostic);
  return status;
}

static int confit_migration_entry_compare(const void *left,
                                          const void *right) {
  const ConfitCatalogSummaryEntry *a =
      (const ConfitCatalogSummaryEntry *)left;
  const ConfitCatalogSummaryEntry *b =
      (const ConfitCatalogSummaryEntry *)right;
  return strcmp(a->symbol, b->symbol);
}

static ConfitStatus confit_migration_build_entries(
    const ConfitCatalog *catalog, const ConfitAllocator *allocator,
    ConfitCatalogSummaryEntry **out_entries, size_t *out_count,
    ConfitDiagnostic *diagnostic) {
  ConfitCatalogSummaryEntry *entries = 0;
  const size_t count = confit_catalog_config_count(catalog);
  size_t index;
  ConfitStatus status = CONFIT_OK;
  *out_entries = 0;
  *out_count = 0U;
  if (count > CONFIT_LIMIT_CONFIG_SYMBOLS ||
      count > SIZE_MAX / sizeof(*entries))
    return confit_migration_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                                 kTooLarge);
  if (count != 0U) {
    entries = (ConfitCatalogSummaryEntry *)allocator->allocate(
        allocator->context, count * sizeof(*entries));
    if (entries == 0)
      return confit_migration_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                   kOutOfMemory);
  }
  for (index = 0U; index < count; ++index) {
    ConfitConfigView view;
    if (!confit_catalog_config_at(catalog, index, &view)) {
      status = confit_migration_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                     kInternal);
      break;
    }
    status = confit_migration_entry_from_view(&view, allocator,
                                              &entries[index], diagnostic);
    if (status != CONFIT_OK) break;
  }
  if (status == CONFIT_OK && count > 1U)
    qsort(entries, count, sizeof(*entries), confit_migration_entry_compare);
  if (status != CONFIT_OK) {
    if (entries != 0)
      allocator->deallocate(allocator->context, entries);
    return status;
  }
  *out_entries = entries;
  *out_count = count;
  return CONFIT_OK;
}

ConfitStatus confit_migration_catalog_summary_format(
    const ConfitCatalog *catalog, const ConfitAllocator *allocator,
    unsigned char **out_bytes, size_t *out_size,
    ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitCatalogSummaryEntry *entries = 0;
  ConfitMigrationBuffer buffer;
  size_t count = 0U;
  size_t index;
  ConfitStatus status;
  if (out_bytes == 0 || out_size == 0 || catalog == 0)
    return confit_migration_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                 kInvalidArgument);
  *out_bytes = 0;
  *out_size = 0U;
  if (!confit_migration_allocator(allocator, &resolved))
    return confit_migration_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                 kInvalidAllocator);
  confit_migration_buffer_init(&buffer, &resolved);
  status = confit_migration_build_entries(catalog, &resolved, &entries,
                                          &count, diagnostic);
  if (status != CONFIT_OK) return status;
  if (!confit_migration_buffer_text(&buffer, kSummaryHeader))
    status = confit_migration_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                   kOutOfMemory);
  for (index = 0U; status == CONFIT_OK && index < count; ++index) {
    const ConfitCatalogSummaryEntry *entry = &entries[index];
    const char *kind = confit_migration_kind_name(entry->kind);
    if (kind == 0 || !confit_migration_buffer_text(&buffer, entry->symbol) ||
        !confit_migration_buffer_text(&buffer, "\t") ||
        !confit_migration_buffer_text(&buffer, kind) ||
        !confit_migration_buffer_text(&buffer, "\t") ||
        !confit_migration_buffer_text(&buffer, entry->prompt_digest) ||
        !confit_migration_buffer_text(&buffer, "\t") ||
        !confit_migration_buffer_text(&buffer, entry->help_digest) ||
        !confit_migration_buffer_text(&buffer, "\t") ||
        !confit_migration_buffer_text(&buffer, entry->default_digest) ||
        !confit_migration_buffer_text(&buffer, "\t") ||
        !confit_migration_buffer_text(&buffer, entry->domain_digest) ||
        !confit_migration_buffer_text(&buffer, "\t") ||
        !confit_migration_buffer_text(&buffer, entry->dependency_digest) ||
        !confit_migration_buffer_text(&buffer, "\n"))
      status = confit_migration_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                                     kTooLarge);
  }
  if (entries != 0)
    resolved.deallocate(resolved.context, entries);
  if (status != CONFIT_OK) {
    confit_migration_buffer_destroy(&buffer);
    return status;
  }
  *out_bytes = buffer.bytes;
  *out_size = buffer.size;
  return CONFIT_OK;
}

static int confit_migration_split_line(char *line, char **fields,
                                       size_t field_count) {
  size_t count = 1U;
  char *cursor = line;
  if (line == 0 || fields == 0 || field_count == 0U) return 0;
  fields[0] = line;
  while (*cursor != '\0') {
    if (*cursor == '\t') {
      if (count >= field_count) return 0;
      *cursor = '\0';
      fields[count++] = cursor + 1U;
    }
    cursor += 1U;
  }
  return count == field_count;
}

static ConfitStatus confit_migration_parse_summary(
    ConfitHostBuffer *buffer, const ConfitAllocator *allocator,
    ConfitCatalogSummaryEntry **out_entries, size_t *out_count,
    ConfitDiagnostic *diagnostic) {
  ConfitCatalogSummaryEntry *entries = 0;
  char *cursor;
  char *end;
  size_t line_count = 0U;
  size_t count = 0U;
  size_t index;
  if (buffer == 0 || out_entries == 0 || out_count == 0 ||
      buffer->size < sizeof(kSummaryHeader) - 1U ||
      memcmp(buffer->bytes, kSummaryHeader, sizeof(kSummaryHeader) - 1U) != 0)
    return confit_migration_fail(diagnostic, CONFIT_ERR_STALE,
                                 "catalog.summary", kInvalidSummary);
  for (index = 0U; index < buffer->size; ++index) {
    if (buffer->bytes[index] == '\0')
      return confit_migration_fail(diagnostic, CONFIT_ERR_STALE,
                                   "catalog.summary", kInvalidSummary);
    if (index >= sizeof(kSummaryHeader) - 1U && buffer->bytes[index] == '\n')
      line_count += 1U;
  }
  if (line_count > CONFIT_LIMIT_CONFIG_SYMBOLS ||
      line_count > SIZE_MAX / sizeof(*entries))
    return confit_migration_fail(diagnostic, CONFIT_ERR_STALE,
                                 "catalog.summary", kTooLarge);
  if (line_count != 0U) {
    entries = (ConfitCatalogSummaryEntry *)allocator->allocate(
        allocator->context, line_count * sizeof(*entries));
    if (entries == 0)
      return confit_migration_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                   kOutOfMemory);
  }
  cursor = (char *)buffer->bytes + sizeof(kSummaryHeader) - 1U;
  end = (char *)buffer->bytes + buffer->size;
  while (cursor < end) {
    char *fields[7];
    char *newline = (char *)memchr(cursor, '\n', (size_t)(end - cursor));
    ConfitCatalogSummaryEntry *entry;
    if (newline == 0 || newline == cursor || count >= line_count) goto invalid;
    *newline = '\0';
    if (!confit_migration_split_line(cursor, fields, 7U) ||
        !confit_symbol_is_valid(fields[0]) ||
        confit_migration_parse_kind(fields[1]) == CONFIT_VALUE_INVALID ||
        !confit_migration_digest_is_valid(fields[2]) ||
        !confit_migration_digest_is_valid(fields[3]) ||
        !confit_migration_digest_is_valid(fields[4]) ||
        !confit_migration_digest_is_valid(fields[5]) ||
        !confit_migration_digest_is_valid(fields[6]) ||
        (count > 0U && strcmp(entries[count - 1U].symbol, fields[0]) >= 0))
      goto invalid;
    entry = &entries[count++];
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->symbol, fields[0], strlen(fields[0]) + 1U);
    entry->kind = confit_migration_parse_kind(fields[1]);
    memcpy(entry->prompt_digest, fields[2], 65U);
    memcpy(entry->help_digest, fields[3], 65U);
    memcpy(entry->default_digest, fields[4], 65U);
    memcpy(entry->domain_digest, fields[5], 65U);
    memcpy(entry->dependency_digest, fields[6], 65U);
    cursor = newline + 1U;
  }
  if (count != line_count) goto invalid;
  *out_entries = entries;
  *out_count = count;
  return CONFIT_OK;

invalid:
  if (entries != 0) allocator->deallocate(allocator->context, entries);
  return confit_migration_fail(diagnostic, CONFIT_ERR_STALE,
                               "catalog.summary", kInvalidSummary);
}

static unsigned confit_migration_compare_entries(
    const ConfitCatalogSummaryEntry *previous,
    const ConfitCatalogSummaryEntry *current) {
  unsigned changes = CONFIT_MIGRATION_CHANGE_NONE;
  if (previous->kind != current->kind) changes |= CONFIT_MIGRATION_CHANGE_TYPE;
  if (strcmp(previous->domain_digest, current->domain_digest) != 0)
    changes |= CONFIT_MIGRATION_CHANGE_DOMAIN;
  if (strcmp(previous->default_digest, current->default_digest) != 0)
    changes |= CONFIT_MIGRATION_CHANGE_DEFAULT;
  if (strcmp(previous->dependency_digest, current->dependency_digest) != 0)
    changes |= CONFIT_MIGRATION_CHANGE_DEPENDENCY;
  if (strcmp(previous->prompt_digest, current->prompt_digest) != 0)
    changes |= CONFIT_MIGRATION_CHANGE_PROMPT;
  if (strcmp(previous->help_digest, current->help_digest) != 0)
    changes |= CONFIT_MIGRATION_CHANGE_HELP;
  return changes;
}

ConfitStatus confit_migration_review_selected(
    ConfitHostRoot *output_root, const ConfitCatalog *current_catalog,
    const ConfitAllocator *allocator, ConfitMigrationReview **out_review,
    ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitMigrationReview *review = 0;
  ConfitHostBuffer summary;
  char selected_digest[CONFIT_SNAPSHOT_DIGEST_TEXT_BYTES + 1U];
  size_t previous_index = 0U;
  size_t current_index = 0U;
  size_t maximum_changes;
  ConfitStatus status;
  confit_host_buffer_init(&summary);
  if (output_root == 0 || current_catalog == 0 || out_review == 0)
    return confit_migration_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                 kInvalidArgument);
  *out_review = 0;
  if (!confit_migration_allocator(allocator, &resolved))
    return confit_migration_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                 kInvalidAllocator);
  status = confit_snapshot_read_selected_artifact(
      output_root, "catalog.summary", &resolved, &summary, selected_digest,
      diagnostic);
  if (status != CONFIT_OK) return status;
  review = (ConfitMigrationReview *)resolved.allocate(resolved.context,
                                                       sizeof(*review));
  if (review == 0) {
    confit_host_buffer_destroy(&summary);
    return confit_migration_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                 kOutOfMemory);
  }
  memset(review, 0, sizeof(*review));
  review->allocator = resolved;
  review->current_catalog = current_catalog;
  status = confit_migration_parse_summary(
      &summary, &resolved, &review->previous, &review->previous_count,
      diagnostic);
  confit_host_buffer_destroy(&summary);
  if (status != CONFIT_OK) goto fail;
  status = confit_migration_build_entries(
      current_catalog, &resolved, &review->current, &review->current_count,
      diagnostic);
  if (status != CONFIT_OK) goto fail;
  if (review->previous_count > SIZE_MAX - review->current_count ||
      (maximum_changes = review->previous_count + review->current_count) >
          SIZE_MAX / sizeof(*review->changes)) {
    status = confit_migration_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                                   kTooLarge);
    goto fail;
  }
  if (maximum_changes != 0U) {
    review->changes = (ConfitMigrationChangeRecord *)resolved.allocate(
        resolved.context, maximum_changes * sizeof(*review->changes));
    if (review->changes == 0) {
      status = confit_migration_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                     kOutOfMemory);
      goto fail;
    }
  }
  while (previous_index < review->previous_count ||
         current_index < review->current_count) {
    ConfitMigrationChangeRecord change;
    int relation;
    memset(&change, 0, sizeof(change));
    if (previous_index == review->previous_count)
      relation = 1;
    else if (current_index == review->current_count)
      relation = -1;
    else
      relation = strcmp(review->previous[previous_index].symbol,
                        review->current[current_index].symbol);
    if (relation < 0) {
      change.symbol = review->previous[previous_index].symbol;
      change.changes = CONFIT_MIGRATION_CHANGE_REMOVED;
      previous_index += 1U;
    } else if (relation > 0) {
      change.symbol = review->current[current_index].symbol;
      change.changes = CONFIT_MIGRATION_CHANGE_NEW;
      change.current_kind = review->current[current_index].kind;
      change.current_prompt = review->current[current_index].prompt;
      current_index += 1U;
    } else {
      change.symbol = review->current[current_index].symbol;
      change.changes = confit_migration_compare_entries(
          &review->previous[previous_index], &review->current[current_index]);
      change.current_kind = review->current[current_index].kind;
      change.current_prompt = review->current[current_index].prompt;
      previous_index += 1U;
      current_index += 1U;
    }
    if (change.changes != CONFIT_MIGRATION_CHANGE_NONE) {
      const unsigned semantic =
          CONFIT_MIGRATION_CHANGE_REMOVED | CONFIT_MIGRATION_CHANGE_TYPE |
          CONFIT_MIGRATION_CHANGE_DOMAIN | CONFIT_MIGRATION_CHANGE_DEFAULT |
          CONFIT_MIGRATION_CHANGE_DEPENDENCY;
      review->changes[review->change_count++] = change;
      if ((change.changes & semantic) != 0U)
        review->has_semantic_changes = 1;
    }
  }
  *out_review = review;
  return CONFIT_OK;

fail:
  confit_migration_review_destroy(review);
  return status;
}

void confit_migration_review_destroy(ConfitMigrationReview *review) {
  ConfitAllocator allocator;
  if (review == 0) return;
  allocator = review->allocator;
  if (review->changes != 0)
    allocator.deallocate(allocator.context, review->changes);
  if (review->current != 0)
    allocator.deallocate(allocator.context, review->current);
  if (review->previous != 0)
    allocator.deallocate(allocator.context, review->previous);
  memset(review, 0, sizeof(*review));
  allocator.deallocate(allocator.context, review);
}

size_t confit_migration_review_change_count(
    const ConfitMigrationReview *review) {
  return review != 0 ? review->change_count : 0U;
}

int confit_migration_review_change_at(
    const ConfitMigrationReview *review, size_t lexical_index,
    ConfitMigrationChangeView *out_view) {
  const ConfitMigrationChangeRecord *record;
  if (review == 0 || out_view == 0 || lexical_index >= review->change_count)
    return 0;
  record = &review->changes[lexical_index];
  out_view->symbol = record->symbol;
  out_view->changes = record->changes;
  out_view->current_kind = record->current_kind;
  out_view->current_prompt = record->current_prompt;
  return 1;
}

int confit_migration_review_has_semantic_changes(
    const ConfitMigrationReview *review) {
  return review != 0 && review->has_semantic_changes;
}

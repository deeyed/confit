#include "confit/resolver.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "confit/limits.h"
#include "expression_internal.h"

struct ConfitResolution {
  ConfitAllocator allocator;
  const ConfitCatalog *catalog;
  ConfitResolvedValue *values;
  size_t *lexical_order;
  size_t value_count;
  ConfitReasonNode *reasons;
  size_t reason_count;
  size_t reason_capacity;
};

typedef struct ConfitResolveWork {
  ConfitAllocator allocator;
  ConfitValue *candidates;
  ConfitValueOrigin *origins;
  unsigned char *explicit_assignments;
  size_t *sort_scratch;
  size_t value_count;
} ConfitResolveWork;

static const char kInvalidArgument[] = "invalid resolver argument";
static const char kInvalidAllocator[] = "allocator capability is incomplete";
static const char kWrongPlan[] = "dependency plan does not belong to the catalog";
static const char kTooManyAssignments[] = "user assignment count exceeds the catalog";
static const char kUnknownAssignment[] = "user assignment names an unknown symbol";
static const char kDuplicateAssignment[] = "user assignment is duplicated";
static const char kInvalidAssignment[] = "user assignment value is malformed";
static const char kWrongType[] = "user assignment has the wrong value type";
static const char kOutsideRange[] = "user assignment is outside the declared range";
static const char kOutsideDomain[] = "user assignment is outside the enum domain";
static const char kUnavailableValue[] = "unavailable option has a non-default user value";
static const char kOutOfMemory[] = "failed to allocate configuration resolution";
static const char kInternalInvariant[] = "configuration resolver invariant is invalid";
static const char kDependencyFalse[] = "dependency is false";
static const char kCanonicalBuffer[] = "canonical resolution buffer is too small";

static ConfitStatus confit_resolver_fail(ConfitDiagnostic *diagnostic,
                                         ConfitStatus status,
                                         const ConfitConfigView *view,
                                         const char *message) {
  confit_diagnostic_set(diagnostic, status,
                        view != 0 ? view->declaration.path : 0,
                        view != 0 ? view->declaration.line : 0U,
                        view != 0 ? view->declaration.column : 0U, message);
  return status;
}

static int confit_resolver_allocator(const ConfitAllocator *requested,
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

static int confit_resolver_size_multiply(size_t left, size_t right,
                                         size_t *out) {
  if (out == 0 || (left != 0U && right > SIZE_MAX / left)) return 0;
  *out = left * right;
  return 1;
}

static int confit_resolver_size_add(size_t left, size_t right, size_t *out) {
  if (out == 0 || left > SIZE_MAX - right) return 0;
  *out = left + right;
  return 1;
}

static void confit_resolve_work_destroy(ConfitResolveWork *work) {
  size_t index;
  if (work == 0 || !confit_allocator_is_valid(&work->allocator)) return;
  if (work->candidates != 0) {
    for (index = work->value_count; index > 0U; --index)
      confit_value_destroy(&work->candidates[index - 1U]);
    work->allocator.deallocate(work->allocator.context, work->candidates);
  }
  if (work->origins != 0)
    work->allocator.deallocate(work->allocator.context, work->origins);
  if (work->explicit_assignments != 0)
    work->allocator.deallocate(work->allocator.context,
                               work->explicit_assignments);
  if (work->sort_scratch != 0)
    work->allocator.deallocate(work->allocator.context, work->sort_scratch);
  memset(work, 0, sizeof(*work));
}

void confit_resolution_destroy(ConfitResolution *resolution) {
  ConfitAllocator allocator;
  size_t index;
  if (resolution == 0) return;
  allocator = resolution->allocator;
  if (resolution->values != 0) {
    for (index = resolution->value_count; index > 0U; --index)
      confit_resolved_value_destroy(&resolution->values[index - 1U]);
    allocator.deallocate(allocator.context, resolution->values);
  }
  if (resolution->lexical_order != 0)
    allocator.deallocate(allocator.context, resolution->lexical_order);
  if (resolution->reasons != 0) {
    for (index = resolution->reason_count; index > 0U; --index)
      confit_reason_node_destroy(&resolution->reasons[index - 1U]);
    allocator.deallocate(allocator.context, resolution->reasons);
  }
  memset(resolution, 0, sizeof(*resolution));
  allocator.deallocate(allocator.context, resolution);
}

static int confit_catalog_symbol_less(const ConfitCatalog *catalog,
                                      size_t left, size_t right) {
  ConfitConfigView left_view;
  ConfitConfigView right_view;
  if (!confit_catalog_config_at(catalog, left, &left_view) ||
      !confit_catalog_config_at(catalog, right, &right_view))
    return 0;
  return strcmp(left_view.symbol, right_view.symbol) < 0;
}

static int confit_resolution_sort_indexes(const ConfitCatalog *catalog,
                                          size_t *indexes, size_t *scratch,
                                          size_t count) {
  size_t width;
  size_t *source = indexes;
  size_t *destination = scratch;
  if (count < 2U) return 1;
  for (width = 1U; width < count;) {
    size_t start;
    for (start = 0U; start < count; start += width * 2U) {
      const size_t middle = start + width < count ? start + width : count;
      const size_t end = middle + width < count ? middle + width : count;
      size_t left = start;
      size_t right = middle;
      size_t output = start;
      while (left < middle && right < end) {
        if (confit_catalog_symbol_less(catalog, source[right], source[left]))
          destination[output++] = source[right++];
        else
          destination[output++] = source[left++];
      }
      while (left < middle) destination[output++] = source[left++];
      while (right < end) destination[output++] = source[right++];
    }
    {
      size_t *swap = source;
      source = destination;
      destination = swap;
    }
    if (width > count / 2U) break;
    width *= 2U;
  }
  if (source != indexes) memcpy(indexes, source, count * sizeof(*indexes));
  return 1;
}

static int confit_resolution_find_catalog_index(
    const ConfitCatalog *catalog, const size_t *lexical_order, size_t count,
    const char *symbol, size_t *out_catalog_index) {
  size_t lower = 0U;
  size_t upper = count;
  while (lower < upper) {
    const size_t middle = lower + (upper - lower) / 2U;
    ConfitConfigView view;
    int comparison;
    if (!confit_catalog_config_at(catalog, lexical_order[middle], &view))
      return 0;
    comparison = strcmp(symbol, view.symbol);
    if (comparison == 0) {
      *out_catalog_index = lexical_order[middle];
      return 1;
    }
    if (comparison < 0)
      upper = middle;
    else
      lower = middle + 1U;
  }
  return 0;
}

static int confit_value_inside_range(const ConfitConfigView *view,
                                     const ConfitValue *value) {
  if (!view->has_range) return 1;
  if (view->kind == CONFIT_VALUE_INT)
    return value->data.integer >= view->range_minimum->data.integer &&
           value->data.integer <= view->range_maximum->data.integer;
  if (view->kind == CONFIT_VALUE_HEX)
    return value->data.hexadecimal >= view->range_minimum->data.hexadecimal &&
           value->data.hexadecimal <= view->range_maximum->data.hexadecimal;
  return 0;
}

static int confit_value_inside_enum(const ConfitConfigView *view,
                                    const ConfitValue *value) {
  size_t index;
  if (view->kind != CONFIT_VALUE_ENUM || value->data.text.data == 0) return 0;
  for (index = 0U; index < view->enum_value_count; ++index)
    if (strlen(view->enum_values[index]) == value->data.text.size &&
        memcmp(view->enum_values[index], value->data.text.data,
               value->data.text.size) == 0)
      return 1;
  return 0;
}

static ConfitStatus confit_resolver_copy_assignment(
    const ConfitConfigView *view, const ConfitValue *source,
    const ConfitAllocator *allocator, ConfitValue *destination,
    ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  if (source == 0 || source->kind != view->kind)
    return confit_resolver_fail(diagnostic, CONFIT_ERR_VALIDATION, view,
                                kWrongType);
  if (view->kind == CONFIT_VALUE_BOOL &&
      source->data.boolean != 0 && source->data.boolean != 1)
    return confit_resolver_fail(diagnostic, CONFIT_ERR_VALIDATION, view,
                                kInvalidAssignment);
  status = confit_value_copy(destination, source, allocator, diagnostic);
  if (status != CONFIT_OK)
    return confit_resolver_fail(diagnostic, status, view, kInvalidAssignment);
  if (!confit_value_inside_range(view, destination))
    return confit_resolver_fail(diagnostic, CONFIT_ERR_VALIDATION, view,
                                kOutsideRange);
  if (view->kind == CONFIT_VALUE_ENUM &&
      !confit_value_inside_enum(view, destination))
    return confit_resolver_fail(diagnostic, CONFIT_ERR_VALIDATION, view,
                                kOutsideDomain);
  return CONFIT_OK;
}

static ConfitStatus confit_resolution_reserve_reasons(
    ConfitResolution *resolution, size_t additional,
    ConfitDiagnostic *diagnostic) {
  ConfitReasonNode *replacement;
  size_t required;
  size_t maximum;
  size_t capacity;
  size_t bytes;
  if (!confit_resolver_size_add(resolution->reason_count, additional,
                                &required))
    return confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kOutOfMemory);
  if (!confit_resolver_size_multiply(
          resolution->value_count,
          CONFIT_LIMIT_DEPENDENCY_AST_NODES + 1U, &maximum) ||
      required > maximum)
    return confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kInternalInvariant);
  if (required <= resolution->reason_capacity) return CONFIT_OK;
  capacity = resolution->reason_capacity == 0U ? 16U
                                               : resolution->reason_capacity;
  while (capacity < required) {
    if (capacity > SIZE_MAX / 2U) {
      capacity = required;
      break;
    }
    capacity *= 2U;
  }
  if (!confit_resolver_size_multiply(capacity, sizeof(*replacement), &bytes))
    return confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kOutOfMemory);
  replacement = (ConfitReasonNode *)resolution->allocator.allocate(
      resolution->allocator.context, bytes);
  if (replacement == 0)
    return confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kOutOfMemory);
  if (resolution->reason_count != 0U)
    memcpy(replacement, resolution->reasons,
           resolution->reason_count * sizeof(*replacement));
  if (resolution->reasons != 0)
    resolution->allocator.deallocate(resolution->allocator.context,
                                     resolution->reasons);
  resolution->reasons = replacement;
  resolution->reason_capacity = capacity;
  return CONFIT_OK;
}

static ConfitStatus confit_resolution_copy_evaluation(
    ConfitResolution *resolution,
    const ConfitDependencyEvaluation *evaluation,
    const ConfitConfigView *owner, size_t *out_root,
    ConfitDiagnostic *diagnostic) {
  const size_t count = confit_dependency_evaluation_reason_count(evaluation);
  const size_t base = resolution->reason_count;
  size_t index;
  ConfitStatus status;
  if (count == 0U || count > CONFIT_LIMIT_DEPENDENCY_AST_NODES)
    return confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, owner,
                                kInternalInvariant);
  status = confit_resolution_reserve_reasons(resolution, count, diagnostic);
  if (status != CONFIT_OK) return status;
  for (index = 0U; index < count; ++index) {
    ConfitDependencyReasonView view;
    size_t children[CONFIT_REASON_CHILD_LIMIT];
    size_t child;
    if (!confit_dependency_evaluation_reason_at(evaluation, index, &view))
      return confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, owner,
                                  kInternalInvariant);
    for (child = 0U; child < view.child_count; ++child) {
      if (view.children[child] >= index)
        return confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, owner,
                                    kInternalInvariant);
      children[child] = base + view.children[child];
    }
    confit_reason_node_init(&resolution->reasons[resolution->reason_count]);
    status = confit_reason_node_set(
        &resolution->reasons[resolution->reason_count], view.kind, view.result,
        view.subject_symbol, 0, view.detail, children, view.child_count,
        &resolution->allocator, diagnostic);
    if (status != CONFIT_OK) return status;
    ++resolution->reason_count;
  }
  index = confit_dependency_evaluation_reason_root(evaluation);
  if (index >= count)
    return confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, owner,
                                kInternalInvariant);
  *out_root = base + index;
  return CONFIT_OK;
}

static ConfitStatus confit_resolution_wrap_unavailable(
    ConfitResolution *resolution, const ConfitConfigView *owner,
    size_t expression_root, size_t *out_root,
    ConfitDiagnostic *diagnostic) {
  ConfitStatus status =
      confit_resolution_reserve_reasons(resolution, 1U, diagnostic);
  if (status != CONFIT_OK) return status;
  confit_reason_node_init(&resolution->reasons[resolution->reason_count]);
  status = confit_reason_node_set(
      &resolution->reasons[resolution->reason_count],
      CONFIT_REASON_UNAVAILABLE, 0, owner->symbol, 0, kDependencyFalse,
      &expression_root, 1U, &resolution->allocator, diagnostic);
  if (status != CONFIT_OK) return status;
  *out_root = resolution->reason_count++;
  return CONFIT_OK;
}

ConfitStatus confit_resolve(
    const ConfitCatalog *catalog, const ConfitDependencyPlan *plan,
    const ConfitAssignment *assignments, size_t assignment_count,
    const ConfitAllocator *allocator, ConfitResolution **out_resolution,
    ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved_allocator;
  ConfitResolution *resolution = 0;
  ConfitResolveWork work;
  size_t count;
  size_t bytes;
  size_t index;
  ConfitStatus status = CONFIT_OK;
  memset(&work, 0, sizeof(work));
  if (catalog == 0 || plan == 0 || out_resolution == 0 ||
      (assignment_count != 0U && assignments == 0)) {
    return confit_resolver_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                kInvalidArgument);
  }
  *out_resolution = 0;
  if (!confit_dependency_plan_matches_catalog(plan, catalog))
    return confit_resolver_fail(diagnostic, CONFIT_ERR_USAGE, 0, kWrongPlan);
  if (!confit_resolver_allocator(allocator, &resolved_allocator))
    return confit_resolver_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                kInvalidAllocator);
  count = confit_catalog_config_count(catalog);
  if (count > CONFIT_LIMIT_CONFIG_SYMBOLS ||
      confit_dependency_plan_config_count(plan) != count)
    return confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kInternalInvariant);
  if (assignment_count > count)
    return confit_resolver_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                                kTooManyAssignments);
  resolution = (ConfitResolution *)resolved_allocator.allocate(
      resolved_allocator.context, sizeof(*resolution));
  if (resolution == 0)
    return confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                kOutOfMemory);
  memset(resolution, 0, sizeof(*resolution));
  resolution->allocator = resolved_allocator;
  resolution->catalog = catalog;
  work.allocator = resolved_allocator;
  if (count != 0U) {
    if (!confit_resolver_size_multiply(count, sizeof(*resolution->values),
                                       &bytes))
      status = CONFIT_ERR_INTERNAL;
    else
      resolution->values = (ConfitResolvedValue *)resolved_allocator.allocate(
          resolved_allocator.context, bytes);
    if (status == CONFIT_OK && resolution->values == 0) status = CONFIT_ERR_INTERNAL;
    if (resolution->values != 0) {
      memset(resolution->values, 0, count * sizeof(*resolution->values));
      resolution->value_count = count;
    }
    if (status == CONFIT_OK &&
        !confit_resolver_size_multiply(count, sizeof(*resolution->lexical_order),
                                       &bytes))
      status = CONFIT_ERR_INTERNAL;
    else if (status == CONFIT_OK)
      resolution->lexical_order = (size_t *)resolved_allocator.allocate(
          resolved_allocator.context, bytes);
    if (status == CONFIT_OK && resolution->lexical_order == 0)
      status = CONFIT_ERR_INTERNAL;
    if (status == CONFIT_OK &&
        !confit_resolver_size_multiply(count, sizeof(*work.candidates), &bytes))
      status = CONFIT_ERR_INTERNAL;
    else if (status == CONFIT_OK)
      work.candidates = (ConfitValue *)resolved_allocator.allocate(
          resolved_allocator.context, bytes);
    if (status == CONFIT_OK && work.candidates == 0) status = CONFIT_ERR_INTERNAL;
    if (work.candidates != 0) {
      memset(work.candidates, 0, count * sizeof(*work.candidates));
      work.value_count = count;
    }
    if (status == CONFIT_OK &&
        !confit_resolver_size_multiply(count, sizeof(*work.origins), &bytes))
      status = CONFIT_ERR_INTERNAL;
    else if (status == CONFIT_OK)
      work.origins = (ConfitValueOrigin *)resolved_allocator.allocate(
          resolved_allocator.context, bytes);
    if (status == CONFIT_OK && work.origins == 0) status = CONFIT_ERR_INTERNAL;
    if (status == CONFIT_OK)
      work.explicit_assignments = (unsigned char *)resolved_allocator.allocate(
          resolved_allocator.context, count);
    if (status == CONFIT_OK && work.explicit_assignments == 0)
      status = CONFIT_ERR_INTERNAL;
    if (status == CONFIT_OK &&
        !confit_resolver_size_multiply(count, sizeof(*work.sort_scratch),
                                       &bytes))
      status = CONFIT_ERR_INTERNAL;
    else if (status == CONFIT_OK)
      work.sort_scratch = (size_t *)resolved_allocator.allocate(
          resolved_allocator.context, bytes);
    if (status == CONFIT_OK && work.sort_scratch == 0) status = CONFIT_ERR_INTERNAL;
    if (status != CONFIT_OK) {
      confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, 0, kOutOfMemory);
      goto fail;
    }
    memset(work.explicit_assignments, 0, count);
    for (index = 0U; index < count; ++index) {
      ConfitConfigView view;
      confit_resolved_value_init(&resolution->values[index]);
      confit_value_init(&work.candidates[index]);
      resolution->lexical_order[index] = index;
      work.origins[index] = CONFIT_ORIGIN_DEFAULT;
      if (!confit_catalog_config_at(catalog, index, &view)) {
        status = confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                      kInternalInvariant);
        goto fail;
      }
      status = confit_value_copy(&work.candidates[index], view.default_value,
                                 &resolved_allocator, diagnostic);
      if (status != CONFIT_OK) {
        status = confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, &view,
                                      kInternalInvariant);
        goto fail;
      }
    }
    if (!confit_resolution_sort_indexes(catalog, resolution->lexical_order,
                                        work.sort_scratch, count)) {
      status = confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                    kInternalInvariant);
      goto fail;
    }
  }
  for (index = 0U; index < assignment_count; ++index) {
    const ConfitAssignment *assignment = &assignments[index];
    ConfitConfigView view;
    size_t config_index;
    if (!confit_symbol_is_valid(assignment->symbol)) {
      status = confit_resolver_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                                    kUnknownAssignment);
      goto fail;
    }
    if (!confit_resolution_find_catalog_index(
            catalog, resolution->lexical_order, count, assignment->symbol,
            &config_index) ||
        !confit_catalog_config_at(catalog, config_index, &view)) {
      status = confit_resolver_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                                    kUnknownAssignment);
      goto fail;
    }
    if (work.explicit_assignments[config_index] != 0U) {
      status = confit_resolver_fail(diagnostic, CONFIT_ERR_VALIDATION, &view,
                                    kDuplicateAssignment);
      goto fail;
    }
    status = confit_resolver_copy_assignment(
        &view, &assignment->value, &resolved_allocator,
        &work.candidates[config_index], diagnostic);
    if (status != CONFIT_OK) goto fail;
    work.explicit_assignments[config_index] = 1U;
    work.origins[config_index] = CONFIT_ORIGIN_USER;
  }
  for (index = 0U; index < count; ++index) {
    ConfitDependencyEvaluation *evaluation = 0;
    ConfitConfigView view;
    const ConfitValue *effective;
    size_t config_index;
    size_t reason_root;
    int available;
    if (!confit_dependency_plan_order_at(plan, index, &config_index) ||
        !confit_catalog_config_at(catalog, config_index, &view)) {
      status = confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                    kInternalInvariant);
      goto fail;
    }
    status = confit_dependency_plan_evaluate_prevalidated(
        plan, config_index, work.candidates, count, &resolved_allocator,
        &evaluation, diagnostic);
    if (status != CONFIT_OK) goto fail;
    available = confit_dependency_evaluation_available(evaluation);
    status = confit_resolution_copy_evaluation(
        resolution, evaluation, &view, &reason_root, diagnostic);
    confit_dependency_evaluation_destroy(evaluation);
    if (status != CONFIT_OK) goto fail;
    if (!available) {
      status = confit_resolution_wrap_unavailable(
          resolution, &view, reason_root, &reason_root, diagnostic);
      if (status != CONFIT_OK) goto fail;
      if (work.explicit_assignments[config_index] != 0U &&
          !confit_value_equal(&work.candidates[config_index],
                              view.default_value)) {
        status = confit_resolver_fail(diagnostic, CONFIT_ERR_VALIDATION,
                                      &view, kUnavailableValue);
        goto fail;
      }
      effective = view.default_value;
    } else {
      effective = &work.candidates[config_index];
    }
    status = confit_resolved_value_set(
        &resolution->values[config_index], view.symbol, view.default_value,
        effective, work.origins[config_index], available, reason_root,
        &resolved_allocator, diagnostic);
    if (status != CONFIT_OK) goto fail;
  }
  confit_resolve_work_destroy(&work);
  *out_resolution = resolution;
  return CONFIT_OK;

fail:
  confit_resolve_work_destroy(&work);
  confit_resolution_destroy(resolution);
  return status;
}

const ConfitCatalog *
confit_resolution_catalog(const ConfitResolution *resolution) {
  return resolution != 0 ? resolution->catalog : 0;
}

size_t confit_resolution_value_count(const ConfitResolution *resolution) {
  return resolution != 0 ? resolution->value_count : 0U;
}

int confit_resolution_value_at(const ConfitResolution *resolution,
                               size_t lexical_index,
                               const ConfitResolvedValue **out_value) {
  if (resolution == 0 || out_value == 0 ||
      lexical_index >= resolution->value_count)
    return 0;
  *out_value = &resolution->values[resolution->lexical_order[lexical_index]];
  return 1;
}

int confit_resolution_find_value(const ConfitResolution *resolution,
                                 const char *symbol,
                                 const ConfitResolvedValue **out_value) {
  size_t index;
  if (resolution == 0 || symbol == 0 || out_value == 0 ||
      !confit_resolution_find_catalog_index(
          resolution->catalog, resolution->lexical_order,
          resolution->value_count, symbol, &index))
    return 0;
  *out_value = &resolution->values[index];
  return 1;
}

size_t confit_resolution_reason_count(const ConfitResolution *resolution) {
  return resolution != 0 ? resolution->reason_count : 0U;
}

int confit_resolution_reason_at(const ConfitResolution *resolution,
                                size_t index,
                                const ConfitReasonNode **out_reason) {
  if (resolution == 0 || out_reason == 0 || index >= resolution->reason_count)
    return 0;
  *out_reason = &resolution->reasons[index];
  return 1;
}

static size_t confit_resolution_unsigned(size_t value, char output[32]) {
  char reverse[32];
  size_t count = 0U;
  size_t index;
  do {
    reverse[count++] = (char)('0' + (value % 10U));
    value /= 10U;
  } while (value != 0U);
  for (index = 0U; index < count; ++index)
    output[index] = reverse[count - index - 1U];
  return count;
}

static int confit_resolution_append(char *buffer, size_t *cursor,
                                    const char *bytes, size_t size) {
  size_t next;
  if (!confit_resolver_size_add(*cursor, size, &next)) return 0;
  if (buffer != 0 && size != 0U) memcpy(buffer + *cursor, bytes, size);
  *cursor = next;
  return 1;
}

static ConfitStatus confit_resolution_write_canonical(
    const ConfitResolution *resolution, char *buffer, size_t *out_size,
    ConfitDiagnostic *diagnostic) {
  static const char prefix[] = "resolution-v6\n";
  char default_text[CONFIT_LIMIT_STRING_BYTES + 64U];
  char effective_text[CONFIT_LIMIT_STRING_BYTES + 64U];
  char digits[32];
  size_t cursor = 0U;
  size_t index;
  if (!confit_resolution_append(buffer, &cursor, prefix, sizeof(prefix) - 1U))
    goto overflow;
  for (index = 0U; index < resolution->value_count; ++index) {
    const ConfitResolvedValue *value;
    const char *origin;
    size_t symbol_size;
    size_t default_size;
    size_t effective_size;
    size_t digit_count;
    if (!confit_resolution_value_at(resolution, index, &value)) goto overflow;
    symbol_size = strlen(value->symbol);
    if (confit_value_format_canonical(
            &value->default_value, default_text, sizeof(default_text),
            &default_size, diagnostic) != CONFIT_OK ||
        confit_value_format_canonical(
            &value->effective_value, effective_text, sizeof(effective_text),
            &effective_size, diagnostic) != CONFIT_OK)
      return CONFIT_ERR_INTERNAL;
    origin = value->origin == CONFIT_ORIGIN_USER ? "user" : "default";
    digit_count = confit_resolution_unsigned(symbol_size, digits);
    if (!confit_resolution_append(buffer, &cursor, "s:", 2U) ||
        !confit_resolution_append(buffer, &cursor, digits, digit_count) ||
        !confit_resolution_append(buffer, &cursor, ":", 1U) ||
        !confit_resolution_append(buffer, &cursor, value->symbol, symbol_size) ||
        !confit_resolution_append(buffer, &cursor, ";a:", 3U) ||
        !confit_resolution_append(buffer, &cursor,
                                  value->available ? "1" : "0", 1U) ||
        !confit_resolution_append(buffer, &cursor, ";o:", 3U) ||
        !confit_resolution_append(buffer, &cursor, origin, strlen(origin)) ||
        !confit_resolution_append(buffer, &cursor, ";d:", 3U))
      goto overflow;
    digit_count = confit_resolution_unsigned(default_size, digits);
    if (!confit_resolution_append(buffer, &cursor, digits, digit_count) ||
        !confit_resolution_append(buffer, &cursor, ":", 1U) ||
        !confit_resolution_append(buffer, &cursor, default_text, default_size) ||
        !confit_resolution_append(buffer, &cursor, ";e:", 3U))
      goto overflow;
    digit_count = confit_resolution_unsigned(effective_size, digits);
    if (!confit_resolution_append(buffer, &cursor, digits, digit_count) ||
        !confit_resolution_append(buffer, &cursor, ":", 1U) ||
        !confit_resolution_append(buffer, &cursor, effective_text,
                                  effective_size) ||
        !confit_resolution_append(buffer, &cursor, "\n", 1U))
      goto overflow;
  }
  *out_size = cursor;
  return CONFIT_OK;

overflow:
  return confit_resolver_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                              kInternalInvariant);
}

ConfitStatus confit_resolution_format_canonical(
    const ConfitResolution *resolution, char *buffer, size_t buffer_size,
    size_t *out_size, ConfitDiagnostic *diagnostic) {
  size_t required;
  ConfitStatus status;
  if (resolution == 0 || out_size == 0)
    return confit_resolver_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                kInvalidArgument);
  status = confit_resolution_write_canonical(resolution, 0, &required,
                                             diagnostic);
  if (status != CONFIT_OK) return status;
  *out_size = required;
  if (buffer == 0 || buffer_size <= required)
    return confit_resolver_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                kCanonicalBuffer);
  status = confit_resolution_write_canonical(resolution, buffer, &required,
                                             diagnostic);
  if (status != CONFIT_OK) return status;
  buffer[required] = '\0';
  return CONFIT_OK;
}

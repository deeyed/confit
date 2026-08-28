#ifndef CONFIT_MODEL_H
#define CONFIT_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "confit/diagnostic.h"
#include "confit/limits.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief No model object occupies this index. */
#define CONFIT_INDEX_NONE ((size_t)-1)

typedef void *(*ConfitAllocateFunction)(void *context, size_t size);
typedef void (*ConfitDeallocateFunction)(void *context, void *pointer);

/**
 * @brief Allocation capability used by owned model objects.
 *
 * Both functions must be present together.  Passing `NULL` to a constructor
 * selects the C runtime allocator.  The capability is copied into every object
 * that owns storage, so its context must outlive that object.
 */
typedef struct ConfitAllocator {
  void *context;
  ConfitAllocateFunction allocate;
  ConfitDeallocateFunction deallocate;
} ConfitAllocator;

/** @brief Populate an allocator capability backed by malloc/free. */
void confit_allocator_default(ConfitAllocator *out_allocator);

/** @brief Return nonzero when the allocator capability is complete. */
int confit_allocator_is_valid(const ConfitAllocator *allocator);

/** @brief The complete schema 6 configuration value-kind set. */
typedef enum ConfitValueKind {
  CONFIT_VALUE_INVALID = 0,
  CONFIT_VALUE_BOOL,
  CONFIT_VALUE_INT,
  CONFIT_VALUE_HEX,
  CONFIT_VALUE_STRING,
  CONFIT_VALUE_ENUM,
} ConfitValueKind;

/** @brief Owned length-delimited UTF-8 text used inside ConfitValue. */
typedef struct ConfitOwnedText {
  char *data;
  size_t size;
} ConfitOwnedText;

/**
 * @brief Type-safe, owned configuration value.
 *
 * Initialize with `confit_value_init()` before any other operation and release
 * with `confit_value_destroy()`.  String and enum storage is owned by the value;
 * text returned by accessors is borrowed until the next mutation or destroy.
 */
typedef struct ConfitValue {
  ConfitValueKind kind;
  union {
    int boolean;
    int64_t integer;
    uint64_t hexadecimal;
    ConfitOwnedText text;
  } data;
  ConfitAllocator allocator;
} ConfitValue;

void confit_value_init(ConfitValue *value);
void confit_value_destroy(ConfitValue *value);
ConfitStatus confit_value_set_bool(ConfitValue *value, int boolean,
                                   const ConfitAllocator *allocator,
                                   ConfitDiagnostic *diagnostic);
ConfitStatus confit_value_set_int(ConfitValue *value, int64_t integer,
                                  const ConfitAllocator *allocator,
                                  ConfitDiagnostic *diagnostic);
ConfitStatus confit_value_set_hex(ConfitValue *value, uint64_t hexadecimal,
                                  const ConfitAllocator *allocator,
                                  ConfitDiagnostic *diagnostic);
ConfitStatus confit_value_set_string(ConfitValue *value, const char *text,
                                     size_t text_size,
                                     const ConfitAllocator *allocator,
                                     ConfitDiagnostic *diagnostic);
ConfitStatus confit_value_set_enum(ConfitValue *value, const char *atom,
                                   size_t atom_size,
                                   const ConfitAllocator *allocator,
                                   ConfitDiagnostic *diagnostic);

/**
 * @brief Copy a value transactionally.
 *
 * On failure the destination is unchanged.  The destination must previously
 * have been initialized, and may already own a value.
 */
ConfitStatus confit_value_copy(ConfitValue *destination,
                               const ConfitValue *source,
                               const ConfitAllocator *allocator,
                               ConfitDiagnostic *diagnostic);

int confit_value_equal(const ConfitValue *left, const ConfitValue *right);

/** @brief Borrow string/enum bytes; return zero for every other kind. */
int confit_value_text(const ConfitValue *value, const char **out_text,
                      size_t *out_size);

/**
 * @brief Write a deterministic, length-framed core representation.
 *
 * This is not TOML, Make, C, or JSON output.  String-like values use
 * `<kind>:<byte-count>:<raw-bytes>`, which is unambiguous even when bytes contain
 * whitespace.  `out_size` excludes the trailing NUL.  An undersized buffer is
 * left untouched and returns `CONFIT_ERR_USAGE`.
 */
ConfitStatus confit_value_format_canonical(const ConfitValue *value,
                                            char *buffer,
                                            size_t buffer_size,
                                            size_t *out_size,
                                            ConfitDiagnostic *diagnostic);

/** @brief Validate one enum domain without constructing a declaration. */
ConfitStatus confit_enum_domain_validate(const char *const *values,
                                         size_t value_count,
                                         ConfitDiagnostic *diagnostic);

/** @brief Borrowed source location.  Zero line/column means unavailable. */
typedef struct ConfitSourceSpan {
  const char *path;
  size_t line;
  size_t column;
} ConfitSourceSpan;

typedef struct ConfitSourceFragmentSpec {
  const char *path;
  size_t parent_fragment;
  size_t source_ordinal;
} ConfitSourceFragmentSpec;

typedef struct ConfitSourceFragmentView {
  const char *path;
  size_t parent_fragment;
  size_t source_ordinal;
} ConfitSourceFragmentView;

typedef struct ConfitMenuSpec {
  size_t fragment;
  size_t parent_menu;
  const char *prompt;
  const char *help;
  ConfitSourceSpan declaration;
} ConfitMenuSpec;

typedef struct ConfitMenuView {
  size_t fragment;
  size_t parent_menu;
  const char *prompt;
  const char *help;
  ConfitSourceSpan declaration;
} ConfitMenuView;

typedef struct ConfitValueRangeSpec {
  int present;
  const ConfitValue *minimum;
  const ConfitValue *maximum;
} ConfitValueRangeSpec;

typedef struct ConfitConfigSpec {
  size_t fragment;
  size_t menu;
  const char *symbol;
  ConfitValueKind kind;
  const char *prompt;
  const char *help;
  const ConfitValue *default_value;
  ConfitValueRangeSpec range;
  const char *const *enum_values;
  size_t enum_value_count;
  const char *dependency_text;
  ConfitSourceSpan declaration;
} ConfitConfigSpec;

/**
 * @brief Borrowed read-only declaration view.
 *
 * Every pointer remains valid until the owning catalog is reset or destroyed.
 */
typedef struct ConfitConfigView {
  size_t fragment;
  size_t menu;
  const char *symbol;
  ConfitValueKind kind;
  const char *prompt;
  const char *help;
  const ConfitValue *default_value;
  int has_range;
  const ConfitValue *range_minimum;
  const ConfitValue *range_maximum;
  const char *const *enum_values;
  size_t enum_value_count;
  const char *dependency_text;
  ConfitSourceSpan declaration;
} ConfitConfigView;

/**
 * @brief Opaque project-wide catalog.
 *
 * The catalog owns root-title, fragment, menu, declaration, default, range,
 * enum-domain, dependency-text, and declaration-path storage.  It never owns a
 * file descriptor or implicit path-open capability.
 */
typedef struct ConfitCatalog ConfitCatalog;

ConfitStatus confit_catalog_create(const ConfitAllocator *allocator,
                                   ConfitCatalog **out_catalog,
                                   ConfitDiagnostic *diagnostic);
void confit_catalog_reset(ConfitCatalog *catalog);
void confit_catalog_destroy(ConfitCatalog *catalog);
ConfitStatus confit_catalog_set_mainmenu(ConfitCatalog *catalog,
                                         const char *mainmenu,
                                         ConfitDiagnostic *diagnostic);
const char *confit_catalog_mainmenu(const ConfitCatalog *catalog);

ConfitStatus confit_catalog_add_fragment(
    ConfitCatalog *catalog, const ConfitSourceFragmentSpec *spec,
    size_t *out_index, ConfitDiagnostic *diagnostic);
ConfitStatus confit_catalog_add_menu(ConfitCatalog *catalog,
                                     const ConfitMenuSpec *spec,
                                     size_t *out_index,
                                     ConfitDiagnostic *diagnostic);
ConfitStatus confit_catalog_add_config(ConfitCatalog *catalog,
                                       const ConfitConfigSpec *spec,
                                       size_t *out_index,
                                       ConfitDiagnostic *diagnostic);

size_t confit_catalog_fragment_count(const ConfitCatalog *catalog);
size_t confit_catalog_menu_count(const ConfitCatalog *catalog);
size_t confit_catalog_config_count(const ConfitCatalog *catalog);
int confit_catalog_fragment_at(const ConfitCatalog *catalog, size_t index,
                               ConfitSourceFragmentView *out_view);
int confit_catalog_menu_at(const ConfitCatalog *catalog, size_t index,
                           ConfitMenuView *out_view);
int confit_catalog_config_at(const ConfitCatalog *catalog, size_t index,
                             ConfitConfigView *out_view);
int confit_catalog_find_config(const ConfitCatalog *catalog,
                               const char *symbol,
                               ConfitConfigView *out_view);

/** @brief Owned explicit user assignment model; no precedence is represented. */
typedef struct ConfitAssignment {
  char *symbol;
  ConfitValue value;
  ConfitAllocator allocator;
} ConfitAssignment;

void confit_assignment_init(ConfitAssignment *assignment);
void confit_assignment_destroy(ConfitAssignment *assignment);
ConfitStatus confit_assignment_set(ConfitAssignment *assignment,
                                   const char *symbol,
                                   const ConfitValue *value,
                                   const ConfitAllocator *allocator,
                                   ConfitDiagnostic *diagnostic);

typedef enum ConfitValueOrigin {
  CONFIT_ORIGIN_INVALID = 0,
  CONFIT_ORIGIN_DEFAULT,
  CONFIT_ORIGIN_USER,
} ConfitValueOrigin;

typedef enum ConfitReasonKind {
  CONFIT_REASON_NONE = 0,
  CONFIT_REASON_LITERAL,
  CONFIT_REASON_REFERENCE,
  CONFIT_REASON_NOT,
  CONFIT_REASON_AND,
  CONFIT_REASON_OR,
  CONFIT_REASON_COMPARISON,
  CONFIT_REASON_UNAVAILABLE,
} ConfitReasonKind;

#define CONFIT_REASON_CHILD_LIMIT ((size_t)2U)

typedef struct ConfitReasonNode {
  ConfitReasonKind kind;
  char *subject_symbol;
  char *related_symbol;
  char *detail;
  size_t children[2];
  size_t child_count;
  ConfitAllocator allocator;
} ConfitReasonNode;

void confit_reason_node_init(ConfitReasonNode *reason);
void confit_reason_node_destroy(ConfitReasonNode *reason);
ConfitStatus confit_reason_node_set(
    ConfitReasonNode *reason, ConfitReasonKind kind,
    const char *subject_symbol, const char *related_symbol,
    const char *detail, const size_t *children, size_t child_count,
    const ConfitAllocator *allocator, ConfitDiagnostic *diagnostic);

typedef struct ConfitResolvedValue {
  char *symbol;
  ConfitValue default_value;
  ConfitValue effective_value;
  ConfitValueOrigin origin;
  int available;
  size_t reason;
  ConfitAllocator allocator;
} ConfitResolvedValue;

void confit_resolved_value_init(ConfitResolvedValue *resolved);
void confit_resolved_value_destroy(ConfitResolvedValue *resolved);
ConfitStatus confit_resolved_value_set(
    ConfitResolvedValue *resolved, const char *symbol,
    const ConfitValue *default_value, const ConfitValue *effective_value,
    ConfitValueOrigin origin, int available, size_t reason,
    const ConfitAllocator *allocator, ConfitDiagnostic *diagnostic);

/** @brief Validate the public symbol grammar without allocating. */
int confit_symbol_is_valid(const char *symbol);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_MODEL_H */

#include "confit/source.h"

#include "source_internal.h"
#include "test_assert.h"
#include "test_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_PATH_BYTES 4096U

typedef struct FailingAllocatorState {
  size_t attempt;
  size_t fail_at;
  size_t outstanding;
} FailingAllocatorState;

static void *failing_allocate(void *context, size_t size) {
  FailingAllocatorState *state = (FailingAllocatorState *)context;
  void *pointer;
  if (state->attempt++ == state->fail_at) {
    return 0;
  }
  pointer = malloc(size);
  if (pointer != 0) {
    state->outstanding += 1U;
  }
  return pointer;
}

static void failing_deallocate(void *context, void *pointer) {
  FailingAllocatorState *state = (FailingAllocatorState *)context;
  CONFIT_TEST_ASSERT(pointer != 0 && state->outstanding > 0U);
  state->outstanding -= 1U;
  free(pointer);
}

static void join_path(char *out, const char *root, const char *relative) {
  CONFIT_TEST_ASSERT(
      confit_test_fs_path_join(out, TEST_PATH_BYTES, root, relative));
}

static void make_directory(const char *root, const char *relative) {
  char path[TEST_PATH_BYTES];
  join_path(path, root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_make_dirs(path));
}

static void write_text(const char *root, const char *relative,
                       const char *text) {
  char path[TEST_PATH_BYTES];
  join_path(path, root, relative);
  CONFIT_TEST_ASSERT(confit_test_fs_write_file(path, text));
}

static int graph_has_path(const ConfitSourceGraph *graph, const char *path) {
  size_t index = CONFIT_INDEX_NONE;
  return confit_source_graph_find(graph, path, &index) &&
         index != CONFIT_INDEX_NONE;
}

static void expect_load_failure(ConfitHostRoot *root, const char *entry,
                                const char *message) {
  ConfitDiagnostic diagnostic;
  ConfitSourceGraph *graph = 0;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_source_graph_load(root, entry, 0, &graph,
                                              &diagnostic) != CONFIT_OK);
  CONFIT_TEST_ASSERT(graph == 0);
  CONFIT_TEST_ASSERT(diagnostic.message != 0);
  if (strcmp(diagnostic.message, message) != 0) {
    fprintf(stderr, "source graph diagnostic mismatch for %s: expected '%s', "
                    "actual '%s'\n",
            entry, message, diagnostic.message);
  }
  CONFIT_TEST_ASSERT(strcmp(diagnostic.message, message) == 0);
}

static void test_basic_poison_and_ledger(const char *root_path,
                                         ConfitHostRoot *root) {
  static const char entry[] =
      "schema_version = 6\n"
      "mainmenu = \"Example\"\n"
      "source = [\"basic/config/reachable.toml\"]\n";
  static const char leaf[] =
      "[menu]\n"
      "prompt = \"Reachable\"\n"
      "help = \"Reachable only.\"\n";
  ConfitDiagnostic diagnostic;
  ConfitSourceGraph *graph = 0;
  ConfitSourceNodeView node;
  ConfitSourceReadLedger ledger;
  ConfitSourceReadRecord records[2];
  size_t expected_total = sizeof(entry) - 1U + sizeof(leaf) - 1U;

  make_directory(root_path, "basic/config");
  make_directory(root_path, "basic/unrelated");
  make_directory(root_path, "basic/src");
  make_directory(root_path, "basic/build");
  write_text(root_path, "basic/Confit.toml", entry);
  write_text(root_path, "basic/config/reachable.toml", leaf);
  write_text(root_path, "basic/unrelated/invalid.toml", "value = \"\n");
  write_text(root_path, "basic/src/poison.c", "#error unread poison\n");
  write_text(root_path, "basic/Makefile", ".error unread poison\n");
  write_text(root_path, "basic/build/poison.mk", ".error unread poison\n");

  confit_diagnostic_init(&diagnostic);
  confit_source_read_ledger_init(&ledger, records, 2U);
  CONFIT_TEST_ASSERT(confit_source_graph_load_observed(
                         root, "basic/Confit.toml", 0, &ledger, &graph,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(graph != 0);
  CONFIT_TEST_ASSERT(confit_source_graph_node_count(graph) == 2U);
  CONFIT_TEST_ASSERT(confit_source_graph_edge_count(graph) == 1U);
  CONFIT_TEST_ASSERT(confit_source_graph_total_bytes(graph) == expected_total);
  CONFIT_TEST_ASSERT(ledger.count == 2U);
  CONFIT_TEST_ASSERT(strcmp(records[0].path, "basic/Confit.toml") == 0);
  CONFIT_TEST_ASSERT(records[0].purpose == CONFIT_SOURCE_READ_ENTRY);
  CONFIT_TEST_ASSERT(records[0].byte_count == sizeof(entry) - 1U);
  CONFIT_TEST_ASSERT(
      strcmp(records[1].path, "basic/config/reachable.toml") == 0);
  CONFIT_TEST_ASSERT(records[1].purpose == CONFIT_SOURCE_READ_FRAGMENT);
  CONFIT_TEST_ASSERT(records[1].byte_count == sizeof(leaf) - 1U);
  CONFIT_TEST_ASSERT(confit_source_graph_node_at(graph, 0U, &node));
  CONFIT_TEST_ASSERT(strcmp(node.path, "basic/Confit.toml") == 0);
  CONFIT_TEST_ASSERT(node.parent_node == CONFIT_INDEX_NONE &&
                     node.include_depth == 0U && node.input != 0);
  CONFIT_TEST_ASSERT(confit_source_graph_node_at(graph, 1U, &node));
  CONFIT_TEST_ASSERT(
      strcmp(node.path, "basic/config/reachable.toml") == 0);
  CONFIT_TEST_ASSERT(node.parent_node == 0U && node.source_ordinal == 0U &&
                     node.include_depth == 1U);
  CONFIT_TEST_ASSERT(!graph_has_path(graph, "basic/unrelated/invalid.toml"));
  CONFIT_TEST_ASSERT(!graph_has_path(graph, "basic/src/poison.c"));
  CONFIT_TEST_ASSERT(!graph_has_path(graph, "basic/Makefile"));
  confit_source_graph_destroy(graph);

  graph = 0;
  confit_source_read_ledger_init(&ledger, records, 1U);
  CONFIT_TEST_ASSERT(confit_source_graph_load_observed(
                         root, "basic/Confit.toml", 0, &ledger, &graph,
                         &diagnostic) == CONFIT_ERR_INTERNAL);
  CONFIT_TEST_ASSERT(graph == 0 && ledger.count == 1U);
}

static void test_nested_root_relative_and_reorder(const char *root_path,
                                                  ConfitHostRoot *root) {
  static const char root_ab[] =
      "source = [\"order/a.toml\", \"order/b.toml\"]\n";
  static const char root_ba[] =
      "source = [\"order/b.toml\", \"order/a.toml\"]\n";
  static const char a[] =
      "[menu]\n"
      "source = [\"order/nested/child.toml\"]\n";
  static const char leaf[] = "[menu]\nprompt = \"Leaf\"\n";
  ConfitDiagnostic diagnostic;
  ConfitSourceGraph *ab = 0;
  ConfitSourceGraph *ba = 0;
  ConfitSourceNodeView node;

  make_directory(root_path, "order/nested");
  write_text(root_path, "order/root-ab.toml", root_ab);
  write_text(root_path, "order/root-ba.toml", root_ba);
  write_text(root_path, "order/a.toml", a);
  write_text(root_path, "order/b.toml", leaf);
  write_text(root_path, "order/nested/child.toml", leaf);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_source_graph_load(
                         root, "order/root-ab.toml", 0, &ab,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_source_graph_load(
                         root, "order/root-ba.toml", 0, &ba,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_source_graph_node_count(ab) == 4U &&
                     confit_source_graph_node_count(ba) == 4U);
  CONFIT_TEST_ASSERT(confit_source_graph_node_at(ab, 1U, &node));
  CONFIT_TEST_ASSERT(strcmp(node.path, "order/a.toml") == 0);
  CONFIT_TEST_ASSERT(confit_source_graph_node_at(ab, 2U, &node));
  CONFIT_TEST_ASSERT(strcmp(node.path, "order/nested/child.toml") == 0 &&
                     node.parent_node == 1U && node.include_depth == 2U);
  CONFIT_TEST_ASSERT(confit_source_graph_node_at(ba, 1U, &node));
  CONFIT_TEST_ASSERT(strcmp(node.path, "order/b.toml") == 0);
  CONFIT_TEST_ASSERT(graph_has_path(ab, "order/a.toml") &&
                     graph_has_path(ab, "order/b.toml") &&
                     graph_has_path(ab, "order/nested/child.toml"));
  CONFIT_TEST_ASSERT(graph_has_path(ba, "order/a.toml") &&
                     graph_has_path(ba, "order/b.toml") &&
                     graph_has_path(ba, "order/nested/child.toml"));
  confit_source_graph_destroy(ba);
  confit_source_graph_destroy(ab);
}

static void test_duplicate_and_cycles(const char *root_path,
                                      ConfitHostRoot *root) {
  make_directory(root_path, "graph-errors");
  write_text(root_path, "graph-errors/leaf.toml", "[menu]\n");
  write_text(root_path, "graph-errors/duplicate.toml",
             "source = [\"graph-errors/leaf.toml\", "
             "\"graph-errors/leaf.toml\"]\n");
  expect_load_failure(root, "graph-errors/duplicate.toml",
                      "source fragment is included more than once");

  write_text(root_path, "graph-errors/root-multi.toml",
             "source = [\"graph-errors/left.toml\", "
             "\"graph-errors/right.toml\"]\n");
  write_text(root_path, "graph-errors/left.toml",
             "[menu]\nsource = [\"graph-errors/common.toml\"]\n");
  write_text(root_path, "graph-errors/right.toml",
             "[menu]\nsource = [\"graph-errors/common.toml\"]\n");
  write_text(root_path, "graph-errors/common.toml", "[menu]\n");
  expect_load_failure(root, "graph-errors/root-multi.toml",
                      "source fragment is included more than once");

  write_text(root_path, "graph-errors/self.toml",
             "source = [\"graph-errors/self.toml\"]\n");
  expect_load_failure(root, "graph-errors/self.toml",
                      "source graph contains an include cycle");

  write_text(root_path, "graph-errors/root-cycle.toml",
             "source = [\"graph-errors/a.toml\"]\n");
  write_text(root_path, "graph-errors/a.toml",
             "[menu]\nsource = [\"graph-errors/b.toml\"]\n");
  write_text(root_path, "graph-errors/b.toml",
             "[menu]\nsource = [\"graph-errors/a.toml\"]\n");
  expect_load_failure(root, "graph-errors/root-cycle.toml",
                      "source graph contains an include cycle");
}

static void test_paths_and_identity(const char *root_path,
                                    ConfitHostRoot *root) {
  char alias_path[TEST_PATH_BYTES];
  char leaf_path[TEST_PATH_BYTES];
  char symlink_path[TEST_PATH_BYTES];
  make_directory(root_path, "paths");
  write_text(root_path, "paths/leaf.toml", "[menu]\n");

  write_text(root_path, "paths/absolute.toml",
             "source = [\"/paths/leaf.toml\"]\n");
  expect_load_failure(root, "paths/absolute.toml",
                      "source path is not a normalized relative TOML path");
  write_text(root_path, "paths/traversal.toml",
             "source = [\"../paths/leaf.toml\"]\n");
  expect_load_failure(root, "paths/traversal.toml",
                      "source path is not a normalized relative TOML path");
  write_text(root_path, "paths/non-toml.toml",
             "source = [\"paths/leaf.conf\"]\n");
  expect_load_failure(root, "paths/non-toml.toml",
                      "source path is not a normalized relative TOML path");

  join_path(leaf_path, root_path, "paths/leaf.toml");
  join_path(symlink_path, root_path, "paths/symlink.toml");
  CONFIT_TEST_ASSERT(symlink("leaf.toml", symlink_path) == 0);
  write_text(root_path, "paths/symlink-root.toml",
             "source = [\"paths/symlink.toml\"]\n");
  {
    ConfitDiagnostic diagnostic;
    ConfitSourceGraph *graph = 0;
    confit_diagnostic_init(&diagnostic);
    CONFIT_TEST_ASSERT(confit_source_graph_load(
                           root, "paths/symlink-root.toml", 0, &graph,
                           &diagnostic) == CONFIT_ERR_IO);
    CONFIT_TEST_ASSERT(graph == 0);
  }

  join_path(alias_path, root_path, "paths/alias.toml");
  CONFIT_TEST_ASSERT(link(leaf_path, alias_path) == 0);
  write_text(root_path, "paths/identity-root.toml",
             "source = [\"paths/leaf.toml\", \"paths/alias.toml\"]\n");
  expect_load_failure(root, "paths/identity-root.toml",
                      "distinct source paths resolve to the same regular file");
}

static void write_depth_chain(const char *root_path, const char *directory,
                              size_t edges) {
  size_t index;
  char relative[256];
  make_directory(root_path, directory);
  for (index = 0U; index <= edges; ++index) {
    char text[512];
    if (index == 0U) {
      CONFIT_TEST_ASSERT(snprintf(relative, sizeof(relative), "%s/n%02zu.toml",
                                  directory, index) > 0);
      if (edges == 0U) {
        strcpy(text, "source = []\n");
      } else {
        CONFIT_TEST_ASSERT(
            snprintf(text, sizeof(text), "source = [\"%s/n%02zu.toml\"]\n",
                     directory, index + 1U) > 0);
      }
    } else {
      CONFIT_TEST_ASSERT(snprintf(relative, sizeof(relative), "%s/n%02zu.toml",
                                  directory, index) > 0);
      if (index == edges) {
        strcpy(text, "[menu]\n");
      } else {
        CONFIT_TEST_ASSERT(snprintf(
                               text, sizeof(text),
                               "[menu]\nsource = [\"%s/n%02zu.toml\"]\n",
                               directory, index + 1U) > 0);
      }
    }
    write_text(root_path, relative, text);
  }
}

static void test_depth_bound(const char *root_path, ConfitHostRoot *root) {
  ConfitDiagnostic diagnostic;
  ConfitSourceGraph *graph = 0;
  ConfitSourceNodeView node;
  write_depth_chain(root_path, "depth-pass", CONFIT_LIMIT_INCLUDE_DEPTH);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_source_graph_load(
                         root, "depth-pass/n00.toml", 0, &graph,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_source_graph_node_count(graph) ==
                     CONFIT_LIMIT_INCLUDE_DEPTH + 1U);
  CONFIT_TEST_ASSERT(confit_source_graph_node_at(
      graph, CONFIT_LIMIT_INCLUDE_DEPTH, &node));
  CONFIT_TEST_ASSERT(node.include_depth == CONFIT_LIMIT_INCLUDE_DEPTH);
  confit_source_graph_destroy(graph);

  write_depth_chain(root_path, "depth-fail",
                    CONFIT_LIMIT_INCLUDE_DEPTH + 1U);
  expect_load_failure(root, "depth-fail/n00.toml",
                      "source include depth limit is exceeded");
}

static void test_count_bounds(void) {
  ConfitDiagnostic diagnostic;
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_source_budget_preflight(
                         1U, 0U, CONFIT_LIMIT_SOURCE_FRAGMENTS - 1U,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_source_budget_preflight(
                         1U, 0U, CONFIT_LIMIT_SOURCE_FRAGMENTS,
                         &diagnostic) == CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(strcmp(diagnostic.message,
                            "source fragment limit is exceeded") == 0);
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_source_budget_preflight(
                         1U, 0U, CONFIT_LIMIT_SOURCE_EDGES,
                         &diagnostic) == CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(strcmp(diagnostic.message,
                            "source fragment limit is exceeded") == 0);
  confit_diagnostic_clear(&diagnostic);
  CONFIT_TEST_ASSERT(confit_source_budget_preflight(
                         0U, CONFIT_LIMIT_SOURCE_EDGES, 0U,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_source_budget_preflight(
                         0U, CONFIT_LIMIT_SOURCE_EDGES, 1U,
                         &diagnostic) == CONFIT_ERR_VALIDATION);
  CONFIT_TEST_ASSERT(strcmp(diagnostic.message,
                            "source edge limit is exceeded") == 0);
}

static void test_source_carrier_errors(const char *root_path,
                                       ConfitHostRoot *root) {
  make_directory(root_path, "carrier");
  write_text(root_path, "carrier/missing.toml", "schema_version = 6\n");
  expect_load_failure(root, "carrier/missing.toml",
                      "entry document requires a top-level source array");
  write_text(root_path, "carrier/scalar.toml", "source = \"leaf.toml\"\n");
  expect_load_failure(root, "carrier/scalar.toml",
                      "source membership must be an array of strings");
  write_text(root_path, "carrier/root.toml",
             "source = [\"carrier/bad-menu.toml\"]\n");
  write_text(root_path, "carrier/bad-menu.toml", "menu = \"bad\"\n");
  expect_load_failure(root, "carrier/root.toml",
                      "fragment menu source carrier must be a table");
}

static void test_allocation_cleanup(const char *root_path,
                                    ConfitHostRoot *root) {
  ConfitAllocator allocator;
  ConfitDiagnostic diagnostic;
  ConfitSourceGraph *graph = 0;
  FailingAllocatorState state;
  size_t fail_at;
  int observed_success = 0;
  make_directory(root_path, "allocation");
  write_text(root_path, "allocation/root.toml",
             "source = [\"allocation/leaf.toml\"]\n");
  write_text(root_path, "allocation/leaf.toml", "[menu]\n");
  allocator.context = &state;
  allocator.allocate = failing_allocate;
  allocator.deallocate = failing_deallocate;
  for (fail_at = 0U; fail_at < 64U; ++fail_at) {
    memset(&state, 0, sizeof(state));
    state.fail_at = fail_at;
    confit_diagnostic_init(&diagnostic);
    if (confit_source_graph_load(root, "allocation/root.toml", &allocator,
                                 &graph, &diagnostic) == CONFIT_OK) {
      CONFIT_TEST_ASSERT(graph != 0);
      confit_source_graph_destroy(graph);
      graph = 0;
      CONFIT_TEST_ASSERT(state.outstanding == 0U);
      observed_success = 1;
      break;
    }
    CONFIT_TEST_ASSERT(graph == 0 && state.outstanding == 0U);
  }
  CONFIT_TEST_ASSERT(observed_success);
}

int main(void) {
  char raw_root[TEST_PATH_BYTES];
  char root_path[TEST_PATH_BYTES];
  ConfitDiagnostic diagnostic;
  ConfitHostRoot *root = 0;

  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(raw_root, sizeof(raw_root),
                                                  "confit-r07"));
  CONFIT_TEST_ASSERT(realpath(raw_root, root_path) != 0);
  confit_diagnostic_init(&diagnostic);
  CONFIT_TEST_ASSERT(confit_host_root_open_absolute(
                         root_path, 0, &root, &diagnostic) == CONFIT_OK);
  test_basic_poison_and_ledger(root_path, root);
  test_nested_root_relative_and_reorder(root_path, root);
  test_duplicate_and_cycles(root_path, root);
  test_paths_and_identity(root_path, root);
  test_depth_bound(root_path, root);
  test_count_bounds();
  test_source_carrier_errors(root_path, root);
  test_allocation_cleanup(root_path, root);
  confit_host_root_destroy(root);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root_path));
  return 0;
}

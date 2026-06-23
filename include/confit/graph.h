#ifndef CONFIT_GRAPH_H
#define CONFIT_GRAPH_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/model.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief graph node record다.
 *
 * `id` 문자열은 graph가 소유한다.
 */
typedef struct ConfitGraphNode {
  /** option id. */
  char *id;
  /** option type. */
  ConfitOptionType type;
  /** prompt가 있으면 1. */
  int is_visible;
} ConfitGraphNode;

/**
 * @brief graph edge record다.
 *
 * `from`, `to` 문자열은 graph가 소유한다.
 */
typedef struct ConfitGraphEdge {
  /** source option id. */
  char *from;
  /** target option id. */
  char *to;
  /** dependency edge kind. */
  ConfitDependencyKind kind;
} ConfitGraphEdge;

/**
 * @brief option dependency graph다.
 *
 * Nodes와 edges는 project model의 deterministic order를 보존한다. 이 구조체는
 * caller가 소유하며 `confit_graph_free`로 해제한다.
 */
typedef struct ConfitGraph {
  /** project name. 없으면 `NULL`. */
  char *project_name;
  /** graph node 목록. */
  ConfitGraphNode *nodes;
  /** graph node 개수. */
  size_t node_count;
  /** graph edge 목록. */
  ConfitGraphEdge *edges;
  /** graph edge 개수. */
  size_t edge_count;
} ConfitGraph;

/**
 * @brief project model의 dependency reference로 graph를 만든다.
 *
 * Unknown dependency reference와 self-edge는 이 단계에서
 * `CONFIT_ERR_DEPENDENCY`로 보고한다. Cycle과 forces legality 같은 전역 graph
 * 제약은 이후 graph validation phase가 담당한다.
 *
 * @param project source project model.
 * @param out_graph 성공 시 caller-owned graph를 받는다.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_graph_build(const ConfitProject *project,
                                ConfitGraph **out_graph,
                                ConfitDiagnostic *diagnostic);

/**
 * @brief graph-level dependency constraints를 검증한다.
 *
 * Round 10 검증은 cycle, illegal forces, visible force, conflict contradiction을
 * 다룬다. Unknown reference와 self-edge는 build 단계에서 이미 검증된다.
 *
 * @param graph 검사할 graph.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받는다.
 * @return 유효하면 CONFIT_OK.
 */
ConfitStatus confit_graph_validate(const ConfitGraph *graph,
                                   ConfitDiagnostic *diagnostic);

/**
 * @brief graph와 그 하위 ownership tree를 해제한다.
 *
 * @param graph 해제할 graph. `NULL`은 허용한다.
 */
void confit_graph_free(ConfitGraph *graph);

/**
 * @brief graph를 deterministic JSON 문자열로 직렬화한다.
 *
 * 반환된 문자열은 caller가 소유하며 `confit_graph_string_free`로 해제한다.
 *
 * @param graph 직렬화할 graph.
 * @param out_json 성공 시 NUL 종료 JSON 문자열을 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_graph_to_json(const ConfitGraph *graph, char **out_json);

/**
 * @brief graph module이 caller에게 넘긴 문자열 allocation을 해제한다.
 *
 * @param text 해제할 문자열. `NULL`은 허용한다.
 */
void confit_graph_string_free(char *text);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_GRAPH_H */

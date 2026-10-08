#ifndef MMUKO_TOPOLOGY_H
#define MMUKO_TOPOLOGY_H

#include <stddef.h>
#include <stdint.h>
#include <mmuko/abi.h>
#include <mmuko/context.h>

/* Process-isolation layer: the POSIX process tree is two-coloured by depth. */
typedef struct mmuko_process_node {
    uint32_t node_id;
    uint32_t parent_node_id;
    uint32_t depth;
    mmuko_bipartite_color color;
    mmuko_cluster_id cluster_id;
} mmuko_process_node;

mmuko_bipartite_color mmuko_topology_color_for_depth(uint32_t depth);
int mmuko_topology_validate_tree(const mmuko_process_node *nodes, size_t count);

/* Execution-context layer.
 *
 * The program context is the neutral control plane: it is not coloured and
 * owns no kernel or module semantics. Execution contexts are coloured by
 * cluster - Cluster A (kernel) and Cluster B (module) - and every peer edge
 * between execution contexts must cross the A|B partition, which is exactly
 * the statement that the execution plane is bipartite. */
typedef enum mmuko_context_color {
    MMUKO_CONTEXT_COLOR_NEUTRAL = 0,
    MMUKO_CONTEXT_COLOR_A = 1,
    MMUKO_CONTEXT_COLOR_B = 2
} mmuko_context_color;

typedef enum mmuko_context_edge_kind {
    MMUKO_CONTEXT_EDGE_CONTROL = 0, /* scheduler dispatch: program -> execution context */
    MMUKO_CONTEXT_EDGE_PEER = 1     /* data-plane adjacency: execution <-> execution */
} mmuko_context_edge_kind;

typedef struct mmuko_context_vertex {
    mmuko_context_role role;
    mmuko_cluster_id cluster;
    mmuko_context_color color;
} mmuko_context_vertex;

typedef struct mmuko_context_edge {
    mmuko_context_role from;
    mmuko_context_role to;
    mmuko_context_edge_kind kind;
} mmuko_context_edge;

typedef enum mmuko_topology_status {
    MMUKO_TOPOLOGY_OK = 0,
    MMUKO_TOPOLOGY_EINVAL = -20,
    MMUKO_TOPOLOGY_EDUPLICATE = -21,     /* role appears twice */
    MMUKO_TOPOLOGY_EROLE_CLUSTER = -22,  /* role bound to the wrong cluster */
    MMUKO_TOPOLOGY_ECOLOR = -23,         /* colour does not match cluster */
    MMUKO_TOPOLOGY_ENEUTRAL = -24,       /* not exactly one neutral program vertex */
    MMUKO_TOPOLOGY_EBILATERAL = -25,     /* missing Cluster A or Cluster B */
    MMUKO_TOPOLOGY_EEDGE = -26,          /* edge references an unknown vertex or itself */
    MMUKO_TOPOLOGY_ECONTROL = -27,       /* control edge not program -> execution, or not exactly one owner */
    MMUKO_TOPOLOGY_ENOT_BIPARTITE = -28  /* peer edge inside one colour class or touching the neutral vertex */
} mmuko_topology_status;

mmuko_cluster_id mmuko_topology_cluster_for_role(mmuko_context_role role);
mmuko_context_color mmuko_topology_color_for_cluster(mmuko_cluster_id cluster);
int mmuko_topology_validate_contexts(const mmuko_context_vertex *vertices, size_t vertex_count,
                                     const mmuko_context_edge *edges, size_t edge_count);

#endif

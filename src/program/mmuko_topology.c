#include <mmuko/topology.h>

mmuko_bipartite_color mmuko_topology_color_for_depth(uint32_t depth)
{
    return (depth & 1u) ? MMUKO_COLOR_ONE : MMUKO_COLOR_ZERO;
}

static int find_node(const mmuko_process_node *nodes, size_t count, uint32_t id)
{
    for (size_t i = 0; i < count; i++) {
        if (nodes[i].node_id == id) return (int)i;
    }
    return -1;
}

int mmuko_topology_validate_tree(const mmuko_process_node *nodes, size_t count)
{
    if (!nodes || count == 0u) return -1;
    size_t roots = 0;

    for (size_t i = 0; i < count; i++) {
        if (nodes[i].color != mmuko_topology_color_for_depth(nodes[i].depth)) return -2;
        for (size_t j = i + 1; j < count; j++) {
            if (nodes[i].node_id == nodes[j].node_id) return -3;
        }
        if (nodes[i].node_id == nodes[i].parent_node_id) {
            if (nodes[i].depth != 0u) return -4;
            roots++;
            continue;
        }
        int parent = find_node(nodes, count, nodes[i].parent_node_id);
        if (parent < 0) return -5;
        if (nodes[parent].depth + 1u != nodes[i].depth) return -6;
        if (nodes[parent].color == nodes[i].color) return -7;
    }
    return roots == 1u ? 0 : -8;
}

mmuko_cluster_id mmuko_topology_cluster_for_role(mmuko_context_role role)
{
    switch (role) {
    case MMUKO_CONTEXT_KERNEL: return MMUKO_CLUSTER_A_KERNEL;
    case MMUKO_CONTEXT_MODULE: return MMUKO_CLUSTER_B_MODULE;
    default: return MMUKO_CLUSTER_PROGRAM;
    }
}

mmuko_context_color mmuko_topology_color_for_cluster(mmuko_cluster_id cluster)
{
    switch (cluster) {
    case MMUKO_CLUSTER_A_KERNEL: return MMUKO_CONTEXT_COLOR_A;
    case MMUKO_CLUSTER_B_MODULE: return MMUKO_CONTEXT_COLOR_B;
    default: return MMUKO_CONTEXT_COLOR_NEUTRAL;
    }
}

static int find_vertex(const mmuko_context_vertex *vertices, size_t count, mmuko_context_role role)
{
    for (size_t i = 0; i < count; i++) {
        if (vertices[i].role == role) return (int)i;
    }
    return -1;
}

int mmuko_topology_validate_contexts(const mmuko_context_vertex *vertices, size_t vertex_count,
                                     const mmuko_context_edge *edges, size_t edge_count)
{
    size_t neutral = 0, cluster_a = 0, cluster_b = 0;

    if (!vertices || vertex_count == 0u || (edge_count != 0u && !edges)) return MMUKO_TOPOLOGY_EINVAL;

    /* Vertices: unique roles, role <-> cluster <-> colour agreement. */
    for (size_t i = 0; i < vertex_count; i++) {
        const mmuko_context_vertex *v = &vertices[i];
        for (size_t j = i + 1; j < vertex_count; j++) {
            if (vertices[j].role == v->role) return MMUKO_TOPOLOGY_EDUPLICATE;
        }
        if (v->cluster != mmuko_topology_cluster_for_role(v->role)) return MMUKO_TOPOLOGY_EROLE_CLUSTER;
        if (v->color != mmuko_topology_color_for_cluster(v->cluster)) return MMUKO_TOPOLOGY_ECOLOR;
        if (v->color == MMUKO_CONTEXT_COLOR_NEUTRAL) neutral++;
        if (v->color == MMUKO_CONTEXT_COLOR_A) cluster_a++;
        if (v->color == MMUKO_CONTEXT_COLOR_B) cluster_b++;
    }
    if (neutral != 1u) return MMUKO_TOPOLOGY_ENEUTRAL;
    if (cluster_a == 0u || cluster_b == 0u) return MMUKO_TOPOLOGY_EBILATERAL;

    /* Edges: control edges fan out from the neutral scheduler; peer edges
     * must cross the A|B partition. */
    for (size_t e = 0; e < edge_count; e++) {
        int from = find_vertex(vertices, vertex_count, edges[e].from);
        int to = find_vertex(vertices, vertex_count, edges[e].to);
        if (from < 0 || to < 0 || from == to) return MMUKO_TOPOLOGY_EEDGE;

        mmuko_context_color from_color = vertices[from].color;
        mmuko_context_color to_color = vertices[to].color;
        if (edges[e].kind == MMUKO_CONTEXT_EDGE_CONTROL) {
            if (from_color != MMUKO_CONTEXT_COLOR_NEUTRAL || to_color == MMUKO_CONTEXT_COLOR_NEUTRAL)
                return MMUKO_TOPOLOGY_ECONTROL;
        } else if (edges[e].kind == MMUKO_CONTEXT_EDGE_PEER) {
            if (from_color == MMUKO_CONTEXT_COLOR_NEUTRAL || to_color == MMUKO_CONTEXT_COLOR_NEUTRAL ||
                from_color == to_color)
                return MMUKO_TOPOLOGY_ENOT_BIPARTITE;
        } else {
            return MMUKO_TOPOLOGY_EEDGE;
        }
    }

    /* Every execution context has exactly one owner (one control in-edge). */
    for (size_t i = 0; i < vertex_count; i++) {
        size_t owners = 0;
        if (vertices[i].color == MMUKO_CONTEXT_COLOR_NEUTRAL) continue;
        for (size_t e = 0; e < edge_count; e++) {
            if (edges[e].kind == MMUKO_CONTEXT_EDGE_CONTROL && edges[e].to == vertices[i].role) owners++;
        }
        if (owners != 1u) return MMUKO_TOPOLOGY_ECONTROL;
    }
    return MMUKO_TOPOLOGY_OK;
}

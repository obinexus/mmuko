#include <mmuko/topology.h>

#include <assert.h>
#include <stdio.h>

static void test_process_tree(void)
{
    mmuko_process_node nodes[] = {
        {0, 0, 0, MMUKO_COLOR_ZERO, MMUKO_CLUSTER_PROGRAM},
        {1, 0, 1, MMUKO_COLOR_ONE, MMUKO_CLUSTER_PROGRAM},
        {2, 1, 2, MMUKO_COLOR_ZERO, MMUKO_CLUSTER_PROGRAM}
    };
    assert(mmuko_topology_validate_tree(nodes, 3) == 0);
    nodes[2].color = MMUKO_COLOR_ONE;
    assert(mmuko_topology_validate_tree(nodes, 3) != 0);
}

static void test_context_topology(void)
{
    mmuko_context_vertex v[] = {
        {MMUKO_CONTEXT_PROGRAM, MMUKO_CLUSTER_PROGRAM, MMUKO_CONTEXT_COLOR_NEUTRAL},
        {MMUKO_CONTEXT_KERNEL, MMUKO_CLUSTER_A_KERNEL, MMUKO_CONTEXT_COLOR_A},
        {MMUKO_CONTEXT_MODULE, MMUKO_CLUSTER_B_MODULE, MMUKO_CONTEXT_COLOR_B}
    };
    mmuko_context_edge e[] = {
        {MMUKO_CONTEXT_PROGRAM, MMUKO_CONTEXT_KERNEL, MMUKO_CONTEXT_EDGE_CONTROL},
        {MMUKO_CONTEXT_PROGRAM, MMUKO_CONTEXT_MODULE, MMUKO_CONTEXT_EDGE_CONTROL},
        {MMUKO_CONTEXT_KERNEL, MMUKO_CONTEXT_MODULE, MMUKO_CONTEXT_EDGE_PEER}
    };
    assert(mmuko_topology_validate_contexts(v, 3, e, 3) == MMUKO_TOPOLOGY_OK);

    /* Peer edge touching the neutral scheduler is not part of the bipartite plane. */
    e[2].from = MMUKO_CONTEXT_PROGRAM;
    assert(mmuko_topology_validate_contexts(v, 3, e, 3) == MMUKO_TOPOLOGY_ENOT_BIPARTITE);
    e[2].from = MMUKO_CONTEXT_KERNEL;

    /* Kernel coloured B: colour must follow the cluster. */
    v[1].color = MMUKO_CONTEXT_COLOR_B;
    assert(mmuko_topology_validate_contexts(v, 3, e, 3) == MMUKO_TOPOLOGY_ECOLOR);
    v[1].color = MMUKO_CONTEXT_COLOR_A;

    /* Module bound to Cluster A: role and cluster disagree. */
    v[2].cluster = MMUKO_CLUSTER_A_KERNEL;
    v[2].color = MMUKO_CONTEXT_COLOR_A;
    assert(mmuko_topology_validate_contexts(v, 3, e, 3) == MMUKO_TOPOLOGY_EROLE_CLUSTER);
    v[2].cluster = MMUKO_CLUSTER_B_MODULE;
    v[2].color = MMUKO_CONTEXT_COLOR_B;

    /* Execution context without an owner. */
    assert(mmuko_topology_validate_contexts(v, 3, e + 1, 2) == MMUKO_TOPOLOGY_ECONTROL);

    /* Control edge from an execution context (kernel scheduling module). */
    e[1].from = MMUKO_CONTEXT_KERNEL;
    assert(mmuko_topology_validate_contexts(v, 3, e, 3) == MMUKO_TOPOLOGY_ECONTROL);
    e[1].from = MMUKO_CONTEXT_PROGRAM;

    /* Two kernels and no module: not bilateral / duplicate role. */
    v[2].role = MMUKO_CONTEXT_KERNEL;
    assert(mmuko_topology_validate_contexts(v, 3, e, 0) == MMUKO_TOPOLOGY_EDUPLICATE);
    v[2].role = MMUKO_CONTEXT_MODULE;

    /* With one context per role, the only way to put both ends of a peer
     * edge in one colour class is a self-loop. */
    e[2].to = MMUKO_CONTEXT_KERNEL;
    assert(mmuko_topology_validate_contexts(v, 3, e, 3) == MMUKO_TOPOLOGY_EEDGE);
    e[2].to = MMUKO_CONTEXT_MODULE;

    assert(mmuko_topology_validate_contexts(v, 3, e, 3) == MMUKO_TOPOLOGY_OK);
}

int main(void)
{
    test_process_tree();
    test_context_topology();
    puts("topology (process tree + bipartite context plane): HELD");
    return 0;
}

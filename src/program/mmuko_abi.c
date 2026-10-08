#include <mmuko/abi.h>

const char *mmuko_cluster_name(mmuko_cluster_id cluster_id)
{
    switch (cluster_id) {
    case MMUKO_CLUSTER_PROGRAM: return "program";
    case MMUKO_CLUSTER_A_KERNEL: return "cluster-a/kernel";
    case MMUKO_CLUSTER_B_MODULE: return "cluster-b/module";
    default: return "unknown";
    }
}

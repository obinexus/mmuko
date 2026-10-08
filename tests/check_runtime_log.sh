#!/bin/sh
# Assertions over a runtime log produced by run_runtime.sh.
#
# usage: check_runtime_log.sh MODE LOG
#   MODE: normal | module-fault | kernel-fault | domain-crash
#
# Lines from the execution domain come from one process and one CPU context
# at a time, so their relative order is deterministic and is checked here.
set -u

mode=$1
log=$2

fail() {
    echo "runtime check ($mode) failed: $*" >&2
    echo "---- $log ----" >&2
    cat "$log" >&2
    exit 1
}
need() { grep -qF -- "$1" "$log" || fail "missing line: $1"; }
absent() { ! grep -qF -- "$1" "$log" || fail "unexpected line: $1"; }
count() {
    n=$(grep -cF -- "$1" "$log")
    [ "$n" -eq "$2" ] || fail "expected $2 x '$1', found $n"
}
# Each pattern's first occurrence must come after the previous pattern's.
in_order() {
    prev=0
    for pattern in "$@"; do
        line=$(grep -nF -m1 -- "$pattern" "$log" | cut -d: -f1)
        [ -n "$line" ] || fail "missing line: $pattern"
        [ "$line" -gt "$prev" ] || fail "out of order: '$pattern' (line $line, previous $prev)"
        prev=$line
    done
}

common() {
    need "[program] bootstrap root node=0"
    need "[program] kernel context READY cluster=cluster-a/kernel"
    need "[program] module context READY cluster=cluster-b/module"
    need "[program] context topology valid"
    need "cluster A registered -> mmuko_kernel_main; cluster B registered -> mmuko_module_main"
    need "bootstrap parent retires now"
    need "[scheduler] kernel context TERMINATED code=0"
    need "[scheduler] module context TERMINATED code=0"
    need "[kernel] state preserved across 2 yields"
    need "[module] state preserved across 2 yields"
    need "[program] supervisor exiting code=0"
}

case "$mode" in
normal)
    common
    in_order \
        "[scheduler] program -> kernel" \
        "[kernel] mmuko_kernel_main entered generation=0" \
        "[kernel] iteration=0" \
        "[kernel] yield" \
        "[scheduler] kernel -> module" \
        "[module] mmuko_module_main entered generation=0" \
        "[module] Orion/GUI iteration=0" \
        "[module] yield" \
        "[scheduler] module -> kernel" \
        "[kernel] resumed" \
        "[kernel] iteration=1" \
        "[module] resumed" \
        "[module] Orion/GUI iteration=1" \
        "[kernel] iteration=2" \
        "[module] Orion/GUI iteration=2"
    count "mmuko_kernel_main entered" 1
    count "mmuko_module_main entered" 1
    count "[kernel] resumed" 2
    count "[module] resumed" 2
    count "[scheduler] " 9
    need "[kernel] peer message kind=2 ontology=mmuko.module.gui.orion"
    need "[module] peer message kind=1 ontology=mmuko.kernel.boot"
    need "[module/orion] graphical module plugged in generation=0"
    need "[module/orion] frame=2 composed"
    absent "FAULTED"
    absent "degraded"
    ;;
module-fault)
    common
    need "[program] injected startup fault cluster=cluster-b/module generation=0"
    need "[scheduler] module context FAULTED code=75 generation=0"
    need "[scheduler] cluster=cluster-b/module recreating module context generation=1 (restart 1/1)"
    need "[module] mmuko_module_main entered generation=1"
    absent "mmuko_module_main entered generation=0"
    in_order \
        "[kernel] iteration=0" \
        "[program] injected startup fault cluster=cluster-b/module" \
        "recreating module context generation=1" \
        "[kernel] resumed" \
        "[module] mmuko_module_main entered generation=1"
    # The kernel context survived the module fault: entered once, never recreated.
    count "mmuko_kernel_main entered" 1
    absent "kernel context FAULTED"
    need "[kernel] peer message kind=2"
    absent "restarting process"
    ;;
kernel-fault)
    common
    need "[program] injected startup fault cluster=cluster-a/kernel generation=0"
    need "[scheduler] kernel context FAULTED code=75 generation=0"
    need "[scheduler] cluster=cluster-a/kernel recreating kernel context generation=1 (restart 1/1)"
    need "[kernel] mmuko_kernel_main entered generation=1"
    absent "mmuko_kernel_main entered generation=0"
    count "mmuko_module_main entered" 1
    absent "module context FAULTED"
    need "[module] peer message kind=1"
    absent "restarting process"
    ;;
domain-crash)
    common
    need "[program] injected hard crash cluster=cluster-b/module generation=0"
    need "[program] supervisor: execution domain generation=0 failed code=137; restarting process (restart 1/1)"
    need "[program] execution domain node=2 parent=1 depth=2 color=0"
    need "generation=1"
    need "[program] supervisor: execution domain generation=1 completed"
    # Process-level recovery recreates the whole domain: both mains run again.
    count "mmuko_kernel_main entered generation=0" 2
    count "mmuko_module_main entered generation=0" 1
    absent "[scheduler] module context FAULTED"
    ;;
*)
    echo "unknown mode: $mode" >&2
    exit 2
    ;;
esac

echo "runtime ($mode): HELD"

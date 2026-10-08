#ifndef MMUKO_PROGRAM_H
#define MMUKO_PROGRAM_H

/* Bootstrap entry. Creates the bilateral process tree, waits until both
 * cluster supervisors have forked their workers, then retires the parent. */
int mmuko_program_main(void);

#endif

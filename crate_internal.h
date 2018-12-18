#ifndef CRATE_CRATE_INTERNAL_H_
#define CRATE_CRATE_INTERNAL_H_

/*
 * NOTICE: This header file should only be needed if you are implementing your
 * own data structures on top of the dsCrate API.
 */

/*
 * Structures used internally by the dsCrate interface.
 */
#define MAGIC_LIB_SUPER     *(uint64_t *)"objSuper"

/*
 * Structures built on top of the dsCrate interface.
 */
#define MAGIC_LIST       *(uint64_t *)"listObj"
#define MAGIC_LISTENTRY  *(uint64_t *)"listEnty"

/*
 * Given an offset and length within the 'active' crate file, return a pointer
 * into its memory map.
 */
void *dsPtr(uint64_t offset, uint64_t length);

/*
 * Given a pointer into the 'active' crate's memory map, return the offset into
 * the crate file.
 */
uint64_t dsOffset(void *address);

/*
 * Call the global log callback set by dsLogger().
 */
#define dsLog(fmt, ...) dsRunLogCallback("%s(): " fmt, __func__, ##__VA_ARGS__)
void dsRunLogCallback(const char *fmt, ...);

/*
 * Scan the crate and print internal information about every object found.
 * This low-level function is just one step above a hexdump in readability.
 */
int dsDebugDump();

#endif

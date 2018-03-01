#ifndef CRATE_OBJECT_H_
#define CRATE_OBJECT_H_

#include <inttypes.h>

/*
 * Incomplete forward declaration.
 */
typedef struct dsCrate dsCrate;

/*
 * Open an crate handle.
 * Optionally, creating it if it doesn't exist.
 * Optionally, setting it as the active crate.
 *
 * On success, a pointer to the opened crate is returned.
 * On error, NULL is returned and errno is set appropriately.
 */
dsCrate *dsOpen(const char *filename, int create, int active);

/*
 * Close and free a previously opened crate handle.
 * If the crate being closed is also the active crate, the
 * active crate is set to NULL.
 */
void dsClose(dsCrate **crate);

/*
 * Set the active crate.
 * All crate operations will act upon the active crate.
 *
 * On success, zero is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
int dsSet(dsCrate *crate);

/*
 * Allocate a new region of 'length' bytes in the active crate.
 *
 * On success, a pointer to the newly allocated region is returned.
 * On error, NULL is returned and errno is set appropriately.
 */
void *dsAlloc(uint64_t length);

/*
 * Free a previously allocated region pointed to by 'address' in the
 * active crate.
 *
 * On success, 0 is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
int dsFree(void *address);

/*
 * Save a snapshot of the active crate to a file called 'filename'.
 *
 * On success, 0 is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
int dsSnapshot(const char *filename);

/*
 * Synchronize the active crate with its file on disk.
 * Optionally, schedule the sync but don't wait on it.
 *
 * On success, 0 is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
int dsSync(int block);

/*
 * The library will call 'callback' each time it wants to print a log message.
 * The callback may be set to NULL to never print library log messages.
 * If a logger is not explicitly set with this function, it defaults to NULL.
 *
 * On success, zero is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
typedef int (*dsLogCallback)(void *userPtr, const char *format, ...);
int dsLogger(dsLogCallback callback);

/*
 * Internal methods.
 */
void *map(uint64_t offset, uint64_t length);
uint64_t objectOffset(void *address);

#endif

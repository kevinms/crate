#ifndef CRATE_OBJECT_H_
#define CRATE_OBJECT_H_

#include <inttypes.h>

/*
 * Incomplete forward declaration.
 */
typedef struct dsObjectStore dsObjectStore;

/*
 * Open an object store handle.
 * Optionally, creating it if it doesn't exist.
 * Optionally, setting it as the active object store.
 *
 * On success, a pointer to the opened object store is returned.
 * On error, NULL is returned and errno is set appropriately.
 */
dsObjectStore *dsOpen(const char *filename, int create, int active);

/*
 * Close and free a previously opened object store handle.
 * If the object store being closed is also the active store, the
 * active store is set to NULL.
 */
void dsClose(dsObjectStore **store);

/*
 * Set the active object store.
 * All object store operations will act upon the active object store.
 *
 * On success, zero is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
int dsSet(dsObjectStore *store);

/*
 * Allocate a new region of 'length' bytes in the active object store.
 *
 * On success, a pointer to the newly allocated region is returned.
 * On error, NULL is returned and errno is set appropriately.
 */
void *dsAlloc(uint64_t length);

/*
 * Free a previously allocated region pointed to by 'address' in the
 * active object store.
 *
 * On success, 0 is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
int dsFree(void *address);

/*
 * Save a snapshot of the active object store to a file called 'filename'.
 *
 * On success, 0 is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
int dsSnapshot(const char *filename);

/*
 * Synchronize the active object store with its file on disk.
 * Optionally, schedule the sync but don't wait on it.
 *
 * On success, 0 is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
int dsSync(int async);

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
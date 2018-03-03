#ifndef CRATE_CRATE_H_
#define CRATE_CRATE_H_

#include <inttypes.h>
#include <stdarg.h>

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
 * Set a region of the crate as the index. The index is used to
 * know what is inside a crate when it is loaded.
 *
 * On success, 0 is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
int dsSetIndex(void *address, uint64_t length);

/*
 * Get the 'active' crate's index.
 * 
 * On success, a pointer to the index is returned. NULL if no index was set.
 * On error, (void *)-1 is returned and errno is set appropriately.
 */
void *dsGetIndex();

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
 * The callback may be set to NULL to never print library log messages. If a
 * logger is not explicitly set with this function, it defaults to NULL.
 *
 * The 'callback' is a process global setting and can't be set per-thread. If
 * you would like separate logging per thread, then use thread-local storage
 * like pthread_specific* or check the thread ID inside the logger callback.
 *
 * On success, zero is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
typedef void (*dsLogCallback)(void *userPtr, const char *format, va_list args);
int dsLogger(dsLogCallback callback, void *userPtr);

/*
 * Simple logger functions provided by the library.
 */
void dsLogToStderr(void *userPtr, const char *format, va_list args);

#endif

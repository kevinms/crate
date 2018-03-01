#ifndef CRATE_LIST_H_
#define CRATE_LIST_H_

#include <inttypes.h>

typedef struct dsList {
	uint64_t magic;
	uint64_t count;
	uint64_t headOffset;
} dsList;

typedef struct dsListEntry {
	uint64_t magic;
	uint64_t prevOffset;
	uint64_t nextOffset;
	uint64_t dataOffset;
} dsListEntry;


/*
 * Allocate and initialize a new list object.
 *
 * On success, a pointer to the new list object is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
dsList *dsListAlloc();

/*
 * Initialize an already allocated list object.
 *
 * On success, zero is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
int dsListInit(dsList *list);

/*
 * Add a new entry to the list that points to 'data'.
 *
 * On success, a pointer to the new list entry is returned.
 * On error, NULL is returned and errno is set appropriately.
 */
dsListEntry *dsListAdd(dsList *list, void *data);

/*
 * Remove the first entry that points to 'data'.
 *
 * On success, zero is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
int dsListDel(dsList *list, void *data);

/*
 * Get a count of how many entries are in the list.
 *
 * On success, the number of entries in the list is returned.
 * On error, -1 is returned and errno is set appropriately.
 */
uint64_t dsListCount(dsList *list);

/*
 * Iterator functions.
 */
dsListEntry *dsListBegin(dsList *list);
dsListEntry *dsListNext(dsListEntry *entry);
void *dsListData(dsListEntry *entry);

#endif

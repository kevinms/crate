#define _GNU_SOURCE

#include <stdio.h>
#include <inttypes.h>

#include "private.h"
#include "crate.h"
#include "list.h"

dsListEntry *
dsListAdd(dsList *list, void *data)
{
	dsListEntry *entry;
	dsListEntry *next;
	uint64_t listEntryOffset;
	uint64_t dataOffset;

	if (list == NULL) {
		log("Bad argument: %p\n", list);
		return(NULL);
	}

	if ((entry = dsAlloc(sizeof(*entry))) == NULL) {
		log("Can't allocate list entry object.\n");
		return(NULL);
	}

	listEntryOffset = objectOffset(entry);
	dataOffset = objectOffset(data);

	debug("listEntryOffset: %" PRIu64 ", dataOffset: %" PRIu64 "\n",\
		  listEntryOffset, dataOffset);

	/*
	 * Found the entry to remove.
	 */
	next = NULL;
	if (list->headOffset != UINT64_MAX) {
		if ((next = map(list->headOffset, sizeof(*next))) == NULL) {
			log("Can't map list next offset.\n");
			if (dsFree(entry) < 0) {
				log("Can't free list entry object.\n");
			}
			return(NULL);
		}
	}

	entry->magic = MAGIC_LISTENTRY;
	entry->dataOffset = dataOffset;
	entry->prevOffset = UINT64_MAX;
	entry->nextOffset = list->headOffset;
	if (next != NULL) {
		next->prevOffset = listEntryOffset;
	}
	list->headOffset = listEntryOffset;
	list->count++;

	return(entry);
}

dsListEntry *
dsListBegin(dsList *list)
{
	dsListEntry *entry;

	if (list->headOffset == UINT64_MAX) {
		return(NULL);
	}

	if ((entry = map(list->headOffset, sizeof(*entry))) == NULL) {
		log("Can't map list head.\n");
		return(NULL);
	}

	return(entry);
}

dsListEntry *
dsListNext(dsListEntry *entry)
{
	if (entry->nextOffset == UINT64_MAX) {
		return(NULL);
	}

	debug("next: %" PRIu64 "\n", entry->nextOffset);

	if ((entry = map(entry->nextOffset, sizeof(*entry))) == NULL) {
		log("Can't map next list entry.\n");
		return(NULL);
	}

	return(entry);
}

void *
dsListData(dsListEntry *entry)
{
	void *data;

	if ((data = map(entry->dataOffset, sizeof(*entry))) == NULL) {
		log("Can't map list data.\n");
		return(NULL);
	}

	return(data);
}

int
dsListInit(dsList *list)
{
	if (list == NULL) {
		return -1;
	}

	list->magic = MAGIC_LIST;
	list->count = 0;
	list->headOffset = UINT64_MAX;

	return 0;
}

dsList *
dsListAlloc()
{
	dsList *list = NULL;
	
	if ((list = dsAlloc(sizeof(*list))) == NULL) {
		return NULL;
	}

	dsListInit(list);

	return list;
}

uint64_t
dsListCount(dsList *list)
{
	if (list == NULL) {
		return -1;
	}

	return list->count;
}

int
dsListDel(dsList *list, void *data)
{
	dsListEntry *entry;
	uint64_t dataOffset;
	uint64_t listEntryOffset;

	dataOffset = objectOffset(data);

	for (entry = dsListBegin(list);
		 entry != NULL;
		 entry = dsListNext(entry)) {

		if (entry->dataOffset == dataOffset) {
			dsListEntry *prev = NULL;
			dsListEntry *next = NULL;

			/*
			 * Found the entry to remove.
			 */
			if (entry->prevOffset != UINT64_MAX) {
				if ((prev = map(entry->prevOffset,
								sizeof(*entry))) == NULL) {
					log("Can't map list previous offset.\n");
					return(-1);
				}
			}
			if (entry->nextOffset != UINT64_MAX) {
				if ((next = map(entry->nextOffset,
								sizeof(*entry))) == NULL) {
					log("Can't map list next offset.\n");
					return(-1);
				}
			}

			/*
			 * Unlink after mapping all neighbors.
			 */
			if (prev != NULL) {
				prev->nextOffset = entry->nextOffset;
			}
			if (next != NULL) {
				next->prevOffset = entry->prevOffset;
			}

			listEntryOffset = objectOffset(entry);
			if (listEntryOffset == list->headOffset) {
				/*
				 * This was the first list entry.
				 */
				list->headOffset = entry->nextOffset;
			}

			list->count--;

			if (dsFree(entry) < 0) {
				log("Can't free list entry object.\n");
				return(-1);
			}

			return(0);
		}
	}

	return(1);
}

#if 0
static inline int
unlinkFromLinkeddsList(dsCrate *crate, void *object,
					 uint64_t magic, uint64_t offsetof,
					 uint64_t *superOffset)
{
	uint64_t *linkOffset;
	uint64_t sizeToMap;
	int i;

	sizeToMap = offsetof + (sizeof(*linkOffset) * 2);
	linkOffset = object+offsetof;

	for (i = 0; i < 2; i++) {
		if (linkOffset[i] != UINT64_MAX) {
			void *neighbor;

			/*
			 * Unlink from neighbor node.
			 */
			if ((neighbor = mapObject(crate,
									  linkOffset[i], sizeToMap)) == NULL) {
				log("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
					linkOffset[i], sizeToMap);
				return(-1);
			}
			if (*(uint64_t *)neighbor != magic) {
				log("Bad magic.\n");
				unmapObject(crate, neighbor);
				return(-1);
			}

			((uint64_t *)(neighbor+offsetof))[i^1] = linkOffset[i^1];
			unmapObject(crate, neighbor);
		} else {
			/*
			 * Unlink from super node.
			 */
			superOffset[i] = linkOffset[i^1];
		}
	}

	linkOffset[0] = UINT64_MAX;
	linkOffset[1] = UINT64_MAX;

	return(0);
}
#endif

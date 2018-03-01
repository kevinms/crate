#define _GNU_SOURCE

#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fs.h>
#include <linux/fiemap.h>

#include "private.h"

/*
 * On-disk structures.
 */

#define freeObjectBit 0x8000000000000000
#define lastObjectBit 0x4000000000000000
#define objectOverhead 24
typedef struct dsObject {
	uint64_t length;
	uint64_t nextGroupOffset;
} dsObject;

#define objectStoreVersion 0x1
#define objectStoreGroups 8 // B K M G T P E Z
typedef struct dsSuperObject {
	uint64_t magic;
	uint64_t version;

	/*
	 * Track free space.
	 */
	uint64_t headGroupOffset[objectStoreGroups];
	uint64_t firstObjectOffset;
} dsSuperObject;

/*
 * In-memory structures.
 */

typedef struct dsMapping {
	void *ptr;
	uint64_t offset;
	uint64_t length;
	struct dsMapping *next;
} dsMapping;

typedef struct dsObjectStore {
	char *filename;
	int fd;
	dsMapping map;
	dsSuperObject *super;
} dsObjectStore;

/*
 * Thread specifics.
 */
static pthread_key_t key;
static pthread_once_t keyOnce = PTHREAD_ONCE_INIT;

static void
makeKey()
{
	if (pthread_key_create(&key, NULL) != 0) {
		log("Can't create pthread key.\n");
		abort();
	}
}

static dsObjectStore *
getdsObjectStore()
{
	pthread_once(&keyOnce, makeKey);

	/*
	 * pthread_getspecific() doesn't fail.
	 */
	return pthread_getspecific(key);
}

static void *
mapObject(dsObjectStore *store, uint64_t offset, uint64_t length)
{
	if (store == NULL) {
		log("Bad argument %p\n", store);
		return NULL;
	}

	if ((offset < store->map.offset) ||
		(offset + length > store->map.offset + store->map.length)) {
		log("Can't map region outside of object store.\n");
		return NULL;
	}

	return store->map.ptr + offset;
}

static void
unmapObject(dsObjectStore *store, void *address)
{
	store = address = NULL;
	return;
}

static void
freedsMapping(dsMapping *mapping)
{
	if ((mapping->ptr != NULL) &&
		(munmap(mapping->ptr, mapping->length) < 0)) {
		log("Can't munmap(%p,%" PRIu64 "): %s", mapping->ptr,
			mapping->length, strerror(errno));
	}
	mapping->ptr = NULL;
	mapping->offset = 0;
	mapping->length = 0;
}

uint64_t
objectOffsetLIB(dsObjectStore *store, void *address)
{
	/*
	 * We only support a single giant mapping, for now.
	 */
	if ((intptr_t)address < (intptr_t)store->map.ptr) {
		log("Pointer falls outside of the object store.\n");
		return UINT64_MAX;
	}

	return (intptr_t)address - (intptr_t)store->map.ptr;
}

static inline uint64_t
getRealLength(uint64_t length)
{
	return length & ~(freeObjectBit | lastObjectBit);
}

#if 0
static dsObject *
prevObject(dsObjectStore *store, dsObject *object)
{
	dsObject *prev;
	uint64_t offset;
	uint64_t *trailer;

	if ((offset = objectOffsetLIB(store, object)) == UINT64_MAX) {
		log("Can't get object offset.\n");
		return (void *)-1;
	}

	if (offset == store->super->firstObjectOffset) {
		return NULL;
	}

	if ((trailer = mapObject(store, offset-sizeof(*trailer),
							 sizeof(*trailer))) == NULL) {
		log("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
			offset-sizeof(*trailer), sizeof(*trailer));
		return (void *)-1;
	}

	if ((prev = mapObject(store, *trailer, sizeof(*prev))) == NULL) {
		log("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
			*trailer, sizeof(*prev));
		return (void *)-1;
	}

	return prev;
}
#endif

static dsObject *
nextObject(dsObjectStore *store, dsObject *object)
{
	dsObject *next;
	uint64_t offset;

	if (object->length & lastObjectBit) {
		return NULL;
	}

	if ((offset = objectOffsetLIB(store, object)) == UINT64_MAX) {
		log("Can't get object offset.\n");
		return (void *)-1;
	}

	if ((next = mapObject(store, offset + getRealLength(object->length),
						  sizeof(*next))) == NULL) {
		log("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
			offset + object->length, sizeof(*next));
		return (void *)-1;
	}

	return next;
}

/*
 * The whole object should already be mapped.
 */
static void
setObjectTrailer(dsObject *object, uint64_t objectOffset)
{
	uint64_t *trailer;
	uint64_t trailerOffset;

	trailerOffset = getRealLength(object->length) - sizeof(*trailer);
	trailer = (uint64_t *)((uintptr_t)object + trailerOffset);
	*trailer = objectOffset;
}

static uint64_t
getObjectTrailer(dsObjectStore *store, dsObject *object)
{
	dsObject *fullObject;
	uint64_t offset;
	uint64_t length;

	if ((offset = objectOffsetLIB(store, object)) == UINT64_MAX) {
		log("Can't get object offset.\n");
		return UINT64_MAX;
	}

	length = getRealLength(object->length);
	if ((fullObject = mapObject(store, offset, length)) == NULL) {
		log("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
			offset, length);
		return UINT64_MAX;
	}

	uint64_t trailer;
	uint64_t trailerOffset;

	trailerOffset = length - sizeof(trailer);
	trailer = *(uint64_t *)((uintptr_t)object + trailerOffset);
	unmapObject(store, fullObject);

	return trailer;
}

static int
debugDump(dsObjectStore *store)
{
	dsObject *object;
	int i;

	if ((object = mapObject(store, store->super->firstObjectOffset,
						 	sizeof(*object))) == NULL) {
		log("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
			store->super->firstObjectOffset, sizeof(*object));
		return -1;
	}

	log("i, head\n");
	for (i = 0; i < objectStoreGroups; i++) {
		log("%d %-20" PRIu64 "\n", i, store->super->headGroupOffset[i]);
	}

	log("%-20s: %-4s %-4s %-20s %-20s %-20s\n",
		"@offset", "free", "last", "length", "next", "offset");
	while (object != NULL) {
		uint64_t offset;

		if ((offset = objectOffsetLIB(store, object)) == UINT64_MAX) {
			log("Can't get object offset.\n");
			return -1;
		}

		/*
		 * Dump debug info.
		 */
		log("%-20" PRIu64 ": %-4s %-4s %-20" PRIu64 " %-20" PRIu64 " %-20" PRIu64 "\n",
			offset,
			object->length & freeObjectBit ? "y" : "n",
			object->length & lastObjectBit ? "y" : "n",
			getRealLength(object->length), object->nextGroupOffset,
			getObjectTrailer(store, object));

		if ((object = nextObject(store, object)) == (void *)-1) {
			log("Can't get next object.\n");
			return -1;
		}
	}

	return 0;
}

static int
unlinkFromGroup(dsObjectStore *store, dsObject *freeObject,
				uint64_t freeObjectOffset, int group)
{
	if (group >= objectStoreGroups) {
		log("Group does not exist.\n");
		return -1;
	}
	if (store->super->headGroupOffset[group] != freeObjectOffset) {
		log("Free object isn't head of group.\n");
		return -1;
	}

	/*
	 * Remove from the group.
	 */
	store->super->headGroupOffset[group] = freeObject->nextGroupOffset;
	freeObject->nextGroupOffset = UINT64_MAX;

	return 0;
}

static int
linkToGroup(dsObjectStore *store, dsObject *freeObject,
			uint64_t freeObjectOffset, int group)
{
	if (group >= objectStoreGroups) {
		log("Group does not exist.\n");
		return -1;
	}

	/*
	 * Add to the new group.
	 */
	freeObject->nextGroupOffset = store->super->headGroupOffset[group];
	store->super->headGroupOffset[group] = freeObjectOffset;

	return 0;
}

static int
getGroup(uint64_t length)
{
	#if 0
	return (int)(log((double)length) / log(1024));
	#else
	int i;
	for (i = 0; length >= 1024; i++, length /= 1024);
	log("Group: %d\n", i);
	return i;
	#endif
}

static void *
allocateObject(dsObjectStore *store, uint64_t length)
{
	dsObject *newObject;
	dsObject *freeObject;
	uint64_t nextGroupOffset;
	uint64_t lengthToAlloc;
	uint64_t realObjectLength;
	int group;

	debugDump(store);

	lengthToAlloc = length + objectOverhead;

	/*
	 * Find the best-fit group.
	 */
	group = getGroup(lengthToAlloc);

	for (; group < objectStoreGroups; group++) {

		nextGroupOffset = store->super->headGroupOffset[group];

		if (nextGroupOffset == UINT64_MAX) {
			/*
			 * This list is empty.
			 */
			continue;
		}

		/*
		 * The first group entry will do.
		 */
		if ((freeObject = mapObject(store, nextGroupOffset,
									sizeof(*freeObject))) == NULL) {
			log("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
				nextGroupOffset, sizeof(*freeObject));
			return NULL;
		}
		if ((freeObject->length & freeObjectBit) == 0) {
			log("Object should be free but isn't.\n");
			unmapObject(store, freeObject);
			return NULL;
		}
		realObjectLength = getRealLength(freeObject->length);

		if (freeObject->length & lastObjectBit) {
			/*
			 * The last free object is the best fit.
			 */
			if (lengthToAlloc > realObjectLength + objectOverhead + 1) {
				/*
				 * But, it isn't large enough. Grow the object store.
				 */
			}
		}

		if (lengthToAlloc > realObjectLength) {
			log("Object groups are corrupt!\n");
			unmapObject(store, freeObject);
			return NULL;
		}

		if (unlinkFromGroup(store, freeObject, nextGroupOffset, group) < 0) {
			log("Can't unlink free object.\n");
			unmapObject(store, freeObject);
			return NULL;
		}

		unmapObject(store, freeObject);
		if ((freeObject = mapObject(store, nextGroupOffset,
									realObjectLength)) == NULL) {
			log("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
				nextGroupOffset, realObjectLength);
			return NULL;
		}
		newObject = freeObject;

		if (realObjectLength < lengthToAlloc + objectOverhead + 1) {
			/*
			 * The free object is too small to split. Use it all.
			 */
			lengthToAlloc = realObjectLength;
		} else {
			int newGroup;
			uint64_t offset;
			uint64_t length;

			/*
			 * Adjust the free object.
			 */
			offset = nextGroupOffset + lengthToAlloc;
			length = realObjectLength - lengthToAlloc;

			freeObject = (dsObject *)((uintptr_t)freeObject +
														lengthToAlloc);
			freeObject->length = length | freeObjectBit;
			freeObject->nextGroupOffset = UINT64_MAX;
			setObjectTrailer(freeObject, offset);

			if (newObject->length & lastObjectBit) {
				freeObject->length |= lastObjectBit;
			}

			newGroup = getGroup(length);
			if (linkToGroup(store, freeObject, offset, newGroup) < 0) {
				log("Can't link free object.\n");
				unmapObject(store, freeObject);
				return NULL;
			}
		}

		/*
		 * Adjust the new new object.
		 */
		newObject->length = lengthToAlloc;
		newObject->nextGroupOffset = UINT64_MAX;
		setObjectTrailer(newObject, nextGroupOffset);

		debugDump(store);

		return newObject;
	}

	return NULL;
}

static void
freedsObjectStore(dsObjectStore **store)
{
	if (store == NULL || *store == NULL) {
		return;
	}

	if ((*store)->fd >= 0) {
		close((*store)->fd);
		(*store)->fd = -1;
	}

	freedsMapping(&(*store)->map);

	free(*store);
	*store = NULL;
}

static void *
allocatedsObjectStore(const char *filename, int create)
{
	dsObjectStore *store;
	struct stat statBuffer;
	int flags = 0;

	if ((store = malloc(sizeof(*store))) == NULL) {
		log("Can't allocate object store.\n");
		return NULL;
	}
	memset(store, 0, sizeof(*store));
	store->filename = strdup(filename);

	flags = O_RDWR | O_NOATIME;
	if (create) {
		flags |= O_CREAT;
	}

	if ((store->fd = open(filename, flags, S_IRUSR | S_IWUSR)) < 0) {
		log("Can't open %s: %s\n", filename, strerror(errno));
		return NULL;
	}

	if (fstat(store->fd, &statBuffer) < 0) {
		log("Can't fstat(%s,): %s\n", filename, strerror(errno));
		return NULL;
	}

	//store->map.length = 32ULL*(1<<30);
	store->map.length = 5*(1<<20); // 90,000
	//store->map.length = 500*(1<<20); // 10,000,000
	if (statBuffer.st_size == 0) {
		/*
		 * Create a giant fixed size sparse file.
		 */
		if (ftruncate(store->fd, store->map.length) < 0) {
			log("Can't ftruncate(%s, %" PRIu64 "): %s\n", filename,
				store->map.length, strerror(errno));
			return NULL;
		}
	}
	if ((store->map.ptr = mmap(0, store->map.length,
							   PROT_READ | PROT_WRITE,
							   MAP_SHARED, store->fd, 0)) == MAP_FAILED) {
		log("Can't map shared memory: %s\n", strerror(errno));
		return NULL;
	}

	store->super = store->map.ptr;
	if (store->super->magic != MAGIC_LIB_SUPER) {
		dsObject *freeObject;
		int i;

		/*
		 * This object store needs a super object.
		 */
		store->super->magic = MAGIC_LIB_SUPER;
		store->super->version = objectStoreVersion;
		for (i = 0; i < objectStoreGroups; i++) {
			store->super->headGroupOffset[i] = UINT64_MAX;
		}
		store->super->firstObjectOffset = sizeof(*store->super);

		/*
		 * Create the first free object.
		 */
		freeObject = store->map.ptr + store->super->firstObjectOffset;
		freeObject->length = store->map.length -
										store->super->firstObjectOffset;
		freeObject->length |= freeObjectBit | lastObjectBit;
		freeObject->nextGroupOffset = UINT64_MAX;
		setObjectTrailer(freeObject, store->super->firstObjectOffset);

		/*
		 * Link the first free object.
		 */
		store->super->headGroupOffset[objectStoreGroups-1] =
										store->super->firstObjectOffset;
	}

	debugDump(store);

	return store;
}

void *
map(uint64_t offset, uint64_t length)
{
	dsObjectStore *store;

	if ((store = getdsObjectStore()) == NULL) {
		log("Can't get object store.\n");
		return NULL;
	}

	return mapObject(store, offset, length);
}

uint64_t
objectOffset(void *address)
{
	dsObjectStore *store;

	if ((store = getdsObjectStore()) == NULL) {
		log("Can't get object store.\n");
		return UINT64_MAX;
	}

	return objectOffsetLIB(store, address);
}

void *
dsAlloc(int length)
{
	dsObjectStore *store;
	void *memory;

	if ((store = getdsObjectStore()) == NULL) {
		log("Can't get object store.\n");
		return NULL;
	}

	if ((memory = allocateObject(store, length)) == NULL) {
		log("Can't allocate object.\n");
		return NULL;
	}

	memory += sizeof(dsObject);

	return memory;
}

int
dsSet(dsObjectStore *store)
{
	/*
	 * Set thread specific object store handle.
	 */
	pthread_once(&keyOnce, makeKey);
	if (pthread_setspecific(key, store) < 0) {
		log("Can't set pthread specific.\n");
		return -1;
	}

	return 0;
}

int
dsFree(void *address)
{
	dsObjectStore *store;

	if ((store = getdsObjectStore()) == NULL) {
		log("Can't get object store.\n");
		return -1;
	}

	address = 0;
	return 0;
}

dsObjectStore *
dsOpen(const char *filename, int create, int active)
{
	dsObjectStore *store;

	if ((store = allocatedsObjectStore(filename, create)) == NULL) {
		log("Can't allocate object store.\n");
		return NULL;
	}

	if (active) {
		if (dsSet(store) < 0) {
			free(store);
			return NULL;
		}
	}

	return store;
}

void
dsClose(dsObjectStore **store)
{
	if (store == NULL || *store == NULL) {
		return;
	}

	if (getdsObjectStore() == *store) {
		/*
		 * Deactivate the object store.
		 */
		dsSet(NULL);
	}

	freedsObjectStore(store);
}

int
dsSnapshot(const char *filename)
{
	dsObjectStore *store;
	struct stat statBuffer;
	int fd;

	if ((store = getdsObjectStore()) == NULL) {
		log("Can't get object store.\n");
		return -1;
	}

	/*
	 * FIEMAP
	 * Inverse of objet index
	 * pagemaps for CBT
	 * File copy
	 */
	if ((fd = open(filename,
				   O_RDWR | O_CREAT | O_EXCL | O_NOATIME,
				   S_IRUSR | S_IWUSR)) < 0) {
		log("Can't open %s: %s\n", filename, strerror(errno));
		return -1;
	}

	if (fstat(store->fd, &statBuffer) < 0) {
		log("Can't fstat(%s,): %s\n", filename, strerror(errno));
		return -1;
	}

	struct fiemap *filemap;

	#define MAX_EXTENT 64
	filemap = malloc(offsetof(struct fiemap, fm_extents[0]) +
								sizeof(struct fiemap_extent) * MAX_EXTENT);

	int count = 0;
	uint64_t start = 0;
	uint64_t end = statBuffer.st_size;
	uint32_t i;

	log("Bob is your uncle: %" PRIu64 ", %" PRIu64 "\n", start, end);

	while (start < end) {
		filemap->fm_start = start;
		filemap->fm_length = end;
		filemap->fm_flags = FIEMAP_FLAG_SYNC;
		filemap->fm_extent_count = MAX_EXTENT;

		if (ioctl(store->fd, FS_IOC_FIEMAP, filemap) != 0) {
			log("Can't ioctl(%d, FS_IOC_FIEMAP,): %s\n",
				store->fd, strerror(errno));
			return -1;
		}

		if (filemap->fm_mapped_extents == 0) {
			break;
		}

		log("FILE: # of extents=%" PRIu32 ", flags=%" PRIu32 "\n",
			filemap->fm_mapped_extents, filemap->fm_flags);

		for (i = 0; i < filemap->fm_mapped_extents; i++) {
			struct fiemap_extent extent;
			extent = filemap->fm_extents[i];

			log("Extent: %5d logical=%lld, phy=%lld, len=%lld, flags=%"
				PRIX32 "\n", count, extent.fe_logical,
				extent.fe_physical, extent.fe_length, extent.fe_flags);

			void *buf = store->map.ptr + extent.fe_logical;

			if (pwrite(fd, buf, extent.fe_length, extent.fe_logical) < 0) {
				log("Can't pwrite(%d,,%lld,%lld): %s\n", fd, extent.fe_length,
					extent.fe_logical, strerror(errno));
				return -1;
			}

			count++;
			start = extent.fe_logical + extent.fe_length;
		}
	}

	free(filemap);
	//close(fd);
	return 0;
}

int
dsSync(int async)
{
	dsObjectStore *store;

	if ((store = getdsObjectStore()) == NULL) {
		log("Can't get object store.\n");
		return -1;
	}

	int flags = async ? MS_SYNC : MS_ASYNC;

	if (msync(store->map.ptr, store->map.length, flags) < 0) {
		log("Can't synchronize object store.\n");
		return -1;
	}

	return 0;
}

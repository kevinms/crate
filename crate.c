#define _GNU_SOURCE

#include "crate.h"

#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fs.h>
#include <linux/fiemap.h>

#include "crate_internal.h"

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

#define crateVersion 0x1
#define objectGroups 8 // B K M G T P E Z
typedef struct dsSuperObject {
	uint64_t magic;
	uint64_t version;

	uint64_t indexObjectOffset;
	uint64_t indexObjectLength;

	/*
	 * Track free space.
	 */
	uint64_t headGroupOffset[objectGroups];
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

typedef struct dsCrate {
	char *filename;
	int fd;

	dsMapping map;
	dsSuperObject *super;
} dsCrate;

/*
 * Logging.
 */
static pthread_rwlock_t logLock = PTHREAD_RWLOCK_INITIALIZER;
static dsLogCallback logCallback = NULL;
static void *logUserPtr = NULL;

void
dsRunLogCallback(const char *fmt, ...)
{
	va_list ap;
	dsLogCallback callback;
	void *userPtr;

	if (logCallback == NULL) {
		return;
	}

	pthread_rwlock_rdlock(&logLock);
	callback = logCallback;
	userPtr = logUserPtr;
	pthread_rwlock_unlock(&logLock);

	va_start(ap, fmt);
	callback(userPtr, fmt, ap);
	va_end(ap);
}

int
dsLogger(dsLogCallback callback, void *userPtr)
{
	pthread_rwlock_wrlock(&logLock);
	logCallback = callback;
	logUserPtr = userPtr;
	pthread_rwlock_unlock(&logLock);

	return 0;
}

void
dsLogToStderr(void *userPtr, const char *format, va_list args)
{
	vfprintf(stderr, format, args);
}

/*
 * Thread specifics.
 */
static pthread_key_t key;
static pthread_once_t keyOnce = PTHREAD_ONCE_INIT;

static void
makeKey()
{
	if (pthread_key_create(&key, NULL) != 0) {
		dsLog("Can't create pthread key.\n");
		abort();
	}
}

static dsCrate *
getActiveCrate()
{
	pthread_once(&keyOnce, makeKey);

	/*
	 * pthread_getspecific() doesn't fail.
	 */
	return pthread_getspecific(key);
}

static void *
mapObject(dsCrate *crate, uint64_t offset, uint64_t length)
{
	if (crate == NULL) {
		dsLog("Bad argument %p\n", crate);
		return NULL;
	}

	if ((offset < crate->map.offset) ||
		(offset + length > crate->map.offset + crate->map.length)) {
		dsLog("Can't map region outside of crate.\n");
		return NULL;
	}

	return crate->map.ptr + offset;
}

static void
unmapObject(dsCrate *crate, void *address)
{
	crate = address = NULL;
	return;
}

#if 0
static dsMapping *
makeMapping(dsCrate *crate)
{
	if (crate == NULL) {
		dsLog("Bad argument %p\n", crate);
		return NULL;
	}

	if ((offset < crate->map.offset) ||
		(offset + length > crate->map.offset + crate->map.length)) {
		dsLog("Can't map region outside of crate.\n");
		return NULL;
	}

	return crate->map.ptr + offset;
}
#endif

static void
freeMapping(dsMapping *mapping)
{
	if (mapping == NULL) {
		return;
	}
	if ((mapping->ptr != NULL) &&
		(munmap(mapping->ptr, mapping->length) < 0)) {
		dsLog("Can't munmap(%p,%" PRIu64 "): %s", mapping->ptr,
			mapping->length, strerror(errno));
	}
	mapping->ptr = NULL;
	mapping->offset = 0;
	mapping->length = 0;
}

static uint64_t
objectOffset(dsCrate *crate, void *address)
{
	/*
	 * We only support a single giant mapping, for now.
	 */
	if ((intptr_t)address < (intptr_t)crate->map.ptr) {
		dsLog("Pointer falls outside of the crate.\n");
		return UINT64_MAX;
	}

	return (intptr_t)address - (intptr_t)crate->map.ptr;
}

static inline uint64_t
getRealLength(uint64_t length)
{
	return length & ~(freeObjectBit | lastObjectBit);
}

#if 0
static dsObject *
prevObject(dsCrate *crate, dsObject *object)
{
	dsObject *prev;
	uint64_t offset;
	uint64_t *trailer;

	if ((offset = objectOffset(crate, object)) == UINT64_MAX) {
		dsLog("Can't get object offset.\n");
		return (void *)-1;
	}

	if (offset == crate->super->firstObjectOffset) {
		return NULL;
	}

	if ((trailer = mapObject(crate, offset-sizeof(*trailer),
							 sizeof(*trailer))) == NULL) {
		dsLog("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
			offset-sizeof(*trailer), sizeof(*trailer));
		return (void *)-1;
	}

	if ((prev = mapObject(crate, *trailer, sizeof(*prev))) == NULL) {
		dsLog("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
			*trailer, sizeof(*prev));
		return (void *)-1;
	}

	return prev;
}
#endif

static dsObject *
nextObject(dsCrate *crate, dsObject *object)
{
	dsObject *next;
	uint64_t offset;

	if (object->length & lastObjectBit) {
		return NULL;
	}

	if ((offset = objectOffset(crate, object)) == UINT64_MAX) {
		dsLog("Can't get object offset.\n");
		return (void *)-1;
	}

	if ((next = mapObject(crate, offset + getRealLength(object->length),
						  sizeof(*next))) == NULL) {
		dsLog("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
			offset + object->length, sizeof(*next));
		return (void *)-1;
	}

	return next;
}

/*
 * The whole object should already be mapped.
 */
static void
setObjectTrailer(dsObject *object, uint64_t offset)
{
	uint64_t *trailer;
	uint64_t trailerOffset;

	trailerOffset = getRealLength(object->length) - sizeof(*trailer);
	trailer = (uint64_t *)((uintptr_t)object + trailerOffset);
	*trailer = offset;
}

static uint64_t
getObjectTrailer(dsCrate *crate, dsObject *object)
{
	dsObject *fullObject;
	uint64_t offset;
	uint64_t length;

	if ((offset = objectOffset(crate, object)) == UINT64_MAX) {
		dsLog("Can't get object offset.\n");
		return UINT64_MAX;
	}

	length = getRealLength(object->length);
	if ((fullObject = mapObject(crate, offset, length)) == NULL) {
		dsLog("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
			offset, length);
		return UINT64_MAX;
	}

	uint64_t trailer;
	uint64_t trailerOffset;

	trailerOffset = length - sizeof(trailer);
	trailer = *(uint64_t *)((uintptr_t)object + trailerOffset);
	unmapObject(crate, fullObject);

	return trailer;
}

static int
debugDump(dsCrate *crate)
{
	dsObject *object;
	int i;

	if ((object = mapObject(crate, crate->super->firstObjectOffset,
						 	sizeof(*object))) == NULL) {
		dsLog("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
			crate->super->firstObjectOffset, sizeof(*object));
		return -1;
	}

	dsLog("i, head\n");
	for (i = 0; i < objectGroups; i++) {
		dsLog("%d %-20" PRIu64 "\n", i, crate->super->headGroupOffset[i]);
	}

	dsLog("%-20s: %-4s %-4s %-20s %-20s %-20s\n",
		"@offset", "free", "last", "length", "next", "offset");
	while (object != NULL) {
		uint64_t offset;

		if ((offset = objectOffset(crate, object)) == UINT64_MAX) {
			dsLog("Can't get object offset.\n");
			return -1;
		}

		/*
		 * Dump debug info.
		 */
		dsLog("%-20" PRIu64 ": %-4s %-4s %-20" PRIu64 " %-20" PRIu64 " %-20" PRIu64 "\n",
			offset,
			object->length & freeObjectBit ? "y" : "n",
			object->length & lastObjectBit ? "y" : "n",
			getRealLength(object->length), object->nextGroupOffset,
			getObjectTrailer(crate, object));

		if ((object = nextObject(crate, object)) == (void *)-1) {
			dsLog("Can't get next object.\n");
			return -1;
		}
	}

	return 0;
}

static int
unlinkFromGroup(dsCrate *crate, dsObject *freeObject,
				uint64_t freeObjectOffset, int group)
{
	if (group >= objectGroups) {
		dsLog("Group does not exist.\n");
		return -1;
	}
	if (crate->super->headGroupOffset[group] != freeObjectOffset) {
		dsLog("Free object isn't head of group.\n");
		return -1;
	}

	/*
	 * Remove from the group.
	 */
	crate->super->headGroupOffset[group] = freeObject->nextGroupOffset;
	freeObject->nextGroupOffset = UINT64_MAX;

	return 0;
}

static int
linkToGroup(dsCrate *crate, dsObject *freeObject,
			uint64_t freeObjectOffset, int group)
{
	if (group >= objectGroups) {
		dsLog("Group does not exist.\n");
		return -1;
	}

	/*
	 * Add to the new group.
	 */
	freeObject->nextGroupOffset = crate->super->headGroupOffset[group];
	crate->super->headGroupOffset[group] = freeObjectOffset;

	return 0;
}

static int
getGroup(uint64_t length)
{
	#if 0
	return (int)(dsLog((double)length) / dsLog(1024));
	#else
	int i;
	for (i = 0; length >= 1024; i++, length /= 1024);
	dsLog("Group: %d\n", i);
	return i;
	#endif
}

static void *
allocateObject(dsCrate *crate, uint64_t length)
{
	dsObject *newObject;
	dsObject *freeObject;
	uint64_t nextGroupOffset;
	uint64_t lengthToAlloc;
	uint64_t realObjectLength;
	int group;

	debugDump(crate);

	lengthToAlloc = length + objectOverhead;

	/*
	 * Find the best-fit group.
	 */
	group = getGroup(lengthToAlloc);

	for (; group < objectGroups; group++) {

		nextGroupOffset = crate->super->headGroupOffset[group];

		if (nextGroupOffset == UINT64_MAX) {
			/*
			 * This list is empty.
			 */
			continue;
		}

		/*
		 * The first group entry will do.
		 */
		if ((freeObject = mapObject(crate, nextGroupOffset,
									sizeof(*freeObject))) == NULL) {
			dsLog("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
				nextGroupOffset, sizeof(*freeObject));
			return NULL;
		}
		if ((freeObject->length & freeObjectBit) == 0) {
			dsLog("Object should be free but isn't.\n");
			unmapObject(crate, freeObject);
			return NULL;
		}
		realObjectLength = getRealLength(freeObject->length);

		if (freeObject->length & lastObjectBit) {
			/*
			 * The last free object is the best fit.
			 */
			if (lengthToAlloc > realObjectLength + objectOverhead + 1) {
				/*
				 * But, it isn't large enough. Grow the crate.
				 */
				//TODO: Grow the crate.
			}
		}

		if (lengthToAlloc > realObjectLength) {
			dsLog("Object groups are corrupt!\n");
			unmapObject(crate, freeObject);
			return NULL;
		}

		if (unlinkFromGroup(crate, freeObject, nextGroupOffset, group) < 0) {
			dsLog("Can't unlink free object.\n");
			unmapObject(crate, freeObject);
			return NULL;
		}

		unmapObject(crate, freeObject);
		if ((freeObject = mapObject(crate, nextGroupOffset,
									realObjectLength)) == NULL) {
			dsLog("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
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
			if (linkToGroup(crate, freeObject, offset, newGroup) < 0) {
				dsLog("Can't link free object.\n");
				unmapObject(crate, freeObject);
				return NULL;
			}
		}

		/*
		 * Adjust the new new object.
		 */
		newObject->length = lengthToAlloc;
		newObject->nextGroupOffset = UINT64_MAX;
		setObjectTrailer(newObject, nextGroupOffset);

		debugDump(crate);

		return newObject;
	}

	return NULL;
}

static void
freeCrate(dsCrate **crate)
{
	if (crate == NULL || *crate == NULL) {
		return;
	}

	if ((*crate)->fd >= 0) {
		close((*crate)->fd);
		(*crate)->fd = -1;
	}

	freeMapping(&(*crate)->map);

	free(*crate);
	*crate = NULL;
}

static int
lockCrate(dsCrate *crate)
{
	if (flock(crate->fd, LOCK_EX) < 0) {
		dsLog("Can't lock crate '%s': %s\n", crate->filename, strerror(errno));
		return -1;
	}

	return 0;
}

static int
unlockCrate(dsCrate *crate)
{
	if (flock(crate->fd, LOCK_UN) < 0) {
		dsLog("Can't unlock crate '%s': %s\n", crate->filename, strerror(errno));
		return -1;
	}

	return 0;
}

static void *
openCrate(const char *filename, int create)
{
	dsCrate *crate = NULL;
	struct stat statBuffer;
	int flags = 0;

	if ((crate = malloc(sizeof(*crate))) == NULL) {
		dsLog("Can't allocate crate.\n");
		goto error;
	}
	memset(crate, 0, sizeof(*crate));
	crate->filename = strdup(filename);

	flags = O_RDWR | O_NOATIME;
	if (create) {
		flags |= O_CREAT;
	}

	if ((crate->fd = open(filename, flags, S_IRUSR | S_IWUSR)) < 0) {
		dsLog("Can't open %s: %s\n", filename, strerror(errno));
		goto error;
	}

	if (lockCrate(crate) < 0) {
		goto error;
	}

	if (fstat(crate->fd, &statBuffer) < 0) {
		dsLog("Can't fstat(%s,): %s\n", filename, strerror(errno));
		goto error;
	}

	if (statBuffer.st_size == 0) {
		/*
		 * Create a giant fixed size sparse file.
		 */
		crate->map.length = 5*(1<<20);

		if (ftruncate(crate->fd, crate->map.length) < 0) {
			dsLog("Can't ftruncate(%s, %" PRIu64 "): %s\n", filename,
				crate->map.length, strerror(errno));
			goto error;
		}
	} else {
		crate->map.length = statBuffer.st_size;
	}

	if ((crate->map.ptr = mmap(0, crate->map.length,
					PROT_READ | PROT_WRITE,
					MAP_SHARED, crate->fd, 0)) == MAP_FAILED) {
		dsLog("Can't map shared memory: %s\n", strerror(errno));
		goto error;
	}

	crate->super = crate->map.ptr;
	if (crate->super->magic != MAGIC_LIB_SUPER) {
		dsObject *freeObject;
		int i;

		/*
		 * This crate needs a super object.
		 */
		crate->super->magic = MAGIC_LIB_SUPER;
		crate->super->version = crateVersion;
		crate->super->indexObjectOffset = UINT64_MAX;
		for (i = 0; i < objectGroups; i++) {
			crate->super->headGroupOffset[i] = UINT64_MAX;
		}
		crate->super->firstObjectOffset = sizeof(*crate->super);

		/*
		 * Create the first free object.
		 */
		freeObject = crate->map.ptr + crate->super->firstObjectOffset;
		freeObject->length = crate->map.length -
										crate->super->firstObjectOffset;
		freeObject->length |= freeObjectBit | lastObjectBit;
		freeObject->nextGroupOffset = UINT64_MAX;
		setObjectTrailer(freeObject, crate->super->firstObjectOffset);

		/*
		 * Link the first free object.
		 */
		crate->super->headGroupOffset[objectGroups-1] =
										crate->super->firstObjectOffset;
	}

	unlockCrate(crate);
	debugDump(crate);

	return crate;

error:

	freeCrate(&crate);

	return NULL;
}

void *
dsPtr(uint64_t offset, uint64_t length)
{
	dsCrate *crate;

	if ((crate = getActiveCrate()) == NULL) {
		dsLog("Can't get active crate.\n");
		return NULL;
	}

	return mapObject(crate, offset, length);
}

uint64_t
dsOffset(void *address)
{
	dsCrate *crate;

	if ((crate = getActiveCrate()) == NULL) {
		dsLog("Can't get active crate.\n");
		return UINT64_MAX;
	}

	return objectOffset(crate, address);
}

void *
dsAlloc(uint64_t length)
{
	dsCrate *crate;
	void *memory;

	if ((crate = getActiveCrate()) == NULL) {
		dsLog("Can't get active crate.\n");
		return NULL;
	}

	if ((memory = allocateObject(crate, length)) == NULL) {
		dsLog("Can't allocate object.\n");
		return NULL;
	}

	memory += sizeof(dsObject);

	return memory;
}

int
dsSet(dsCrate *crate)
{
	/*
	 * Set thread specific crate handle.
	 */
	pthread_once(&keyOnce, makeKey);
	if (pthread_setspecific(key, crate) < 0) {
		dsLog("Can't set pthread specific.\n");
		return -1;
	}

	return 0;
}

int
dsFree(void *address)
{
	dsCrate *crate;

	if ((crate = getActiveCrate()) == NULL) {
		dsLog("Can't get active crate.\n");
		return -1;
	}

	//TODO: Actually free the object.

	address = 0;
	return 0;
}

void
dsClose(dsCrate **crate)
{
	if (crate == NULL || *crate == NULL) {
		return;
	}

	if (getActiveCrate() == *crate) {
		/*
		 * Deactivate the crate.
		 */
		dsSet(NULL);
	}

	freeCrate(crate);
}

dsCrate *
dsOpen(const char *filename, int create, int active)
{
	dsCrate *crate;

	if ((crate = openCrate(filename, create)) == NULL) {
		dsLog("Can't allocate crate.\n");
		return NULL;
	}

	if (active) {
		if (dsSet(crate) < 0) {
			dsClose(&crate);
			return NULL;
		}
	}

	return crate;
}

int
dsSetIndex(void *address, uint64_t length)
{
	dsCrate *crate = NULL;
	uint64_t offset = UINT64_MAX;

	if ((crate = getActiveCrate()) == NULL) {
		dsLog("Can't get active crate.\n");
		return -1;
	}

	if ((offset = objectOffset(crate, address)) == UINT64_MAX) {
		dsLog("Can't get object offset.\n");
		return -1;
	}

	crate->super->indexObjectOffset = offset;
	crate->super->indexObjectLength = length;

	return -1;
}

void *
dsGetIndex()
{
	dsCrate *crate = NULL;
	void *ptr;

	if ((crate = getActiveCrate()) == NULL) {
		dsLog("Can't get active crate.\n");
		return (void *)-1;
	}

	if ((ptr = mapObject(crate, crate->super->indexObjectOffset,
					crate->super->indexObjectLength)) == NULL) {
		dsLog("Can't mapObject(,%" PRIu64 ",%" PRIu64 ")\n",
			crate->super->indexObjectOffset,
			crate->super->indexObjectLength);
		return (void *)-1;
	}

	return ptr;
}

int
dsSnapshot(const char *filename)
{
	dsCrate *crate;
	struct stat statBuffer;
	int fd;

	if ((crate = getActiveCrate()) == NULL) {
		dsLog("Can't get active crate.\n");
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
		dsLog("Can't open %s: %s\n", filename, strerror(errno));
		return -1;
	}

	if (fstat(crate->fd, &statBuffer) < 0) {
		dsLog("Can't fstat(%s,): %s\n", filename, strerror(errno));
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

	dsLog("Bob is your uncle: %" PRIu64 ", %" PRIu64 "\n", start, end);

	while (start < end) {
		filemap->fm_start = start;
		filemap->fm_length = end;
		filemap->fm_flags = FIEMAP_FLAG_SYNC;
		filemap->fm_extent_count = MAX_EXTENT;

		if (ioctl(crate->fd, FS_IOC_FIEMAP, filemap) != 0) {
			dsLog("Can't ioctl(%d, FS_IOC_FIEMAP,): %s\n",
				crate->fd, strerror(errno));
			return -1;
		}

		if (filemap->fm_mapped_extents == 0) {
			break;
		}

		dsLog("FILE: # of extents=%" PRIu32 ", flags=%" PRIu32 "\n",
			filemap->fm_mapped_extents, filemap->fm_flags);

		for (i = 0; i < filemap->fm_mapped_extents; i++) {
			struct fiemap_extent extent;
			extent = filemap->fm_extents[i];

			dsLog("Extent: %5d logical=%lld, phy=%lld, len=%lld, flags=%"
				PRIX32 "\n", count, extent.fe_logical,
				extent.fe_physical, extent.fe_length, extent.fe_flags);

			void *buf = crate->map.ptr + extent.fe_logical;

			if (pwrite(fd, buf, extent.fe_length, extent.fe_logical) < 0) {
				dsLog("Can't pwrite(%d,,%lld,%lld): %s\n", fd, extent.fe_length,
					extent.fe_logical, strerror(errno));
				return -1;
			}

			count++;
			start = extent.fe_logical + extent.fe_length;
		}
	}

	free(filemap);
	close(fd);
	return 0;
}

int
dsSync(int block)
{
	dsCrate *crate;

	if ((crate = getActiveCrate()) == NULL) {
		dsLog("Can't get active crate.\n");
		return -1;
	}

	int flags = block ? MS_SYNC : MS_ASYNC;

	if (msync(crate->map.ptr, crate->map.length, flags) < 0) {
		dsLog("Can't synchronize crate.\n");
		return -1;
	}

	return 0;
}


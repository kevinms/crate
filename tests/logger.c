#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <pthread.h>

#include <crate.h>
#include <list.h>

void
logPerThread(void *userPtr, const char *format, va_list args)
{
	/*
	 * Log the thread ID with the log message.
	 */
	fprintf(stderr, "Thread(%d): ", (int)syscall(SYS_gettid));
	vfprintf(stderr, format, args);
}

static void *
readerThread(void *arg)
{
	dsCrate *crate = dsOpen("myCrate", 0, 1);

	dsList *list = dsGetIndex();

	dsListEntry *e;
	for (e = dsListBegin(list); e != NULL; e = dsListNext(e)) {
		int *data = dsListData(e);
	}

	return(NULL);
}

int main()
{
	dsCrate *crate = dsOpen("myCrate", 1, 1);

	dsList *list = dsListAlloc();

	dsSetIndex(list, sizeof(*list));

	int i;
	for (i = 0; i < 10; i++) {
		int *data = dsAlloc(sizeof(*data));
		*data = i;
		dsListAdd(list, data);
	}

	dsClose(&crate);

	/*
	 * Setup per-thread logging.
	 */
	dsLogger(logPerThread, NULL);

	/*
	 * Spawn multiple threads to read the crate.
	 */
	int n = 3;
	pthread_t threads[n];
	for (i = 0; i < n; i++) {
		pthread_create(threads + i, NULL, readerThread, NULL);
	}

	for (i = 0; i < n; i++) {
		void *res;
		pthread_join(threads[i], &res);
	}

	return 0;
}

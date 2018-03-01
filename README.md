Crate is an extensible C library that provides a set of common data structures and a way to persist them to disk.
A desire to have python-like serialization features, i.e. ``pickle```, but in C spurred the creation of this library.
Crate attempts to fulfill this desire by having 

Crate is an extensible C library providing a set of common data structures and a way to persist them to disk.

A desire to have python-like serialization features, i.e. ``pickle```, but in a C environment spurred the creation of this library.

Crate uses a custom memory allocator along with memory mapped files to persist C data structures to disk.

List example:

```
dsObjectStore *store = dsOpen("new-store", 1, 1);

dsList *list = dsListAlloc();

int i;
for (i = 0; i < 10; i++) {
	int *data = dsAlloc(sizeof(*data));
	dsListAdd(list, data);
}

dsListEntry *e;
for (e = dsListBegin(list); e != NULL; e = dsListNext(e)) {
	int *data = dsListData(e);
	printf("%d\n", *data);
}
```

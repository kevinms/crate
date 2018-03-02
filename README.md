# Crate

Crate is an extensible C library providing a set of common data structures and a way to persist them to disk.

A desire to have python-like serialization features, i.e. ``pickle```, but in a C environment spurred the creation of this library.

---
### The Crate Interface

This libraries central concept is storing data in crates. Crates may be persisted to a file on disk at any time and then loaded from that file at any point in the future. The ```dsCrate``` structure implements this central concept with the public API in ```crate.h```.

Use ```dsOpen()``` and ```dsClose()``` to open an existing crate file, create a new one or close a crate.
```c
dsCrate *crate = dsOpen("path/to/myCrate", 1, 0);

dsClose(crate);
```

Instead of having to pass the ```dsCrate``` handle to nearly every function in the library, you set the 'active' crate once and then operate on it many times. The 'active' crate can be set using ```dsSet()```.

```c
dsSet(crate);
```

A program may have several crates open simultaneously. Since the ```dsCrate``` interface is thread-safe and the ```dsSet()``` API uses thread-local storage, each thread may set a different 'active' crate. But, there can be only one 'active' crate per-thread.

Use ```dsAlloc()``` and ```dsFree()``` to add and remove data from a create.
```c
int *data = dsAlloc(sizeof(*data));

*data = 42;

dsFree(data);
```

Using ```dsSetIndex()``` and ```dsGetIndex()```, ...
```
int *data = dsAlloc(sizeof(*data));

dsSetIndex(data, sizeof(*data));
data = dsGetIndex();
```

As data is added and removed from a crate it is asynchronously flushed to the crate file given to ```dsOpen()```. Given an undefined amount of time, all changes will "eventually" be flushed to the file. The changes may be flushed synchronously and on-demand by calling either ```dsSync()``` or ```dsClose()```.

```c
dsSync(1);
dsClose(crate);
```

Using ```dsSnapshot()``` a snapshot-in-time of a crate may be created at any time. This affectively makes a copy of a crate.
```c
dsSnapshot("path/to/snapshot");
```

---
### Data Structures

This library also provides a set of common data structures that all sit on top of the ```dsCrate``` interface. That means each data structure is persisted to a crate file and snapshots be taken.

List example:

```c
dsCrate *crate = dsOpen("path/to/myCrate", 1, 1);

dsList *list = dsListAlloc();

dsSetIndex(list, sizeof(*list));

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

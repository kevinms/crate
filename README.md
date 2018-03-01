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

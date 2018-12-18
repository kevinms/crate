#include <stdio.h>

#include <crate.h>
#include <list.h>

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

	dsListEntry *e;
	for (e = dsListBegin(list); e != NULL; e = dsListNext(e)) {
		int *data = dsListData(e);
		printf("%d\n", *data);
	}

	return 0;
}

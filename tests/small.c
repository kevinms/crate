#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "crate.h"
#include "list.h"

struct Item {
	int id;
};

#include "crate_internal.h"

int main()
{
	dsCrate *crate = dsOpen("mycrate", 1, 1);

	// Create a new dsList.
	dsList *items = dsListAlloc();

	// Set the list as the crate's index.
	dsSetIndex(items, sizeof(*items));

	// Add items to the list.
	int i;
	for (i = 0; i < 10; i++) {
		struct Item *item = dsAlloc(sizeof(*item));
		item->id = i;

		dsListAdd(items, item);
	}

	// Take a snapshot of the crate.
	dsSnapshot("mycrate-snapshot");

	// Close the crate.
	dsClose(&crate);


	// Open the snapshot.
	crate = dsOpen("mycrate-snapshot", 0, 1);

	// Get the crate's index.
	items = dsGetIndex();

	// Iterate over list.
	dsListEntry *entry;
	for (entry = dsListBegin(items); entry != NULL; entry = dsListNext(entry)) {
		struct Item *item = dsListData(entry);
		printf("  Item: %d\n", item->id);
	}

	return 0;
}

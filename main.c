#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "crate.h"

struct Player {
	char name[4096];
	dsList deck;
	dsList tweets;
};

struct Card {
	int type;
	int color;
	char name[256];
	int power;
	int toughness;
};

struct Tweet {
	int a, b, c, d;
};

void printCards(dsList *deck)
{
	struct Card *card;
	dsListEntry *listEntry;

	printf("Deck:\n");
	for (listEntry = dsListBegin(deck);
		 listEntry != NULL;
		 listEntry = dsListNext(listEntry)) {
		card = dsListData(listEntry);
		printf("  Card: %s\n", card->name);
	}
}

int main()
{
	struct Player *player;
	struct Card *card;

	dsObjectStore *store = dsOpen("bob", 1, 1);
	dsSet(store);

	player = dsAlloc(sizeof(*player));
	memset(player, 0, sizeof(*player));

	strcpy(player->name, "Kilvin");
	dsListInit(&player->deck);

	card = dsAlloc(sizeof(*card));
	strcpy(card->name, "Kvothe");
	dsListAdd(&player->deck, card);

	card = dsAlloc(sizeof(*card));
	strcpy(card->name, "Elodin");
	dsListAdd(&player->deck, card);

	dsListInit(&player->tweets);
	int i;
	struct Tweet *tweet;
	//for (i = 0; i < 10000000; i++) {
	//for (i = 0; i < 90000; i++) {
	for (i = 0; i < 10; i++) {
		tweet = dsAlloc(sizeof(*tweet));
		dsListAdd(&player->tweets, tweet);
	}
	dsListEntry *listEntry;
	i = 0;
	for (listEntry = dsListBegin(&player->tweets);
		 listEntry != NULL;
		 listEntry = dsListNext(listEntry)) {
		tweet = dsListData(listEntry);
		i++;
	}
	printf("Tweet count: %d\n", i);

	struct timeval t1, t2;
	double elapsedTime;
	int k = 1;
	for (i = 0; i < k; i++) {
		gettimeofday(&t1, NULL);
		dsSnapshot("fred");
		gettimeofday(&t2, NULL);
		elapsedTime += (t2.tv_sec - t1.tv_sec) * 1000.0;
		elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000.0;
	}
	printf("%lf ms\n", elapsedTime/k);

	printCards(&player->deck);
	dsListDel(&player->deck, card);
	printCards(&player->deck);
	dsListDel(&player->deck, dsListData(dsListBegin(&player->deck)));
	printCards(&player->deck);

	return(0);
}

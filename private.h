#ifndef __PRIVATE_H
#define __PRIVATE_H

#define log(fmt, ...) fprintf(stderr, "%s(): " fmt, __func__, ##__VA_ARGS__)
#if 0
#define debug(fmt, ...) fprintf(stderr, "%s(): " fmt, __func__, ##__VA_ARGS__)
#else
#define debug(fmt, ...)
#endif

/*
 * Structures used internally by the object store library.
 */
#define MAGIC_LIB_SUPER     *(uint64_t *)"objSuper"

/*
 * Structures built on top of the object store.
 */
#define MAGIC_LIST       *(uint64_t *)"listObj"
#define MAGIC_LISTENTRY  *(uint64_t *)"listEnty"

#endif /* __PRIVATE_H */

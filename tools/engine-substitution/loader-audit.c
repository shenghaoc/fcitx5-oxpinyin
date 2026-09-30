#define _GNU_SOURCE
#include <link.h>
#include <stdio.h>
#include <string.h>
unsigned int la_version(unsigned int version) { return version; }
unsigned int la_objopen(struct link_map *map, Lmid_t id, uintptr_t *cookie) {
    (void)id;
    (void)cookie;
    if (strstr(map->l_name, "libpinyin.so") ||
        strstr(map->l_name, "/oxpinyin.so"))
        fprintf(stderr, "SUBSTITUTION_MAP %s\n", map->l_name);
    return 0;
}

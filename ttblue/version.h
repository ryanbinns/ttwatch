#ifndef __VERSION_H__
#define __VERSION_H__

#define VERSION_TUPLE(...) ((struct version_tuple){ .len=(sizeof((int[]){__VA_ARGS__})/sizeof(int)), .tuple={__VA_ARGS__} })
struct version_tuple { int len; int tuple[4]; };

int parse_version(const char *s, struct version_tuple *v, char *seps);
int compare_versions(struct version_tuple *v1, struct version_tuple *v2);
const char *str_version(struct version_tuple *v, char sep);

#endif /* #ifndef __VERSION_H__ */

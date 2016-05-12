#include <stdio.h>
#include <string.h>
#include "version.h"

int
parse_version(const char *s, struct version_tuple *v, char *seps) {
    int count, len;
    char sep;
    for (v->len = 0; *s; s+=len) {
        count = sscanf(s, "%d%c%n", v->tuple + (v->len++), &sep, &len);
        if (count==0 || (count==2 && !strchr(seps, sep)))
            return -1;
    }
    return v->len;
}

const char *
str_version(struct version_tuple *v, char sep) {
    static char buf[100]; //FIXME
    char *s = buf;
    for (int ii=0; ii<v->len; ii++)
        s += sprintf(s, "%d%c", v->tuple[ii], sep);
    s[-1] = '\0';
    return buf;
}

int
compare_versions(struct version_tuple *v1, struct version_tuple *v2) {
    int ii;
    for (ii=0; ii<v1->len && ii<v2->len; ii++) {
        if (v1->tuple[ii] != v2->tuple[ii])
            return v1->tuple[ii] - v2->tuple[ii];
    }
    if (ii<v1->len)
        return 1;
    else if (ii<v2->len)
        return 2;
    else
        return 0;
}

#ifdef VERSION_TEST
int main(int argc, char **argv) {

    struct version_tuple older = VERSION_TUPLE(1,8,34), newer = VERSION_TUPLE(1,8,46);
    const char *old_s="1.8.34", *new_s="1.8.46";

    const char *vs = "1.8.37";

    struct version_tuple version = {0};
    parse_version(vs, &version, ".");
    
    printf("%s cmp %s = %d\n", vs, str_version(&older,'.'), compare_versions(&version, &older));
    printf("%s cmp %s = %d\n", vs, str_version(&newer,'.'), compare_versions(&version, &newer));
    printf("%s cmp %s = %d\n", old_s, new_s, compare_versions(&older, &newer));
    printf("%s cmp %s = %d\n", new_s, old_s, compare_versions(&newer, &older));

    vs = "1.8.46.0";
    parse_version(vs, &version, ".");
    
    printf("%s cmp %s = %d\n", vs, str_version(&older,'.'), compare_versions(&version, &older));
    printf("%s cmp %s = %d\n", vs, str_version(&newer,'.'), compare_versions(&version, &newer));
    return 0;
}
#endif

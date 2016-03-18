#ifndef __UTIL_H__
#define __UTIL_H__

__attribute__ ((format (printf, 1, 2)))
void term_title(const char *fmt, ...);

int isleep(int seconds, int verbose);

#endif /* __UTIL_H__ */

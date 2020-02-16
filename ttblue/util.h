#ifndef __UTIL_H__
#define __UTIL_H__

#define BARRAY(...) (const uint8_t[]){ __VA_ARGS__ }

__attribute__ ((format (printf, 1, 2)))
void term_title(const char *fmt, ...);

int isleep(int seconds, int verbose);

uint32_t crc16(const uint8_t *buf, size_t len, uint32_t start);
void hexlify(FILE *where, const uint8_t *buf, size_t len, bool newl);

#endif /* __UTIL_H__ */

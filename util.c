#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>

__attribute__ ((format (printf, 1, 2)))
void
term_title(const char *fmt, ...)
{
    if (isatty(1)) {
        va_list va;
        va_start(va, fmt);

        fputs("\033]0;", stdout);
        vfprintf(stdout, fmt, va);
        fputc('\007', stdout);
        fflush(stdout);
    }
}

/****************************************************************************/

// This is a weird workaround for the non-realtime-awareness of sleep(3):
// http://stackoverflow.com/questions/32152276/real-time-aware-sleep-call

static void
nullhandler(int signal) {}

int
isleep(int seconds, int verbose)
{
    if (verbose) {
        fprintf(stderr, "Sleeping for %d seconds...", seconds);
        fflush(stderr);
    }
    signal(SIGALRM, nullhandler);

    int res=0, elapsed=0;
    for (time_t t=time(NULL); (elapsed<seconds) && (res<=0); elapsed=time(NULL)-t)
        res = sleep((seconds-elapsed > 30) ? 30 : seconds-elapsed);

    signal(SIGALRM, SIG_IGN);
    if (verbose)
        fputs(res ? " woken by signal!\n\n" : "\n", stderr);
    return (res>0);
}

uint32_t
crc16(const uint8_t *buf, size_t len, uint32_t start)
{
    uint32_t crc = start;		        // should be 0xFFFF first time
    for (size_t pos = 0; pos < len; pos++) {
        crc ^= (uint32_t)buf[pos];          // XOR byte into least sig. byte of crc

        for (int i = 8; i != 0; i--) {  // Loop over each bit
            if ((crc & 0x0001) != 0) {  // If the LSB is set
                crc >>= 1;              // Shift right and XOR 0xA001
                crc ^= 0xA001;
            }
            else                        // Else LSB is not set
                crc >>= 1;              // Just shift right
        }
    }
    return crc;
}

void
hexlify(FILE *where, const uint8_t *buf, size_t len, bool newl)
{
    while (len--) {
        fprintf(where, "%2.2x", (int)*buf++);
    }
    if (newl)
        fputc('\n', where);
}

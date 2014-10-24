/*****************************************************************************\
** log.c                                                                     **
** Logging functions                                                         **
\*****************************************************************************/

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int log_location = LOG_CONSOLE;

void write_log(int error, const char *fmt, ...)
{
    static int print_time = 1;
    va_list va;
    va_start(va, fmt);

    if (log_location == LOG_CONSOLE)
        vfprintf(error ? stderr : stdout, fmt, va);
    else
    {
        FILE *file = fopen("/var/log/ttwatch/ttwatch.log", "a");
        if (file)
        {
            if (print_time)
            {
                time_t tt;
                static char buf[64];

                tt = time(NULL);
                strftime(buf, sizeof(buf), "%c", localtime(&tt));

                fprintf(file, "%s: ", buf);
            }
            print_time = (fmt[strlen(fmt) - 1] == '\n');
            vfprintf(file, fmt, va);
            fclose(file);
        }
    }

    va_end(va);
}

void set_log_location(int location)
{
    log_location = (location != LOG_CONSOLE);
}


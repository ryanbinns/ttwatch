/*****************************************************************************\
** export.h                                                                  **
** Export definitions and common export helper functions                     **
\*****************************************************************************/

#ifndef __EXPORT_H__
#define __EXPORT_H__

#include "libttwatch.h"
#include "log.h"
#include "ttbin.h"
#include "protobuf.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define OFFLINE_FORMAT_CSV  (0x00000001)
#define OFFLINE_FORMAT_FIT  (0x00000002)
#define OFFLINE_FORMAT_GPX  (0x00000004)
#define OFFLINE_FORMAT_KML  (0x00000008)
#define OFFLINE_FORMAT_PWX  (0x00000010)
#define OFFLINE_FORMAT_TCX  (0x00000020)

typedef struct
{
    uint32_t mask;
    const char *name;
    int gps_ok;
    int treadmill_ok;
    int pool_swim_ok;
    int indoor_ok;
    void (*producer)(TTBIN_FILE* ttbin, FILE *file);
    void (*protobuf_producer)(PROTOBUF_FILE* ttbin, FILE *file);
} OFFLINE_FORMAT;

#define OFFLINE_FORMAT_COUNT    (6)
extern const OFFLINE_FORMAT OFFLINE_FORMATS[OFFLINE_FORMAT_COUNT];

void export_csv(TTBIN_FILE *ttbin, FILE *file);

void export_gpx(TTBIN_FILE *ttbin, FILE *file);

void export_kml(TTBIN_FILE *ttbin, FILE *file);

void export_tcx(TTBIN_FILE *ttbin, FILE *file);

void export_protobuf_csv(PROTOBUF_FILE *protobuf, FILE *file);

uint32_t export_formats(TTBIN_FILE *ttbin, uint32_t formats);
uint32_t export_protobuf_formats(PROTOBUF_FILE *protobuf, uint32_t formats);

uint32_t parse_format_list(const char *formats);
uint32_t get_configured_formats(TTWATCH *watch);

#endif  /* __EXPORT_H__ */


/*****************************************************************************\
** messages.h                                                                **
** message structure definitions                                             **
\*****************************************************************************/

#include <stdint.h>

/* file IDs */
#define FILE_SYSTEM_FIRMWARE        (0x000000f0)
#define FILE_GPSQUICKFIX_DATA       (0x00010100)
#define FILE_GPS_FIRMWARE           (0x00010200)
#define FILE_FIRMWARE_UPDATE_LOG    (0x00013001)
#define FILE_MANIFEST1              (0x00850000)
#define FILE_MANIFEST2              (0x00850001)
#define FILE_PREFERENCES_XML        (0x00f20000)

#define FILE_TYPE_MASK              (0xffff0000)
#define FILE_RACE_DATA              (0x00710000)
#define FILE_RACE_HISTORY_DATA      (0x00720000)
#define FILE_HISTORY_DATA           (0x00730000)
#define FILE_HISTORY_SUMMARY        (0x00830000)
#define FILE_TTBIN_DATA             (0x00910000)

typedef struct __attribute__((packed))
{
    uint16_t entry;
    uint32_t value;
} TT_MANIFEST_ENTRY;

typedef struct __attribute__((packed))
{
    uint16_t file_type;
    uint16_t entry_count;
    TT_MANIFEST_ENTRY entries[0];
} TT_MANIFEST_FILE;

#define TT_MANIFEST_ENTRY_UTC_OFFSET    (169)

typedef struct __attribute__((packed))
{
    char     name[16];
    uint32_t _unk1[5];
    uint8_t  _unk2;
    uint8_t  _unk3;
    uint32_t checkpoints;
    uint32_t time;
    uint32_t distance;
    uint8_t  distances[0];
} TT_RACE_FILE;

typedef struct __attribute__((packed))
{
    uint32_t index;
    uint8_t  activity;
    uint32_t _unk1[2];
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t _unk2[2];
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
    uint32_t _unk3[2];
    uint32_t duration;
    float    distance;
    uint32_t calories;
    uint32_t file_id;           // does not exist for swim entries
    uint32_t swolf;             // only exists for swim entries
    uint32_t strokes_per_lap;   // only exists for swim entries
} TT_HISTORY_ENTRY;

typedef struct __attribute__((packed))
{
    uint32_t _unk1;
    uint16_t activity;
    uint16_t entry_length;
    uint16_t entry_count;
    uint16_t _unk2;
    uint8_t  data[0];   // cannot be TT_HISTORY_ENTRY's because they are variable size...
} TT_HISTORY_FILE;

/*****************************************************************************/

#include "manifest_definitions.h"

struct
{
    uint32_t version;
    uint32_t count;
    struct MANIFEST_DEFINITION **definitions;
} MANIFEST_DEFINITIONS[] =
{
    { 0x00010819, MANIFEST_DEFINITION_00010819_COUNT, MANIFEST_DEFINITIONS_00010819 },
    { 0x00010822, MANIFEST_DEFINITION_00010819_COUNT, MANIFEST_DEFINITIONS_00010819 },
};

#define MANIFEST_DEFINITION_COUNT (2)


#ifndef __TTBLUE_H__
#define __TTBLUE_H__
/*****************************************************************************/

/******************************************************************************
* Messages                                                                    *
******************************************************************************/
#define MSG_OPEN_FILE_WRITE         (0x02)
#define MSG_WRITE                   (0x00)
#define MSG_READ                    (0x01)
#define MSG_CANCEL_TRANSFER         (0x02)
#define MSG_LIST_FILES              (0x03)
#define MSG_DELETE                  (0x04)
#define MSG_UPDATE_EPHEMERIS        (0x05) // Used on the quickgps file
#define MSG_UPDATE_GOLF_MAPS        (0x06) // No idea how this one works
#define MSG_RESET_DEVICE            (0x07)
#define MSG_SET_TIME                (0x08)
#define MSG_UIPROD                  (0x09) // Used on the rest_proto_file and for phone notifications
#define MSG_WRITE_RECOVERABLE       (0x0a) // Used on V2 devices instead of MSG_WRITE
#define MSG_READ_RECOVERABLE        (0x0b) // Used on V2 devices instead of MSG_READ

/******************************************************************************
* File IDs                                                                    *
******************************************************************************/
#define TTBLUE_FILE_NOTIFICATION            (0x00b50000) // for text messages, phone calls
#define TTBLUE_FILE_GPS_STATUS              (0x00020001)
#define TTBLUE_FILE_HOSTNAME1               (0x00020002) // Hostname on V1 devices
#define TTBLUE_FILE_HOSTNAME2               (0x00020003) // Hostname on V2 devices (offical
                                                         // tomtom app only sets this one)
#define TTBLUE_FILE_GOLF_MANIFEST           (0x00b00000)
#define TTBLUE_FILE_STEP_BUCKET             (0x00b10000)
#define TTBLUE_FILE_GOLF_SCORECARDS         (0x00940000) // Also called GOLF_ROUNDS?
#define TTBLUE_FILE_REST_PROTO_FILE         (0x00890000) // Only file uiprod command is used
                                                         // on except notification files
#define TTBLUE_FILE_FIRMWARE_CHUNK          (0x00fd0000) // Used when doing OTA updates?
#define TTBLUE_FILE_BRIDGEHEAD              (0x00000000) // ??
#define TTBLUE_FILE_MANIFEST1               (0x00850000)
#define TTBLUE_FILE_PREFERENCES_XML         (0x00f20000)
#define TTBLUE_FILE_TTBIN_DATA              (0x00910000)
#define TTBLUE_FILE_GPSQUICKFIX_DATA        (0x00010100)

#endif  // __TTBLUE_H__

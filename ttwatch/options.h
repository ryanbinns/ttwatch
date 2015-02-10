/******************************************************************************\
** options.h                                                                  **
** Interface of basic config file loading                                     **
\******************************************************************************/

#include <stdint.h>

typedef struct
{
    int update_firmware;
    int update_gps;
#ifdef UNSAFE
    int list_files;
    int read_file;
    int write_file;
    uint32_t file_id;
#endif
    int select_device;
    char *device;
    int show_versions;
    int list_devices;
    int get_time;
    int set_time;
    int get_activities;
    int get_name;
    int set_name;
    char *watch_name;
    int list_formats;
    int set_formats;
    uint32_t formats;
    int daemon_mode;
    int run_as;
    char *run_as_user;
#ifdef UNSAFE
    char *file;
#endif
    char *activity_store;
    int list_races;
    int update_race;
    char *race;
    int list_history;
    int delete_history;
    char *history_entry;
    int clear_data;
    int display_settings;
    int setting;
    char *setting_spec;
    int list_settings;
    int skip_elevation;
    char *post_processor;
} OPTIONS;

/*****************************************************************************/
typedef enum
{
    LoadAll,
    LoadSettingsOnly,
    LoadDaemonOperations
} ConfLoadType;

void load_conf_file(const char *filename, OPTIONS *options, ConfLoadType type);

OPTIONS *alloc_options();
OPTIONS *copy_options(const OPTIONS *o);
void free_options(OPTIONS *o);


//------------------------------------------------------------------------------
// libttwatch_cpp.cpp
//
//------------------------------------------------------------------------------

#include "libttwatch.hpp"

namespace ttwatch
{

#define RET_LOG_ERROR(watch, err) return ((watch)->m_last_error = (err)) == TTWATCH_NoError

typedef struct
{
    WatchEnumerator enumerator;
    void *data;
} EnumerateDevicesData;

//------------------------------------------------------------------------------
// File implementation
File::File(Watch *watch, TTWATCH_FILE *file)
    : m_watch(watch), m_file(file)
{
}

//------------------------------------------------------------------------------
File::~File()
{
    if (m_file)
        close();
}

//------------------------------------------------------------------------------
uint32_t File::id() const
{
    return m_file ? m_file->file_id : 0;
}

//------------------------------------------------------------------------------
bool File::close()
{
    if ((m_watch->m_last_error = ttwatch_close_file(m_file)) == TTWATCH_NoError)
    {
        m_file = 0;
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
uint32_t File::length() const
{
    uint32_t length;
    if ((m_watch->m_last_error = ttwatch_get_file_size(m_file, &length)) == TTWATCH_NoError)
        return length;
    return 0;
}

//------------------------------------------------------------------------------
bool File::read(uint8_t *data, uint32_t length) const
{
    RET_LOG_ERROR(m_watch, ttwatch_read_file_data(m_file, data, length));
}

//------------------------------------------------------------------------------
bool File::write(const uint8_t *data, uint32_t length)
{
    RET_LOG_ERROR(m_watch, ttwatch_write_file_data(m_file, data, length));
}

//------------------------------------------------------------------------------
// PreferencesFile implementation
PreferencesFile::PreferencesFile(Watch *watch)
    : m_watch(watch)
{
}

//------------------------------------------------------------------------------
PreferencesFile::~PreferencesFile()
{
}

//------------------------------------------------------------------------------
bool PreferencesFile::reload()
{
    RET_LOG_ERROR(m_watch, ttwatch_reload_preferences(m_watch->m_watch));
}

//------------------------------------------------------------------------------
bool PreferencesFile::write() const
{
    RET_LOG_ERROR(m_watch, ttwatch_write_preferences(m_watch->m_watch));
}

//------------------------------------------------------------------------------
bool PreferencesFile::updateModifiedTime()
{
    RET_LOG_ERROR(m_watch, ttwatch_update_preferences_modified_time(m_watch->m_watch));
}

//------------------------------------------------------------------------------
std::string PreferencesFile::watchName() const
{
    char name[64];
    if ((m_watch->m_last_error = ttwatch_get_watch_name(m_watch->m_watch, name, sizeof(name))) == TTWATCH_NoError)
        return std::string(name);
    return std::string();
}

//------------------------------------------------------------------------------
bool PreferencesFile::setWatchName(std::string name)
{
    RET_LOG_ERROR(m_watch, ttwatch_set_watch_name(m_watch->m_watch, name.c_str()));
}

//------------------------------------------------------------------------------
static void offlineFormatEnumerator(const char *id, int auto_open, void *data)
{
    std::vector<PreferencesFile::OfflineFormat> *formats =
        (std::vector<PreferencesFile::OfflineFormat>*)data;
    PreferencesFile::OfflineFormat format = { id, auto_open != 0 };
    formats->push_back(format);
}
std::vector<PreferencesFile::OfflineFormat> PreferencesFile::offlineFormats() const
{
    std::vector<OfflineFormat> formats;
    m_watch->m_last_error = ttwatch_enumerate_offline_formats(m_watch->m_watch,
        offlineFormatEnumerator, &formats);
    return formats;
}

//------------------------------------------------------------------------------
bool PreferencesFile::addOfflineFormat(std::string id, bool auto_open)
{
    RET_LOG_ERROR(m_watch, ttwatch_add_offline_format(m_watch->m_watch, id.c_str(), auto_open ? 1 : 0));
}

//------------------------------------------------------------------------------
bool PreferencesFile::removeOfflineFormat(std::string id)
{
    RET_LOG_ERROR(m_watch, ttwatch_remove_offline_format(m_watch->m_watch, id.c_str()));
}

//------------------------------------------------------------------------------
ManifestFile::ManifestFile(Watch *watch)
    : m_watch(watch)
{
}

//------------------------------------------------------------------------------
ManifestFile::~ManifestFile()
{
}

//------------------------------------------------------------------------------
bool ManifestFile::reload()
{
    RET_LOG_ERROR(m_watch, ttwatch_reload_manifest(m_watch->m_watch));
}

//------------------------------------------------------------------------------
bool ManifestFile::write() const
{
    RET_LOG_ERROR(m_watch, ttwatch_write_manifest(m_watch->m_watch));
}

//------------------------------------------------------------------------------
size_t ManifestFile::entryCount() const
{
    uint16_t entry_count;
    if ((m_watch->m_last_error = ttwatch_get_manifest_entry_count(m_watch->m_watch, &entry_count)) != TTWATCH_NoError)
        return 0;
    return entry_count;
}

//------------------------------------------------------------------------------
bool ManifestFile::getEntry(uint16_t entry, uint32_t *value) const
{
    RET_LOG_ERROR(m_watch, ttwatch_get_manifest_entry(m_watch->m_watch, entry, value));
}

//------------------------------------------------------------------------------
bool ManifestFile::setEntry(uint16_t entry, const uint32_t *value)
{
    RET_LOG_ERROR(m_watch, ttwatch_set_manifest_entry(m_watch->m_watch, entry, value));
}

//------------------------------------------------------------------------------
uint32_t ManifestFile::operator [](uint16_t entry) const
{
    uint32_t value;
    if (!getEntry(entry, &value))
        return 0;
    return value;
}

//------------------------------------------------------------------------------
Watch::Watch()
    : m_watch(0)
{
}

//------------------------------------------------------------------------------
Watch::~Watch()
{
    if (m_watch)
        close();
}

//------------------------------------------------------------------------------
void Watch::enumerateDevices(WatchEnumerator enumerator, void *data)
{
    EnumerateDevicesData cbdata = { enumerator, data };
    ttwatch_enumerate_devices(enumerateDevicesCallback, &cbdata);
}

//------------------------------------------------------------------------------
bool Watch::open()
{
    RET_LOG_ERROR(this, ttwatch_open(0, &m_watch));
}

//------------------------------------------------------------------------------
bool Watch::open(std::string serial_or_name)
{
    RET_LOG_ERROR(this, ttwatch_open(serial_or_name.c_str(), &m_watch));
}

//------------------------------------------------------------------------------
void Watch::close()
{
    if ((m_last_error = ttwatch_close(m_watch)) == TTWATCH_NoError)
        m_watch = 0;
}

//------------------------------------------------------------------------------
int Watch::lastError() const
{
    return m_last_error;
}

//------------------------------------------------------------------------------
// file handling
File *Watch::openFile(uint32_t file_id, bool read)
{
    TTWATCH_FILE *file;
    if ((m_last_error = ttwatch_open_file(m_watch, file_id, read ? 1 : 0, &file)) != TTWATCH_NoError)
        return 0;
    return new File(this, file);
}

//------------------------------------------------------------------------------
bool Watch::deleteFile(uint32_t file_id)
{
    RET_LOG_ERROR(this, ttwatch_delete_file(m_watch, file_id));
}

//------------------------------------------------------------------------------
bool Watch::readWholeFile(uint32_t file_id, void **data, uint32_t *length)
{
    RET_LOG_ERROR(this, ttwatch_read_whole_file(m_watch, file_id, data, length));
}

//------------------------------------------------------------------------------
bool Watch::writeWholeFile(uint32_t file_id, const void *data, uint32_t length) const
{
    RET_LOG_ERROR(this, ttwatch_write_whole_file(m_watch, file_id, data, length));
}

//------------------------------------------------------------------------------
bool Watch::writeVerifyWholeFile(uint32_t file_id, const void *data, uint32_t length) const
{
    RET_LOG_ERROR(this, ttwatch_write_verify_whole_file(m_watch, file_id, data, length));
}

//------------------------------------------------------------------------------
bool Watch::enumerateFiles(TTWATCH_FILE_ENUMERATOR enumerator, uint32_t type, void *data) const
{
    RET_LOG_ERROR(this, ttwatch_enumerate_files(m_watch, type, enumerator, data));
}

//------------------------------------------------------------------------------
// file enumeration
bool Watch::findFirstFile(uint32_t *file_id, uint32_t *length) const
{
    RET_LOG_ERROR(this, ttwatch_find_first_file(m_watch, file_id, length));
}

//------------------------------------------------------------------------------
bool Watch::findNextFile(uint32_t *file_id, uint32_t *length) const
{
    RET_LOG_ERROR(this, ttwatch_find_next_file(m_watch, file_id, length));
}

//------------------------------------------------------------------------------
bool Watch::findClose()
{
    RET_LOG_ERROR(this, ttwatch_find_close(m_watch));
}

//------------------------------------------------------------------------------
// general messages
bool Watch::resetGpsProcessor()
{
    RET_LOG_ERROR(this, ttwatch_reset_gps_processor(m_watch));
}

//------------------------------------------------------------------------------
bool Watch::resetWatch()
{
    RET_LOG_ERROR(this, ttwatch_reset_watch(m_watch));
}

//------------------------------------------------------------------------------
time_t Watch::getWatchTime() const
{
    time_t timestamp;
    if ((m_last_error = ttwatch_get_watch_time(m_watch, &timestamp)) != TTWATCH_NoError)
        return 0;
    return timestamp;
}

//------------------------------------------------------------------------------
std::string Watch::serialNumber() const
{
    return std::string(m_watch->serial_number);
}

//------------------------------------------------------------------------------
uint32_t Watch::productId() const
{
    return m_watch->product_id;
}

//------------------------------------------------------------------------------
uint32_t Watch::firmwareVersion() const
{
    return m_watch->firmware_version;
}

//------------------------------------------------------------------------------
uint32_t Watch::bleVersion() const
{
    return m_watch->ble_version;
}

//------------------------------------------------------------------------------
bool Watch::sendMessageGroup1()
{
    RET_LOG_ERROR(this, ttwatch_send_message_group_1(m_watch));
}

//------------------------------------------------------------------------------
PreferencesFile *Watch::preferences()
{
    if (!m_watch->preferences_file)
    {
        if ((m_last_error = ttwatch_reload_preferences(m_watch)) != TTWATCH_NoError)
            return 0;
    }
    return new PreferencesFile(this);
}

//------------------------------------------------------------------------------
ManifestFile *Watch::manifest()
{
    if (!m_watch->manifest_file)
    {
        if ((m_last_error = ttwatch_reload_manifest(m_watch)) != TTWATCH_NoError)
            return 0;
    }
    return new ManifestFile(this);
}

//------------------------------------------------------------------------------
// race functions
bool Watch::enumerateRaces(TTWATCH_RACE_ENUMERATOR enumerator, void *data) const
{
    RET_LOG_ERROR(this, ttwatch_enumerate_races(m_watch, enumerator, data));
}

//------------------------------------------------------------------------------
bool Watch::updateRace(TTWATCH_ACTIVITY activity, int index,
    std::string name, uint32_t distance, uint32_t duration, int checkpoints)
{
    RET_LOG_ERROR(this, ttwatch_update_race(m_watch, activity, index,
        name.c_str(), distance, duration, checkpoints));
}

//------------------------------------------------------------------------------
// history functions
bool Watch::enumerateHistoryEntries(TTWATCH_HISTORY_ENUMERATOR enumerator, void *data) const
{
    RET_LOG_ERROR(this, ttwatch_enumerate_history_entries(m_watch, enumerator, data));
}

//------------------------------------------------------------------------------
bool Watch::deleteHistoryEntry(TTWATCH_ACTIVITY activity, int index)
{
    RET_LOG_ERROR(this, ttwatch_delete_history_entry(m_watch, activity, index));
}

//------------------------------------------------------------------------------
int Watch::enumerateDevicesCallback(TTWATCH *watch, void *data)
{
    EnumerateDevicesData &cbdata = *(EnumerateDevicesData*)data;
    Watch *w = new Watch;
    w->m_watch = watch;
    return cbdata.enumerator(w, cbdata.data) ? 1 : 0;
}

//------------------------------------------------------------------------------

}

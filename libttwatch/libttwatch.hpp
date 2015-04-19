//------------------------------------------------------------------------------
// libttwatch.hpp
// interface file for the C++ TomTom watch implementation
//------------------------------------------------------------------------------

#ifndef __LIBTTWATCH_HPP__
#define __LIBTTWATCH_HPP__

#include "libttwatch.h"

#include <string>
#include <vector>

namespace ttwatch
{

class Watch;

//------------------------------------------------------------------------------
// The methods of these classes all call the C functions defined in libttwatch.h 
// so refer to that file for function information. They return simple bool
// values instead of the TTWATCH_*** error codes. The actual error code is
// available using the Watch::lastError() method.
//
// For the Watch and File classes, the destructor automatically calls their
// respective close methods if it wasn't called already.
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
class File
{
public:
    File(Watch *watch, TTWATCH_FILE *file);
    ~File();

    uint32_t id() const;
    bool close();
    uint32_t length() const;
    bool read(uint8_t *data, uint32_t length) const;
    bool write(const uint8_t *data, uint32_t length);

private:
    Watch *m_watch;
    TTWATCH_FILE *m_file;
};

//------------------------------------------------------------------------------
class PreferencesFile
{
public:
    PreferencesFile(Watch *watch);
    ~PreferencesFile();

    bool reload();
    bool write() const;

    bool updateModifiedTime();

    std::string watchName() const;
    bool setWatchName(std::string name);

    struct OfflineFormat
    {
        std::string id;
        bool        auto_open;
    };

    std::vector<OfflineFormat> offlineFormats() const;
    bool addOfflineFormat(std::string id, bool auto_open);
    bool removeOfflineFormat(std::string id);

private:
    Watch *m_watch;
};

//------------------------------------------------------------------------------
class ManifestFile
{
public:
    ManifestFile(Watch *watch);
    ~ManifestFile();

    bool reload();
    bool write() const;

    size_t entryCount() const;

    bool getEntry(uint16_t entry, uint32_t *value) const;
    bool setEntry(uint16_t entry, const uint32_t *value);

    uint32_t operator [](uint16_t entry) const;

private:
    Watch *m_watch;
};

typedef bool (*WatchEnumerator)(Watch *watch, void *data);

//------------------------------------------------------------------------------
class Watch
{
public:
    Watch();
    ~Watch();

    static void enumerateDevices(WatchEnumerator enumerator, void *data);

    bool open();
    bool open(std::string serial_or_name);
    void close();

    int lastError() const;

    // file handling
    File *openFile(uint32_t file_id, bool read = true);
    bool deleteFile(uint32_t file_id);

    bool readWholeFile(uint32_t file_id, void **data, uint32_t *length);
    bool writeWholeFile(uint32_t file_id, const void *data, uint32_t length) const;
    bool writeVerifyWholeFile(uint32_t file_id, const void *data, uint32_t length) const;
    bool enumerateFiles(TTWATCH_FILE_ENUMERATOR enumerator, uint32_t type, void *data) const;

    // file enumeration
    bool findFirstFile(uint32_t *file_id, uint32_t *length) const;
    bool findNextFile(uint32_t *file_id, uint32_t *length) const;
    bool findClose();

    // general messages
    bool resetGpsProcessor();
    bool resetWatch();

    time_t getWatchTime() const;

    std::string serialNumber() const;
    uint32_t productId() const;
    uint32_t firmwareVersion() const;
    uint32_t bleVersion() const;

    bool sendMessageGroup1();

    PreferencesFile *preferences();

    ManifestFile *manifest();

    // race functions
    bool enumerateRaces(TTWATCH_RACE_ENUMERATOR enumerator, void *data) const;
    bool updateRace(TTWATCH_ACTIVITY activity, int index,
        std::string name, uint32_t distance, uint32_t duration, int checkpoints);

    // history functions
    bool enumerateHistoryEntries(TTWATCH_HISTORY_ENUMERATOR enumerator, void *data) const;
    bool deleteHistoryEntry(TTWATCH_ACTIVITY activity, int index);

private:
    TTWATCH *m_watch;
    mutable int m_last_error;

    static int enumerateDevicesCallback(TTWATCH *watch, void *data);

    friend File;
    friend PreferencesFile;
    friend ManifestFile;
};

}


#endif  // __LIBTTWATCH_HPP__

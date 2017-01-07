#include "cycling_cadence.h"

static uint32_t handle_overflow(uint16_t overflowable, uint16_t previous_value)
{
    if (overflowable < previous_value)
        return (uint32_t)overflowable + 0x0000FFFF;
    return overflowable;
}

static float calculate_wheel_speed(CyclingCadenceData* data, CYCLING_CADENCE_RECORD* record)
{
    uint32_t wheel_rev_prev = data->wheel_rev_prev;
    uint32_t wheel_rev = handle_overflow(record->wheel_revolutions, wheel_rev_prev);

    uint32_t wheel_rev_time_prev = data->wheel_rev_time_prev;
    uint32_t wheel_rev_time = handle_overflow(record->wheel_revolutions_time, wheel_rev_time_prev);

    float result = 0.001 *
        (1000 * data->wheel_size * (wheel_rev - wheel_rev_prev) /
            (wheel_rev_time - wheel_rev_time_prev)); //mm/s

    if (result > 35) //35 m/s = 126 km/h
        result = 0;
    return result;
}

static unsigned calculate_cadence(CyclingCadenceData* data, CYCLING_CADENCE_RECORD* record)
{
    uint32_t crank_rev_prev = data->crank_rev_prev;
    uint32_t crank_rev = handle_overflow(record->crank_revolutions, crank_rev_prev);

    uint32_t crank_rev_time_prev = data->crank_rev_time_prev;
    uint32_t crank_rev_time = handle_overflow(record->crank_revolutions_time, crank_rev_time_prev);

    unsigned result = 60000L * (crank_rev - crank_rev_prev) /
            (crank_rev_time - crank_rev_time_prev);

    if (result > 200)
        result = 0; /* bad_data */
    return result;
}

CyclingCadenceData cc_initialize(void)
{
    CyclingCadenceData result = {
        0,      //uint32_t wheel_size;
        0,      //uint16_t crank_rev_prev;
        0,      //uint16_t crank_rev_time_prev; // Time in ms
        0,      //uint16_t wheel_rev_prev;
        0,      //uint16_t wheel_rev_time_prev; // Time in ms
        0,      //uint8_t  cadence_watchdog;
        0,      //uint8_t  wheel_speed_watchdog;
        0,      //unsigned cycling_cadence;     // Cadence in rpm
        0.,     //float wheel_speed;            // Wheel speed in m/s
        false,  //bool cadence_available;
        false,   //bool wheel_speed_available;
    };

    return result;
}

void cc_set_wheel_size(CyclingCadenceData* data, WHEEL_SIZE_RECORD* record)
{
    data->wheel_size = record->wheel_size;
}

void cc_sensor_packet(CyclingCadenceData* data, CYCLING_CADENCE_RECORD* record)
{
    if (data->crank_rev_prev != record->crank_revolutions)
    {
        if (data->cadence_available)
            data->cycling_cadence = calculate_cadence(data, record);
        else
            data->cadence_available = true;
        data->crank_rev_prev      = record->crank_revolutions;
        data->crank_rev_time_prev = record->crank_revolutions_time;
        data->cadence_watchdog = 0;
    }

    if (data->wheel_rev_prev != record->wheel_revolutions && data->wheel_size > 0)
    {
        if (data->wheel_speed_available)
            data->wheel_speed = calculate_wheel_speed(data, record);
        else
            data->wheel_speed_available = true;
        data->wheel_rev_prev      = record->wheel_revolutions;
        data->wheel_rev_time_prev = record->wheel_revolutions_time;
        data->wheel_speed_watchdog = 0;
    }
}

void cc_gps_packet_tick(CyclingCadenceData* data)
{
    /* clear cadence if no new data for 5 watchdog ticks*/
    if (data->cadence_watchdog++ > 3)
    {
        data->cadence_watchdog = 0;
        data->cycling_cadence = 0;
    }

    if (data->wheel_speed_watchdog++ > 3)
    {
        data->wheel_speed_watchdog = 0;
        data->wheel_speed = 0;
    }
}



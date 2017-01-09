#ifndef CYCLING_CADENCE_H_INCLUDED
#define CYCLING_CADENCE_H_INCLUDED

#include "ttbin.h"
#include <stdbool.h>

typedef struct
{
    uint32_t wheel_size;
    uint16_t crank_rev_prev;
    uint16_t crank_rev_time_prev; // Time in ms
    uint16_t wheel_rev_prev;
    uint16_t wheel_rev_time_prev; // Time in ms
    uint8_t  cadence_watchdog;
    uint8_t  wheel_speed_watchdog;
    unsigned cycling_cadence;     // Cadence in rpm
    float wheel_speed;            // Wheel speed in m/s
    bool cadence_available;
    bool wheel_speed_available;
} CyclingCadenceData;

CyclingCadenceData cc_initialize(void);
void cc_set_wheel_size(CyclingCadenceData* data, WHEEL_SIZE_RECORD* record);
void cc_sensor_packet(CyclingCadenceData* data, CYCLING_CADENCE_RECORD* record);
void cc_gps_packet_tick(CyclingCadenceData* data);

#endif // CYCLING_CADENCE_H_INCLUDED

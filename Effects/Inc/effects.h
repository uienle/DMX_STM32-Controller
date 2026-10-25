#ifndef EFFECTS_H
#define EFFECTS_H

#include "stm32f1xx_hal.h"

#define MAX_DEVICES 10

typedef struct {
    uint16_t Address;
    uint8_t EffectID;
    uint8_t Speed;
    uint8_t Data1;
    uint8_t Data2;
    uint8_t Data3;
} DeviceChange_t;

typedef struct {
    uint16_t Address;
    uint8_t EffectID;
    uint8_t Speed;
    uint8_t R, G, B;
    uint8_t Threshold;
    double Tick;
} ActiveDeviceState_t;

// Cần xuất DMX_Buffer ra để main.c có thể truy cập và gửi UART
extern uint8_t DMX_Buffer[513];

// API Prototypes
void Update_Device_Effect(DeviceChange_t change);
void Update_Active_Effects_Loop(void);
void Clear_All_Effects(void);

#endif

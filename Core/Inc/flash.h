#ifndef FLASH_H
#define FLASH_H

#include "stm32f1xx_hal.h"

void Flash_SaveScenario(const uint8_t *scenario, uint16_t len);
void Flash_LoadScenario(uint8_t *scenario, uint16_t *len);

#endif // FLASH_H

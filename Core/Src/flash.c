#include "flash.h"
#include <string.h>

// Lưu tại trang nhớ cuối cùng của Flash để bảo vệ chương trình chính (Thích hợp cho STM32F103 có 128KB hoặc 64KB)
#define FLASH_SCENARIO_START_ADDR  ((uint32_t)0x0801FC00)

void Flash_SaveScenario(const uint8_t *scenario, uint16_t len)
{
    HAL_FLASH_Unlock();

    // Ghi đè xóa 2 trang (Tổng cộng 4KB không gian lưu trữ kịch bản)
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t PageError = 0;
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
    EraseInitStruct.PageAddress = FLASH_SCENARIO_START_ADDR;
    EraseInitStruct.NbPages = 2;

    HAL_FLASHEx_Erase(&EraseInitStruct, &PageError);

    // Ghi độ dài kịch bản đầu tiên (2 bytes)
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, FLASH_SCENARIO_START_ADDR, len);

    // Ghi dữ liệu kịch bản dạng hàng từ nửa từ (16-bit blocks)
    for (uint16_t i = 0; i < len; i += 2)
    {
        uint16_t hword = scenario[i] | (scenario[i+1] << 8);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, FLASH_SCENARIO_START_ADDR + 2 + i, hword);
    }

    HAL_FLASH_Lock();
}

void Flash_LoadScenario(uint8_t *scenario, uint16_t *len)
{
    uint16_t saved_len = *(__IO uint16_t*)FLASH_SCENARIO_START_ADDR;
    if (saved_len == 0xFFFF || saved_len > 4096 || saved_len < 4)
    {
        *len = 0;
        return; // Chưa có kịch bản cũ hoặc bị lỗi dữ liệu
    }

    *len = saved_len;
    for (uint16_t i = 0; i < saved_len; i += 2)
    {
        uint16_t val = *(__IO uint16_t*)(FLASH_SCENARIO_START_ADDR + 2 + i);
        scenario[i] = val & 0xFF;
        scenario[i+1] = (val >> 8) & 0xFF;
    }
}

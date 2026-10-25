#include "effects.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

ActiveDeviceState_t ActiveDevices[MAX_DEVICES];
uint8_t ActiveDevicesCount = 0;

// Bộ đệm kênh DMX gửi đi (Kênh 0: Start Code, Kênh 1-512: Data)
uint8_t DMX_Buffer[513] = {0};

// HÀM BỔ TRỢ: Tính toán màu Rainbow tùy chỉnh (Trắng -> R -> G -> B)
void GetRainbowColor(float h, float v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    float phase = fmodf(h, 360.0f);
    if (phase < 0) phase += 360.0f;

    int i = (int)(phase / 60.0f);
    float f = (phase / 60.0f) - i;

    // Saturation = 1.0
    float p = 0.0f;
    float q = v * (1.0f - f);
    float t = v * f;

    switch (i % 6) {
        case 0: *r = (uint8_t)(v * 255.0f); *g = (uint8_t)(t * 255.0f); *b = (uint8_t)(p * 255.0f); break;
        case 1: *r = (uint8_t)(q * 255.0f); *g = (uint8_t)(v * 255.0f); *b = (uint8_t)(p * 255.0f); break;
        case 2: *r = (uint8_t)(p * 255.0f); *g = (uint8_t)(v * 255.0f); *b = (uint8_t)(t * 255.0f); break;
        case 3: *r = (uint8_t)(p * 255.0f); *g = (uint8_t)(q * 255.0f); *b = (uint8_t)(v * 255.0f); break;
        case 4: *r = (uint8_t)(t * 255.0f); *g = (uint8_t)(p * 255.0f); *b = (uint8_t)(v * 255.0f); break;
        default: *r = (uint8_t)(v * 255.0f); *g = (uint8_t)(p * 255.0f); *b = (uint8_t)(q * 255.0f); break;
    }
}

// HÀM BỔ TRỢ: Hiệu chuẩn màu sắc (Đã bỏ theo yêu cầu, giữ nguyên RGB)
void Apply_Color_Correction(uint8_t r_in, uint8_t g_in, uint8_t b_in, uint8_t *r_out, uint8_t *g_out, uint8_t *b_out)
{
    *r_out = r_in;
    *g_out = g_in;
    *b_out = b_in;
}

void Update_Device_Effect(DeviceChange_t change)
{
    int found_idx = -1;
    for (uint8_t i = 0; i < ActiveDevicesCount; i++) {
        if (ActiveDevices[i].Address == change.Address) {
            found_idx = i;
            break;
        }
    }

    if (found_idx == -1) {
        if (ActiveDevicesCount < MAX_DEVICES) {
            found_idx = ActiveDevicesCount;
            ActiveDevicesCount++;
        } else {
            return; // Đầy danh sách
        }
    }

    ActiveDevices[found_idx].Address = change.Address;
    ActiveDevices[found_idx].EffectID = change.EffectID;
    ActiveDevices[found_idx].Speed = change.Speed;

    // EffectID >= 13 (Motor) sử dụng Data1 làm Threshold/Góc, ID <= 12 (LED) dùng R, G, B
    if (change.EffectID >= 13) {
        ActiveDevices[found_idx].Threshold = change.Data1;
        ActiveDevices[found_idx].R = 0;
        ActiveDevices[found_idx].G = 0;
        ActiveDevices[found_idx].B = 0;
    } else {
        ActiveDevices[found_idx].R = change.Data1;
        ActiveDevices[found_idx].G = change.Data2;
        ActiveDevices[found_idx].B = change.Data3;
        ActiveDevices[found_idx].Threshold = 0;
    }

    ActiveDevices[found_idx].Tick = 0;
}

void Clear_All_Effects(void)
{
    ActiveDevicesCount = 0;
    for (uint16_t i = 0; i <= 512; i++) {
        DMX_Buffer[i] = 0;
    }
}

void Update_Active_Effects_Loop(void)
{
    static uint32_t last_time = 0;
    uint32_t now = HAL_GetTick();
    if (last_time == 0) {
        last_time = now;
    }
    uint32_t delta_ms = now - last_time;
    last_time = now;
    if (delta_ms > 200) {
        delta_ms = 25; // Giới hạn khi khởi động lại hoặc có ngắt quãng lớn
    }

    // DMX Start Code luôn bằng 0
    DMX_Buffer[0] = 0;

    // Đếm tổng số thiết bị LED và MOTOR thực sự hoạt động trong kịch bản để đồng bộ công thức
    int num_leds = 0;
    int num_motors = 0;
    for (uint8_t i = 0; i < ActiveDevicesCount; i++)
    {
        if (ActiveDevices[i].EffectID > 0)
        {
            if (ActiveDevices[i].EffectID <= 12) {
                num_leds++;
            } else {
                num_motors++;
            }
        }
    }
    if (num_leds == 0) num_leds = 1;     // Tránh lỗi chia cho 0
    if (num_motors == 0) num_motors = 1; // Tránh lỗi chia cho 0

    int led_idx = 0;
    int motor_idx = 0;

    for (uint8_t i = 0; i < ActiveDevicesCount; i++)
    {
        ActiveDeviceState_t *dev = &ActiveDevices[i];
        uint16_t addr = dev->Address;

        if (dev->EffectID == 0)
        {
            if (addr <= 512) DMX_Buffer[addr]     = 0;
            if (addr + 1 <= 512) DMX_Buffer[addr + 1] = 0;
            if (addr + 2 <= 512) DMX_Buffer[addr + 2] = 0;
            continue;
        }

        // Tốc độ thay đổi tick trong firmware:
        // Được cấu hình dựa trên Delta Time thực tế để đồng bộ hoàn hảo với WPF theo công thức tick = time_ms * (BPM / 30000.0).
        // Đối với Motor (ID >= 13), chúng ta chia cho 60000.0f để giảm tốc độ xuống còn 50% so với LED,
        // giúp khắc phục giới hạn cơ học, mô phỏng quán tính nước thực tế và đồng bộ tuyệt đối với WPF.
        float tick_step;
        if (dev->EffectID >= 13)
        {
            tick_step = (float)delta_ms * ((float)dev->Speed / 60000.0f);
        }
        else
        {
            tick_step = (float)delta_ms * ((float)dev->Speed / 30000.0f);
        }
        dev->Tick += tick_step;
        double tick = dev->Tick;

        // HIỆU ỨNG LED (ID 1 -> 12)
        if (dev->EffectID <= 12)
        {
            int idx = led_idx;
            led_idx++;

            uint8_t out_r = 0, out_g = 0, out_b = 0;
            switch (dev->EffectID)
            {
                case 1: // Rainbow
                {
                    double hue = fmod(tick * 20.0 + idx * 25.0, 360.0);
                    GetRainbowColor(hue, 1.0f, &out_r, &out_g, &out_b);
                    break;
                }
                case 2: // Rainbow Wipe
                {
                    double rainbowWipeTick = tick * 2.0;
                    double denominator = num_leds * 1.2 + 4.0;
                    double rainbowWipeCycle = fmod(rainbowWipeTick, denominator);
                    double baseHueWipe = floor(rainbowWipeTick / denominator) * 50.0;
                    if (idx <= rainbowWipeCycle)
                    {
                        double hue = fmod(baseHueWipe + idx * 20.0, 360.0);
                        GetRainbowColor(hue, 1.0f, &out_r, &out_g, &out_b);
                    }
                    else
                    {
                        out_r = 0;
                        out_g = 0;
                        out_b = 0;
                    }
                    break;
                }
                case 3: // Chase
                {
                    double chaseTick = tick * 2.0;
                    int chasePos = (int)floor(chaseTick) % num_leds;
                    int trail = (chasePos - idx + num_leds) % num_leds;
                    double chaseIntensity = 0;
                    if (trail == 0) chaseIntensity = 1.0;
                    else if (trail == 1) chaseIntensity = 0.6;
                    else if (trail == 2) chaseIntensity = 0.3;
                    else if (trail == 3) chaseIntensity = 0.1;

                    out_r = (uint8_t)(dev->R * chaseIntensity);
                    out_g = (uint8_t)(dev->G * chaseIntensity);
                    out_b = (uint8_t)(dev->B * chaseIntensity);
                    break;
                }
                case 4: // Breathing
                {
                    double breathBrightness = 0.5 * (1.0 - cos(tick * 3.14159));
                    out_r = (uint8_t)(dev->R * breathBrightness);
                    out_g = (uint8_t)(dev->G * breathBrightness);
                    out_b = (uint8_t)(dev->B * breathBrightness);
                    break;
                }
                case 5: // Rainbow Breathing
                {
                    double rainbowBreathBrightness = 0.5 * (1.0 - cos(tick * 3.14159));
                    double rainbowBreathHue = fmod(tick * 10.0 + idx * 15.0, 360.0);
                    GetRainbowColor(rainbowBreathHue, (float)rainbowBreathBrightness, &out_r, &out_g, &out_b);
                    break;
                }
                case 6: // Color Wipe
                {
                    double colorWipeTick = tick * 2.0;
                    double denominator = num_leds * 1.2 + 4.0;
                    double colorWipeCycle = fmod(colorWipeTick, denominator);
                    if (idx <= colorWipeCycle)
                    {
                        double diff = colorWipeCycle - idx;
                        double intensity = diff >= 1.0 ? 1.0 : diff;
                        out_r = (uint8_t)(dev->R * intensity);
                        out_g = (uint8_t)(dev->G * intensity);
                        out_b = (uint8_t)(dev->B * intensity);
                    }
                    else
                    {
                        out_r = 0;
                        out_g = 0;
                        out_b = 0;
                    }
                    break;
                }
                case 7: // Strobe
                {
                    bool strobeState = fmod(tick, 2.0) < 0.3;
                    if (strobeState)
                    {
                        out_r = dev->R;
                        out_g = dev->G;
                        out_b = dev->B;
                    }
                    else
                    {
                        out_r = 0;
                        out_g = 0;
                        out_b = 0;
                    }
                    break;
                }
                case 8: // Gradient Shift
                {
                    double gradHue = fmod(tick * 15.0 + idx * 360.0 / num_leds, 360.0);
                    GetRainbowColor(gradHue, 1.0f, &out_r, &out_g, &out_b);
                    break;
                }
                case 9: // Glitter
                {
                    double glitterVal = sin(tick * 3.5 + idx * 23.3) * cos(tick * 2.2 - idx * 13.7);
                    double glitterIntensity = (glitterVal > 0.35) ? (glitterVal - 0.35) / 0.65 : 0.0;
                    out_r = (uint8_t)(dev->R * glitterIntensity);
                    out_g = (uint8_t)(dev->G * glitterIntensity);
                    out_b = (uint8_t)(dev->B * glitterIntensity);
                    break;
                }
                case 10: // Meteor
                {
                    double meteorTick = tick * 4.0;
                    double meteorHead = fmod(meteorTick, num_leds + 15.0);
                    double distToHead = meteorHead - idx;
                    double meteorIntensity = 0;
                    if (distToHead >= 0 && distToHead < 10)
                    {
                        meteorIntensity = pow(1.0 - (distToHead / 10.0), 1.5);
                    }
                    out_r = (uint8_t)(dev->R * meteorIntensity);
                    out_g = (uint8_t)(dev->G * meteorIntensity);
                    out_b = (uint8_t)(dev->B * meteorIntensity);
                    break;
                }
                case 11: // Pulse Gradient
                {
                    double pulseIntensity = 0.1 + 0.9 * pow(fabs(cos(tick * 3.14159)), 4.0);
                    double pulseHue = fmod(tick * 12.0 + idx * 360.0 / num_leds, 360.0);
                    GetRainbowColor(pulseHue, (float)pulseIntensity, &out_r, &out_g, &out_b);
                    break;
                }
                case 12: // Color
                {
                    out_r = dev->R;
                    out_g = dev->G;
                    out_b = dev->B;
                    break;
                }
                default:
                    out_r = dev->R; out_g = dev->G; out_b = dev->B;
                    break;
            }

            // Áp dụng cân bằng màu sắc và Gamma Correction trước khi ghi vào DMX_Buffer
            uint8_t corrected_r = 0, corrected_g = 0, corrected_b = 0;
            Apply_Color_Correction(out_r, out_g, out_b, &corrected_r, &corrected_g, &corrected_b);

            // Ghi vào 3 kênh RGB liên tiếp của địa chỉ hiện tại
            if (addr <= 512) DMX_Buffer[addr]     = corrected_r;
            if (addr + 1 <= 512) DMX_Buffer[addr + 1] = corrected_g;
            if (addr + 2 <= 512) DMX_Buffer[addr + 2] = corrected_b;
        }
        // HIỆU ỨNG MOTOR (ID 13 -> 21)
        else if (dev->EffectID >= 13)
        {
            int idx = motor_idx;
            motor_idx++;

            double fillPercent = 0; // Giá trị phần trăm góc hoạt động (0.0 -> threshold)
            switch (dev->EffectID)
            {
                case 13: // Fixed Level (Đặt mức cố định)
                {
                    fillPercent = dev->Threshold;
                    break;
                }
                case 14: // Global Breathing (Tăng giảm đồng loạt)
                {
                    double wave = (sin(tick * 0.785398) + 1.0) / 2.0;
                    fillPercent = wave * dev->Threshold;
                    break;
                }
                case 15: // Wave Sweep (Sóng lan truyền)
                {
                    double phase = (tick * 0.785398) - (idx * 1.0);
                    double wave = (sin(phase) + 1.0) / 2.0;
                    fillPercent = wave * dev->Threshold;
                    break;
                }
                case 16: // Alternate Wave (Xen kẽ đối xứng)
                {
                    double phase = tick * 0.785398;
                    if (idx % 2)
                    {
                        phase += 3.14159;
                    }
                    double wave = (sin(phase) + 1.0) / 2.0;
                    fillPercent = wave * dev->Threshold;
                    break;
                }
                case 17: // Left To Right Sweep (Quét trái sang phải)
                {
                    uint8_t pos = ((uint32_t)(tick / 2.0)) % num_motors;
                    fillPercent = (idx == pos) ? dev->Threshold : 0;
                    break;
                }
                case 18: // Center Out Sweep (Quét giữa ra ngoài)
                {
                    int center = num_motors / 2;
                    int distance = abs(idx - center);
                    double phase = (tick * 0.785398) - (distance * 1.2);
                    double wave = (sin(phase) + 1.0) / 2.0;
                    fillPercent = wave * dev->Threshold;
                    break;
                }
                case 19: // Edge To Center Sweep (Quét ngoài vào giữa)
                {
                    int center = num_motors / 2;
                    int distance = abs(idx - center);
                    double phase = (tick * 0.785398) + (distance * 1.2);
                    double wave = (sin(phase) + 1.0) / 2.0;
                    fillPercent = wave * dev->Threshold;
                    break;
                }
                case 20: // Peak Runner (Đỉnh nhọn chạy)
                {
                    uint8_t pos = ((uint32_t)(tick / 2.0)) % num_motors;
                    int distance = abs((int)idx - (int)pos);
                    if (distance < 3)
                    {
                        double bright = 1.0 - ((double)distance / 3.0);
                        fillPercent = bright * dev->Threshold;
                    }
                    else
                    {
                        fillPercent = 0;
                    }
                    break;
                }
                case 21: // Fountain Fan (Xòe quạt)
                {
                    int center = num_motors / 2;
                    int distance = abs(idx - center);
                    double base_val = (sin(tick * 0.785398) + 1.0) / 2.0;
                    double scale = 1.0 - ((double)distance / (double)(center + 1));
                    fillPercent = base_val * scale * dev->Threshold;
                    break;
                }
                default:
                    fillPercent = dev->Threshold;
                    break;
            }

            // Ghi giá trị góc ra cổng DMX tương ứng cho motor
            if (addr <= 512) {
                DMX_Buffer[addr] = (uint8_t)floor(fillPercent);
            }
        }
    }
}


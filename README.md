# STM32 TX DMX512 Controller

## English

### Overview

Firmware for an STM32F103C8T6-based DMX512 controller. It executes control scenarios created with **DMXScenarioDesigner** and transmits DMX512 data to connected devices via RS-485.

### System Architecture

The system consists of three main components:

- **DMXScenarioDesigner (PC Application):** Creates and configures control scenarios, then transfers them to the TX controller through a USB virtual COM port.
- **TX Controller (STM32):** Receives scenarios from the application, stores them in internal Flash memory, and independently manages scenario timing and effect execution. It generates and transmits DMX512 data to control connected devices.
- **RX Devices:** Receive DMX512 data and use their assigned channel values to control the corresponding hardware.

The PC application is used to create and transfer scenarios. It does not directly control DMX devices during execution. Once a scenario has been downloaded, the TX controller operates independently as the DMX controller.

### Features

- Scenario transfer from DMXScenarioDesigner via a USB virtual COM port.
- Scenario storage in the STM32's internal Flash memory.
- Independent scenario timing and effect execution.
- DMX512 transmission via UART and RS-485.
- Designed to control multiple DMX devices through their assigned channels.

### Hardware and Development

- **MCU:** STM32F103C8T6
- **DMX Interface:** UART and RS-485
- **PC Communication:** USB virtual COM port
- **Development Environment:** STM32CubeIDE

### Compatibility

- Tested with DMX-controlled high-power RGB LED devices.
- Designed to control DMX512-compatible industrial lighting equipment and custom-built receivers using RS-485.
- Compatibility with other DMX devices depends on their DMX512 implementation and configuration.

### Related Projects

- **DMXScenarioDesigner:** PC application for creating and transferring control scenarios. *(Link to be added.)*
- **STM32 RX DMX512 Devices:** [STM32_RxDMX-LED_MOTOR](https://github.com/uienle/STM32_RxDMX-LED_MOTOR)

---

## Tiếng Việt

### Tổng quan

Firmware cho bộ điều khiển DMX512 sử dụng STM32F103C8T6. Firmware thực thi các kịch bản điều khiển được tạo bằng phần mềm **DMXScenarioDesigner** và truyền dữ liệu DMX512 đến các thiết bị thông qua giao tiếp RS-485.

### Kiến trúc hệ thống

Hệ thống gồm ba thành phần chính:

- **DMXScenarioDesigner (Phần mềm PC):** Tạo và cấu hình kịch bản điều khiển, sau đó truyền kịch bản đến TX thông qua cổng COM ảo USB.
- **TX Controller (STM32):** Nhận kịch bản từ phần mềm, lưu vào Flash nội của STM32 và tự xử lý thời gian cũng như quá trình thực thi hiệu ứng. TX tạo và truyền dữ liệu DMX512 để điều khiển các thiết bị kết nối.
- **RX Devices:** Nhận dữ liệu DMX512 và sử dụng các giá trị tại những kênh DMX được gán để điều khiển phần cứng tương ứng.

Phần mềm trên PC dùng để thiết kế và truyền kịch bản, không trực tiếp điều khiển thiết bị DMX trong quá trình vận hành. Sau khi kịch bản được tải xuống, TX hoạt động độc lập với vai trò là bộ điều khiển DMX.

### Tính năng

- Tải kịch bản từ DMXScenarioDesigner thông qua cổng COM ảo USB.
- Lưu kịch bản trong Flash nội của STM32.
- Tự xử lý thời gian và thực thi hiệu ứng theo kịch bản.
- Truyền dữ liệu DMX512 qua UART và RS-485.
- Hướng đến điều khiển nhiều thiết bị DMX thông qua các kênh được gán.

### Phần cứng và môi trường phát triển

- **Vi điều khiển:** STM32F103C8T6
- **Giao tiếp DMX:** UART và RS-485
- **Giao tiếp với PC:** Cổng COM ảo USB
- **Môi trường phát triển:** STM32CubeIDE

### Khả năng tương thích

- Đã thử nghiệm với thiết bị đèn LED RGB công suất điều khiển qua DMX.
- Được thiết kế để điều khiển các thiết bị chiếu sáng công nghiệp tương thích DMX512 và các bộ nhận tự thiết kế sử dụng RS-485.
- Khả năng tương thích với các thiết bị DMX khác phụ thuộc vào cách triển khai giao thức DMX512 và cấu hình của từng thiết bị.

### Các dự án liên quan

- **DMXScenarioDesigner:** Phần mềm PC dùng để tạo và tải kịch bản điều khiển. *(Sẽ bổ sung liên kết.)*
- **STM32 RX DMX512 Devices:** [STM32_RxDMX-LED_MOTOR](https://github.com/uienle/STM32_RxDMX-LED_MOTOR)

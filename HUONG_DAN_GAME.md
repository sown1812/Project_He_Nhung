# Flappy Bird trên STM32F429I-DISC1 — Trạng thái & Cách build

## Đã tích hợp sẵn (không cần làm lại)
- **Game:** `Core/Inc/game.h`, `Core/Src/game.c` — logic Flappy Bird đầy đủ.
- **main.c:** đã gọi `Game_Init()` + `Game_Loop()`.
- **Clock:** SYSCLK = 180 MHz (đã cấu hình bằng CubeMX).
- **Thư viện màn hình BSP** (copy từ STM32Cube_FW_F4):
  - `Drivers/BSP/STM32F429I-Discovery/` — discovery, LCD, SDRAM
  - `Drivers/BSP/Components/ili9341/`, `Drivers/BSP/Components/Common/`
  - `Drivers/BSP/Fonts/` — font8/12/16/20/24 + fonts.h
  - *(đã sửa include font trong `stm32f429i_discovery_lcd.c/.h` về `"fonts.h"`
    và bỏ các dòng `#include "*.c"` để tránh trùng định nghĩa khi link)*
- **HAL source bổ sung** trong `Drivers/STM32F4xx_HAL_Driver/Src/`:
  ltdc, ltdc_ex, dma2d, sdram, spi, i2c, ll_fmc.
- **HAL module đã bật** trong `Core/Inc/stm32f4xx_hal_conf.h`:
  `LTDC, DMA2D, SDRAM, SPI, I2C` (cùng các module mặc định DMA/GPIO/RCC...).
- **Include paths** đã thêm vào `.cproject` (cả Debug & Release):
  BSP/STM32F429I-Discovery, BSP/Components/Common, BSP/Components/ili9341, BSP/Fonts.

## Cách build & nạp
1. Trong **STM32CubeIDE**: chọn project → nhấn **F5** (Refresh) để IDE nhận các file mới.
2. **Project → Build Project** (hoặc Ctrl+B). Lần đầu build lâu vì biên dịch cả BSP.
3. Cắm board qua cáp USB (ST-Link tích hợp) → **Run → Debug** (hoặc Run) để nạp.
4. Màn hình hiện "FLAPPY BIRD" → nhấn nút **USER (B1, xanh)** để chơi.

## ⚠️ Lưu ý quan trọng khi mở lại CubeMX
Nếu bạn bấm **GENERATE CODE** lại từ file `.ioc`, CubeMX sẽ **ghi đè**
`stm32f4xx_hal_conf.h` và **tắt lại** các HAL module (LTDC/DMA2D/SDRAM/SPI/I2C).
Khi đó chỉ cần mở `Core/Inc/stm32f4xx_hal_conf.h` và bỏ comment lại 5 dòng:
```c
#define HAL_DMA2D_MODULE_ENABLED
#define HAL_SDRAM_MODULE_ENABLED
#define HAL_LTDC_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
```
(Các file BSP và include paths trong `.cproject` thì CubeMX không đụng tới.)

## Cách chơi
- Màn **READY**: nhấn nút USER (B1) để bắt đầu.
- Đang chơi: mỗi lần nhấn, chim bay vọt lên; thả ra thì rơi theo trọng lực.
- Chui qua khe giữa 2 ống để ăn điểm. Chạm ống / trần / đáy → **GAME OVER**.
- Màn GAME OVER: nhấn nút để chơi lại. Điểm cao nhất lưu trong phiên chạy.

## Chỉnh độ khó (đầu file `Core/Src/game.c`, mục "Tham so game")
| Hằng số | Ý nghĩa | Dễ hơn |
|---|---|---|
| `GAP_H` | độ cao khe hở | tăng |
| `PIPE_SPEED` | tốc độ ống | giảm |
| `PIPE_SPACING` | khoảng cách 2 ống | tăng |
| `GRAVITY` | trọng lực | giảm |
| `FLAP_VELOCITY` | lực bay lên | giảm độ lớn |

## Ghi chú kỹ thuật
- **Chống nháy hình:** 2 layer LTDC làm double buffer (`draw_begin`/`draw_end`).
- **Framebuffer** trong SDRAM: layer0 `0xD0000000`, layer1 `0xD004B000`
  (240×320×4 byte, ARGB8888).
- **`BSP_LCD_Init()` tự gọi `BSP_SDRAM_Init()`** nên không cần init SDRAM riêng.
- **Nút PA0** active-high → `BSP_PB_GetState` trả 1 khi nhấn; code phát hiện sườn lên.
- **~50 FPS**, điều tiết bằng `HAL_GetTick()` (`FRAME_MS = 20`).

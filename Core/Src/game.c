/**
 * *****************************************************************************
 * @file    game.c
 * @brief   Flappy Bird cho STM32F429I-DISC1 (Toi uu VSYNC + Am thanh + Rung + Do kho)
 *
 *  - Man hinh: TFT 240x320 tich hop tren board (dieu khien qua BSP_LCD_*)
 *  - Dieu khien: nut USER (B1) PA0 hoac cham vao bat ky dau tren man hinh cam ung
 *  - Do hoa: Tang toc phan cung DMA2D (Chrom-Art)
 *  - Rung & Chop: Rung man hinh va chop trang khi chim va cham chet
 *  - Do kho: Tang toc ong va thu hep khe ho theo diem so
 *  - Nền dem: Tu dong chuyen sang man dem (background_night) khi dat 15 diem
 * *****************************************************************************
 */
#include "game.h"
#include "assets.h"

#include "stm32f429i_discovery.h"
#include "stm32f429i_discovery_lcd.h"
#include "stm32f429i_discovery_ts.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ==========================================================================
 *  Cau hinh man hinh & double buffer
 * ========================================================================== */
#define SCR_W            240u
#define SCR_H            320u

/* Framebuffer nam trong SDRAM (0xD0000000). Layer dung dinh dang ARGB8888
 * => moi pixel 4 byte. Kich thuoc 1 layer = 240*320*4 = 0x4B000 byte. */
#define LCD_FB_LAYER0    ((uint32_t)0xD0000000)
#define LCD_FB_LAYER1    ((uint32_t)(0xD0000000 + (SCR_W * SCR_H * 4u)))

/* ==========================================================================
 *  Tham so game va cau hinh dong
 * ========================================================================== */
#define BIRD_X           60      /* vi tri ngang co dinh cua chim            */
#define BIRD_R           9       /* ban kinh va cham vat ly cua chim (pixel)  */

#define GRAVITY          0.42f   /* trong luc moi khung hinh                 */
#define FLAP_VELOCITY   -6.0f    /* van toc bat len khi nhan nut             */
#define MAX_FALL         8.0f    /* gioi han van toc roi                     */

#define PIPE_W           52      /* be rong ong (trung hop voi SPRITE_PIPE_W) */
#define PIPE_SPACING     150     /* khoang cach giua 2 ong lien tiep         */
#define NUM_PIPES        3       /* so ong quan ly cung lu                   */

#define GROUND_Y         (SCR_H - SPRITE_GROUND_H) /* 320 - 48 = 272         */
#define GAP_MIN_Y        55      /* gioi han tren cua tam khe ho            */
#define GAP_MAX_Y        205     /* gioi han duoi cua tam khe ho            */

#define FRAME_MS         20u     /* ~50 FPS                                  */

#define COL_TEXT         0xFF333333u /* mau chu xam dam sang trong           */

/* ==========================================================================
 *  Trang thai game
 * ========================================================================== */
typedef enum {
    STATE_READY = 0,   /* cho nhan nut de bat dau  */
    STATE_PLAYING,     /* dang choi                */
    STATE_GAMEOVER     /* thua, cho nhan de choi lai*/
} GameState;

typedef struct {
    int16_t  x;        /* toa do trai cua ong            */
    int16_t  gap_y;    /* tam khe ho theo truc dung      */
    uint8_t  scored;   /* da tinh diem khi chim vuot qua?*/
} Pipe;

static GameState g_state;
static float     g_bird_y;
static float     g_bird_vy;
static Pipe      g_pipes[NUM_PIPES];
static uint32_t  g_score;
static uint32_t  g_best;

static uint32_t load_best_score(void);
static void     save_best_score(uint32_t score);

static volatile uint32_t g_active_layer; /* layer dang dung de VE (0 hoac 1) */
static int16_t           g_ground_scroll; /* do cuon cua mat dat */
static uint32_t          g_frame_count;   /* dung de chay hoat anh canh chim */

/* Hieu ung Rung man hinh (Screen Shake) va Chop sang (Flash) */
static int16_t           g_shake_x = 0;
static int16_t           g_shake_y = 0;
static uint8_t           g_shake_frames = 0;
static uint8_t           g_flash_frames = 0;

/* ==========================================================================
 *  Cau hinh Am thanh bang Timer 4 & GPIO PC9
 * ========================================================================== */
TIM_HandleTypeDef htim4;

typedef enum {
    SOUND_NONE = 0,
    SOUND_WING,
    SOUND_POINT,
    SOUND_HIT_DIE
} SoundEffect;

static SoundEffect g_current_sound = SOUND_NONE;
static uint32_t    g_sound_frame = 0;

static void Buzzer_Init(void)
{
    // 1. Bat Clock cho GPIOC va GPIOG (PG14 la LED do)
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    // 2. Cau hinh PC9 la Output Day-Keo (Buzzer)
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // Cau hinh PG14 (LED do) la Output
    GPIO_InitStruct.Pin = GPIO_PIN_14;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_RESET);

    // 3. Bat Clock cho TIM4
    __HAL_RCC_TIM4_CLK_ENABLE();

    // 4. Cau hinh TIM4 o che do Time Base
    htim4.Instance = TIM4;
    htim4.Init.Prescaler = 90 - 1;
    htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim4.Init.Period = 1000 - 1;
    htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_Base_Init(&htim4);

    // 5. Bat ngat TIM4 NVIC
    HAL_NVIC_SetPriority(TIM4_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(TIM4_IRQn);
}

static void play_frequency(uint16_t freq)
{
    if (freq == 0) {
        HAL_TIM_Base_Stop_IT(&htim4);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOG, GPIO_PIN_14, GPIO_PIN_RESET);
    } else {
        uint32_t period = 500000u / freq;
        if (period < 5) period = 5;
        __HAL_TIM_SET_AUTORELOAD(&htim4, period - 1);
        __HAL_TIM_SET_COUNTER(&htim4, 0);
        HAL_TIM_Base_Start_IT(&htim4);
    }
}

static void start_sound(SoundEffect effect)
{
    if (g_current_sound == SOUND_HIT_DIE && effect != SOUND_HIT_DIE) {
        return;
    }
    g_current_sound = effect;
    g_sound_frame = 0;
}

static void update_sound_effects(void)
{
    if (g_current_sound == SOUND_NONE) {
        play_frequency(0);
        return;
    }

    g_sound_frame++;

    switch (g_current_sound) {
        case SOUND_WING:
            if (g_sound_frame <= 4) {
                uint16_t freq = 450 + (g_sound_frame - 1) * 150;
                play_frequency(freq);
            } else {
                g_current_sound = SOUND_NONE;
                play_frequency(0);
            }
            break;

        case SOUND_POINT:
            if (g_sound_frame <= 3) {
                play_frequency(880);
            } else if (g_sound_frame <= 5) {
                play_frequency(0);
            } else if (g_sound_frame <= 10) {
                play_frequency(1320);
            } else {
                g_current_sound = SOUND_NONE;
                play_frequency(0);
            }
            break;

        case SOUND_HIT_DIE:
            if (g_sound_frame <= 15) {
                uint16_t freq = 700 - (g_sound_frame - 1) * 40;
                play_frequency(freq);
            } else {
                g_current_sound = SOUND_NONE;
                play_frequency(0);
            }
            break;

        default:
            g_current_sound = SOUND_NONE;
            play_frequency(0);
            break;
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM4) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_9);
        HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_14);
    }
}

/* Sinh so ngau nhien don gian (LCG) */
static uint32_t g_rng = 0x1234abcdu;
static uint32_t rng_next(void)
{
    g_rng = g_rng * 1103515245u + 12345u;
    return (g_rng >> 16) & 0x7fffu;
}

/* ==========================================================================
 *  Quan ly do kho dong (Dynamic Difficulty)
 * ========================================================================== */
static uint8_t get_pipe_speed(void)
{
    if (g_state != STATE_PLAYING) return 3; // Toc do mac dinh o menu
    if (g_score < 10) return 3;
    if (g_score < 25) return 4;
    return 5; // Diem cao chay nhanh hon
}

static uint8_t get_gap_height(void)
{
    if (g_score < 10) return 96;
    if (g_score < 20) return 86;
    return 76; // Càng nhieu diem khe ho cang hep
}

/* ==========================================================================
 *  Doc nut bam va cam ung: Phat hien suon len (chi flap 1 lan/lan cham)
 * ========================================================================== */
static uint8_t g_btn_prev = 0;
static uint8_t g_ts_prev = 0;

/* Tra ve 1 dung MOT khung hinh khi co nhan nut hoac cham man hinh (rising edge). */
static uint8_t input_pressed_edge(void)
{
    // 1. Doc nut bam USER
    uint8_t btn_now = (BSP_PB_GetState(BUTTON_KEY) == 1) ? 1 : 0;
    uint8_t btn_edge = (btn_now && !g_btn_prev) ? 1 : 0;
    g_btn_prev = btn_now;

    // 2. Doc cam ung man hinh
    TS_StateTypeDef ts_state = {0};
    BSP_TS_GetState(&ts_state);
    uint8_t ts_now = (ts_state.TouchDetected > 0) ? 1 : 0;
    uint8_t ts_edge = (ts_now && !g_ts_prev) ? 1 : 0;
    g_ts_prev = ts_now;

    return (btn_edge || ts_edge);
}

/* ==========================================================================
 *  Double buffer dong bo VSYNC: Ve vao layer ẩn va gan dia chi tren VSYNC
 * ========================================================================== */
static void draw_begin(void)
{
    // Chi can ve truc tiep vao back buffer
}

static void draw_end(void)
{
    uint32_t back_fb = (g_active_layer == 0) ? LCD_FB_LAYER0 : LCD_FB_LAYER1;

    BSP_LCD_SetLayerAddress(0, back_fb);
    BSP_LCD_Relaod(LCD_RELOAD_VERTICAL_BLANKING);
    g_active_layer = 1u - g_active_layer;
}

/* ==========================================================================
 *  Vẽ Sprite tang toc bang phan cung DMA2D
 * ========================================================================== */

/* Vẽ Sprite co kenh Alpha (Tu dong Hoa tron phan cung) */
static void draw_sprite_dma2d(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint32_t *sprite_data)
{
    uint32_t fb_base = (g_active_layer == 0) ? LCD_FB_LAYER0 : LCD_FB_LAYER1;

    int16_t src_x = 0;
    int16_t src_y = 0;
    int16_t draw_w = w;
    int16_t draw_h = h;

    if (x < 0) { src_x = -x; draw_w += x; x = 0; }
    if (x + draw_w > (int16_t)SCR_W) draw_w = (int16_t)SCR_W - x;
    if (y < 0) { src_y = -y; draw_h += y; y = 0; }
    if (y + draw_h > (int16_t)SCR_H) draw_h = (int16_t)SCR_H - y;

    if (draw_w <= 0 || draw_h <= 0) return;

    uint32_t src_addr = (uint32_t)(sprite_data + src_y * w + src_x);
    uint32_t dst_addr = (uint32_t)(fb_base + 4 * (y * SCR_W + x));

    DMA2D_HandleTypeDef hdma2d = {0};
    hdma2d.Instance = DMA2D;
    hdma2d.Init.Mode = DMA2D_M2M_BLEND;
    hdma2d.Init.ColorMode = DMA2D_OUTPUT_ARGB8888;
    hdma2d.Init.OutputOffset = SCR_W - draw_w;

    // Background (Layer 0) - Framebuffer
    hdma2d.LayerCfg[0].AlphaMode = DMA2D_NO_MODIF_ALPHA;
    hdma2d.LayerCfg[0].InputAlpha = 0xFF;
    hdma2d.LayerCfg[0].InputColorMode = DMA2D_INPUT_ARGB8888;
    hdma2d.LayerCfg[0].InputOffset = SCR_W - draw_w;

    // Foreground (Layer 1) - Sprite giu nguyen Alpha
    hdma2d.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
    hdma2d.LayerCfg[1].InputAlpha = 0xFF;
    hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_ARGB8888;
    hdma2d.LayerCfg[1].InputOffset = w - draw_w;

    HAL_DMA2D_Init(&hdma2d);
    HAL_DMA2D_ConfigLayer(&hdma2d, 0);
    HAL_DMA2D_ConfigLayer(&hdma2d, 1);

    HAL_DMA2D_BlendingStart(&hdma2d, src_addr, dst_addr, dst_addr, draw_w, draw_h);
    HAL_DMA2D_PollForTransfer(&hdma2d, 10);
}

/* Vẽ Sprite Opaque (Sao chep de truc tiep, khong hoa tron -> sieu nhanh) */
static void draw_sprite_dma2d_opaque(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint32_t *sprite_data)
{
    uint32_t fb_base = (g_active_layer == 0) ? LCD_FB_LAYER0 : LCD_FB_LAYER1;

    int16_t src_x = 0;
    int16_t src_y = 0;
    int16_t draw_w = w;
    int16_t draw_h = h;

    if (x < 0) { src_x = -x; draw_w += x; x = 0; }
    if (x + draw_w > (int16_t)SCR_W) draw_w = (int16_t)SCR_W - x;
    if (y < 0) { src_y = -y; draw_h += y; y = 0; }
    if (y + draw_h > (int16_t)SCR_H) draw_h = (int16_t)SCR_H - y;

    if (draw_w <= 0 || draw_h <= 0) return;

    uint32_t src_addr = (uint32_t)(sprite_data + src_y * w + src_x);
    uint32_t dst_addr = (uint32_t)(fb_base + 4 * (y * SCR_W + x));

    DMA2D_HandleTypeDef hdma2d = {0};
    hdma2d.Instance = DMA2D;
    hdma2d.Init.Mode = DMA2D_M2M;
    hdma2d.Init.ColorMode = DMA2D_OUTPUT_ARGB8888;
    hdma2d.Init.OutputOffset = SCR_W - draw_w;

    hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_ARGB8888;
    hdma2d.LayerCfg[1].InputOffset = w - draw_w;

    HAL_DMA2D_Init(&hdma2d);
    HAL_DMA2D_ConfigLayer(&hdma2d, 1);

    HAL_DMA2D_Start(&hdma2d, src_addr, dst_addr, draw_w, draw_h);
    HAL_DMA2D_PollForTransfer(&hdma2d, 10);
}

/* Ve chuoi chu voi nen trong suot bang CPU */
static void draw_string_transparent(uint16_t x, uint16_t y, const char *str, sFONT *font, uint32_t text_color)
{
    uint32_t *fb = (uint32_t *)((g_active_layer == 0) ? LCD_FB_LAYER0 : LCD_FB_LAYER1);
    uint16_t height = font->Height;
    uint16_t width = font->Width;
    uint16_t len = strlen(str);

    for (uint16_t char_idx = 0; char_idx < len; char_idx++) {
        char ascii = str[char_idx];
        if (ascii < ' ' || ascii > '~') continue;

        const uint8_t *c = &font->table[(ascii - ' ') * height * ((width + 7) / 8)];
        uint8_t offset = 8 * ((width + 7) / 8) - width;

        for (uint16_t row = 0; row < height; row++) {
            int16_t sy = y + row;
            if (sy < 0 || sy >= (int16_t)SCR_H) continue;

            const uint8_t *pchar = c + ((width + 7) / 8) * row;
            uint32_t line = 0;
            switch ((width + 7) / 8) {
                case 1:  line = pchar[0]; break;
                case 2:  line = (pchar[0] << 8) | pchar[1]; break;
                case 3:
                default: line = (pchar[0] << 16) | (pchar[1] << 8) | pchar[2]; break;
            }

            for (uint16_t col = 0; col < width; col++) {
                int16_t sx = x + char_idx * width + col;
                if (sx < 0 || sx >= (int16_t)SCR_W) continue;

                if (line & (1 << (width - col + offset - 1))) {
                    fb[sy * SCR_W + sx] = text_color;
                }
            }
        }
    }
}

static void draw_string_transparent_centered(uint16_t y, const char *str, sFONT *font, uint32_t text_color)
{
    uint16_t total_width = strlen(str) * font->Width;
    int16_t start_x = (SCR_W - total_width) / 2;
    if (start_x < 0) start_x = 0;
    draw_string_transparent(start_x, y, str, font, text_color);
}

/* Ve diem so su dung cac sprite so 0-9 dong bang DMA2D */
static void draw_score_sprites(uint32_t score, int16_t center_y)
{
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);

    uint16_t total_width = 0;
    for (int i = 0; i < len; i++) {
        uint8_t digit = buf[i] - '0';
        total_width += sprite_number_widths[digit];
        if (i < len - 1) {
            total_width += 2;
        }
    }

    int16_t start_x = (SCR_W - total_width) / 2;
    for (int i = 0; i < len; i++) {
        uint8_t digit = buf[i] - '0';
        draw_sprite_dma2d(start_x + g_shake_x, center_y + g_shake_y, sprite_number_widths[digit], SPRITE_NUMBER_H, sprite_numbers[digit]);
        start_x += sprite_number_widths[digit] + 2;
    }
}

/* Hoat anh lay sprite chim va xoay theo van toc doc g_bird_vy */
static const uint32_t *get_bird_sprite(void)
{
    if (g_state == STATE_READY) {
        uint32_t index = (g_frame_count / 4) % 4;
        switch (index) {
            case 0: return sprite_bird1;
            case 1: return sprite_bird2;
            case 2: return sprite_bird3;
            case 3: return sprite_bird2;
        }
        return sprite_bird1;
    }

    if (g_bird_vy < -2.0f) {
        uint32_t index = (g_frame_count / 3) % 4;
        switch (index) {
            case 0: return sprite_bird1_up;
            case 1: return sprite_bird2_up;
            case 2: return sprite_bird3_up;
            case 3: return sprite_bird2_up;
        }
        return sprite_bird1_up;
    }
    else if (g_bird_vy < 1.5f) {
        uint32_t index = (g_frame_count / 4) % 4;
        switch (index) {
            case 0: return sprite_bird1;
            case 1: return sprite_bird2;
            case 2: return sprite_bird3;
            case 3: return sprite_bird2;
        }
        return sprite_bird1;
    }
    else if (g_bird_vy < 3.5f) {
        return sprite_bird2_rot30;
    }
    else if (g_bird_vy < 5.5f) {
        return sprite_bird2_rot60;
    }
    else {
        return sprite_bird2_rot90;
    }
}

/* ==========================================================================
 *  Khoi tao / reset man choi
 * ========================================================================== */
static void reset_round(void)
{
    g_bird_y  = GROUND_Y / 2.0f;
    g_bird_vy = 0.0f;
    g_score   = 0;

    for (int i = 0; i < NUM_PIPES; i++) {
        g_pipes[i].x     = SCR_W + i * PIPE_SPACING;
        g_pipes[i].gap_y = GAP_MIN_Y + rng_next() % (GAP_MAX_Y - GAP_MIN_Y);
        g_pipes[i].scored = 0;
    }
    g_shake_x = 0;
    g_shake_y = 0;
    g_shake_frames = 0;
    g_flash_frames = 0;
}

/* ==========================================================================
 *  Cap nhat vat ly + va cham (chi khi STATE_PLAYING)
 * ========================================================================== */
static void update_playing(uint8_t flap)
{
    if (flap) {
        g_bird_vy = FLAP_VELOCITY;
        start_sound(SOUND_WING);
    }

    g_bird_vy += GRAVITY;
    if (g_bird_vy > MAX_FALL) g_bird_vy = MAX_FALL;
    g_bird_y  += g_bird_vy;

    /* Cham tran / cham dat -> thua */
    if (g_bird_y - BIRD_R < 0 || g_bird_y + BIRD_R > GROUND_Y) {
        g_state = STATE_GAMEOVER;
        start_sound(SOUND_HIT_DIE);
        g_shake_frames = 12; // Kich hoat rung man hinh
        g_flash_frames = 2;  // Kich hoat chop trang
        if (g_score > g_best) g_best = g_score;
        save_best_score(g_best);
        return;
    }

    for (int i = 0; i < NUM_PIPES; i++) {
        g_pipes[i].x -= get_pipe_speed();

        /* Ong chay het man -> dua ra sau va sinh khe moi */
        if (g_pipes[i].x + PIPE_W < 0) {
            int16_t max_x = g_pipes[0].x;
            for (int j = 1; j < NUM_PIPES; j++)
                if (g_pipes[j].x > max_x) max_x = g_pipes[j].x;
            g_pipes[i].x     = max_x + PIPE_SPACING;
            g_pipes[i].gap_y = GAP_MIN_Y + rng_next() % (GAP_MAX_Y - GAP_MIN_Y);
            g_pipes[i].scored = 0;
        }

        /* Tinh diem khi chim vuot qua ong */
        if (!g_pipes[i].scored && g_pipes[i].x + PIPE_W < BIRD_X - BIRD_R) {
            g_pipes[i].scored = 1;
            g_score++;
            start_sound(SOUND_POINT);
        }

        /* Va cham voi ong: chim chong len vung ong theo truc X */
        if (BIRD_X + BIRD_R > g_pipes[i].x &&
            BIRD_X - BIRD_R < g_pipes[i].x + PIPE_W) {
            int16_t gap_top    = g_pipes[i].gap_y - get_gap_height() / 2;
            int16_t gap_bottom = g_pipes[i].gap_y + get_gap_height() / 2;
            if (g_bird_y - BIRD_R < gap_top ||
                g_bird_y + BIRD_R > gap_bottom) {
                g_state = STATE_GAMEOVER;
                start_sound(SOUND_HIT_DIE);
                g_shake_frames = 12; // Kich hoat rung man hinh
                g_flash_frames = 2;  // Kich hoat chop trang
                if (g_score > g_best) g_best = g_score;
                save_best_score(g_best);
                return;
            }
        }
    }
}

/* ==========================================================================
 *  Ve khug hinh
 * ========================================================================== */
static void draw_pipe(const Pipe *p)
{
    int16_t gap_top    = p->gap_y - get_gap_height() / 2;
    int16_t gap_bottom = p->gap_y + get_gap_height() / 2;

    /* Ve Ong tren (Truy cap phan cuoi cua mang anh ong nuoc lat nguoc stm32f4xx_hal_tim) */
    if (gap_top > 0) {
        const uint32_t *sprite_src = sprite_pipe_flipped + (320 - gap_top) * SPRITE_PIPE_W;
        draw_sprite_dma2d(p->x + g_shake_x, g_shake_y, SPRITE_PIPE_W, gap_top, sprite_src);
    }

    /* Ve Ong duoi (Lay dung tu dau tro xuong) */
    int16_t bottom_pipe_h = GROUND_Y - gap_bottom;
    if (bottom_pipe_h > 0) {
        draw_sprite_dma2d(p->x + g_shake_x, gap_bottom + g_shake_y, SPRITE_PIPE_W, bottom_pipe_h, sprite_pipe);
    }
}

static void draw_frame(void)
{
    draw_begin();

    // 1. Kiem tra hieu ung chop trang (Flash)
    if (g_flash_frames > 0) {
        uint32_t fb_base = (g_active_layer == 0) ? LCD_FB_LAYER0 : LCD_FB_LAYER1;
        memset((void*)fb_base, 0xFF, SCR_W * SCR_H * 4); // Phủ trang toan bo man hinh
        g_flash_frames--;
        draw_end();
        return;
    }

    // 2. Kiem tra hieu ung rung man hinh (Shake)
    if (g_shake_frames > 0) {
        g_shake_x = ((int16_t)(rng_next() % 9)) - 4; // Rung ngang tu -4 den 4 pixel
        g_shake_y = ((int16_t)(rng_next() % 9)) - 4; // Rung doc tu -4 den 4 pixel
        g_shake_frames--;
    } else {
        g_shake_x = 0;
        g_shake_y = 0;
    }

    // 3. Ve anh nen (Chuyen sang man dem sprite_background_night khi >= 15 diem)
    if (g_score < 15) {
        draw_sprite_dma2d_opaque(g_shake_x, g_shake_y, SPRITE_BG_W, SPRITE_BG_H, sprite_background);
    } else {
        draw_sprite_dma2d_opaque(g_shake_x, g_shake_y, SPRITE_BG_W, SPRITE_BG_H, sprite_background_night);
    }

    // 4. Ve cac cot
    if (g_state == STATE_PLAYING || g_state == STATE_GAMEOVER) {
        for (int i = 0; i < NUM_PIPES; i++) {
            draw_pipe(&g_pipes[i]);
        }
    }

    // 5. Ve mat dat (opaque copy ho tro am)
    draw_sprite_dma2d_opaque(-g_ground_scroll + g_shake_x, GROUND_Y + g_shake_y, SPRITE_GROUND_W, SPRITE_GROUND_H, sprite_ground);

    // 6. Ve chim (36x36 dong nhat, xoay theo van toc)
    int16_t bx = BIRD_X - SPRITE_BIRD_W / 2;
    int16_t by = (int16_t)g_bird_y - SPRITE_BIRD_H / 2;
    draw_sprite_dma2d(bx + g_shake_x, by + g_shake_y, SPRITE_BIRD_W, SPRITE_BIRD_H, get_bird_sprite());

    // 7. Ve giao dien theo tung trang thai
    if (g_state == STATE_READY) {
        int16_t mx = (SCR_W - SPRITE_MESSAGE_W) / 2;
        int16_t my = (GROUND_Y - SPRITE_MESSAGE_H) / 2;
        if (my < 0) my = 2;
        draw_sprite_dma2d(mx + g_shake_x, my + g_shake_y, SPRITE_MESSAGE_W, SPRITE_MESSAGE_H, sprite_message);
    }
    else if (g_state == STATE_PLAYING) {
        draw_score_sprites(g_score, 20);
    }
    else if (g_state == STATE_GAMEOVER) {
        int16_t go_x = (SCR_W - SPRITE_GAMEOVER_W) / 2;
        draw_sprite_dma2d(go_x + g_shake_x, 45 + g_shake_y, SPRITE_GAMEOVER_W, SPRITE_GAMEOVER_H, sprite_gameover);

        draw_string_transparent(100 + g_shake_x, 105 + g_shake_y, "SCORE", &Font16, COL_TEXT);
        draw_score_sprites(g_score, 125);

        draw_string_transparent(105 + g_shake_x, 170 + g_shake_y, "BEST", &Font16, COL_TEXT);
        draw_score_sprites(g_best, 190);

        draw_string_transparent_centered(245 + g_shake_y, "TAP TO PLAY AGAIN", &Font16, COL_TEXT);
    }

    draw_end();
}

/* ==========================================================================
 *  API cong khai
 * ========================================================================== */
void Game_Init(void)
{
    /* --- Man hinh --- */
    BSP_LCD_Init();

    // Chi cau hinh Layer 0 lam layer hien thi chinh, trỏ den LCD_FB_LAYER0 ban dau
    BSP_LCD_LayerDefaultInit(0, LCD_FB_LAYER0);
    BSP_LCD_SelectLayer(0);
    BSP_LCD_DisplayOn();

    // Ve truoc hinh nen vao ca 2 framebuffer trong RAM de tranh rac do khi hoan doi lan dau
    for (uint32_t i = 0; i < SCR_W * SCR_H; i++) {
        ((uint32_t*)LCD_FB_LAYER0)[i] = sprite_background[i];
        ((uint32_t*)LCD_FB_LAYER1)[i] = sprite_background[i];
    }

    // Khoi tao viet vao buffer 1 ở frame dau tien
    g_active_layer = 1;

    /* --- Coi Buzzer & Timer 4 --- */
    Buzzer_Init();

    /* --- Cam ung va Nut USER --- */
    BSP_TS_Init(SCR_W, SCR_H);
    BSP_PB_Init(BUTTON_KEY, BUTTON_MODE_GPIO);

    /* --- Trang thai ban dau --- */
    g_best          = load_best_score();
    g_state         = STATE_READY;
    g_ground_scroll = 0;
    g_frame_count   = 0;
    g_current_sound = SOUND_NONE;
    play_frequency(0);

    reset_round();
}

void Game_Loop(void)
{
    uint32_t last = HAL_GetTick();

    for (;;) {
        uint32_t now = HAL_GetTick();
        if (now - last < FRAME_MS) {
            __WFI();            /* ngu tiet kiem dien den ngat ke tiep, thay vi quay vong */
            continue;
        }
        last += FRAME_MS;

        uint8_t flap = input_pressed_edge();

        /* Gieo lai bo random theo thoi diem nhan nut dau tien */
        if (flap && g_state == STATE_READY)
            g_rng ^= HAL_GetTick() * 2654435761u;

        switch (g_state) {
            case STATE_READY:
                if (flap) {
                    g_state = STATE_PLAYING;
                    update_playing(1);   /* cu nhay dau tien */
                }
                break;

            case STATE_PLAYING:
                update_playing(flap);
                break;

            case STATE_GAMEOVER:
                if (flap) {
                    reset_round();
                    g_state = STATE_READY;
                }
                break;
        }

        // Mat dat dung cuon khi game over
        if (g_state != STATE_GAMEOVER) {
            // Cuon dong bo voi toc do ong nuoc get_pipe_speed()
            // Tối ưu hóa chu kỳ cuộn mat dat o 60 pixel de luon chia het cho toc do 3, 4, 5 (khong lech nét)
            g_ground_scroll = (g_ground_scroll + get_pipe_speed()) % 60;
        }
        g_frame_count++;

        // Cap nhat am thanh cuon tan so 8-bit phi tuan tu
        update_sound_effects();

        draw_frame();
    }
}

/* ==========================================================================
 *  Luu diem cao nhat vao Flash (sector cuoi) de khong mat khi cup dien
 * ========================================================================== */
#define BEST_SCORE_ADDR   0x081E0000u
#define BEST_SCORE_SECTOR FLASH_SECTOR_23

static uint32_t load_best_score(void)
{
    uint32_t *ptr = (uint32_t*)BEST_SCORE_ADDR;
    uint32_t best = 0;
    
    // Quét qua sector 128KB (32768 từ 32-bit) để tìm điểm số ghi gần nhất
    for (uint32_t i = 0; i < 32768; i++) {
        if (ptr[i] == 0xFFFFFFFFu) {
            break; // Gặp ô trống đầu tiên
        }
        best = ptr[i];
    }
    return best;
}

static void save_best_score(uint32_t score)
{
    if (score <= load_best_score()) return;

    uint32_t *ptr = (uint32_t*)BEST_SCORE_ADDR;
    uint32_t write_idx = 32768;
    
    // Tìm ô trống 32-bit đầu tiên
    for (uint32_t i = 0; i < 32768; i++) {
        if (ptr[i] == 0xFFFFFFFFu) {
            write_idx = i;
            break;
        }
    }
    
    HAL_FLASH_Unlock();
    
    if (write_idx >= 32768) {
        // Sector đã đầy, cần xóa sector và ghi lại từ đầu
        FLASH_EraseInitTypeDef erase = {0};
        erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
        erase.Sector       = BEST_SCORE_SECTOR;
        erase.NbSectors    = 1;
        erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        uint32_t err;
        HAL_FLASHEx_Erase(&erase, &err);
        
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, BEST_SCORE_ADDR, score);
    } else {
        // Ghi tuần tự vào ô trống tiếp theo (chỉ mất ~16us, không giật màn hình)
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, (uint32_t)&ptr[write_idx], score);
    }
    
    HAL_FLASH_Lock();
}
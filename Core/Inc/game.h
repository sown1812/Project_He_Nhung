/**
 ******************************************************************************
 * @file    game.h
 * @brief   Flappy Bird cho STM32F429I-DISC1 (LCD 240x320 + nut USER PA0)
 *          Su dung thu vien BSP cua ST: BSP_LCD_*, BSP_PB_*
 ******************************************************************************
 */
#ifndef GAME_H
#define GAME_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Khoi tao man hinh, nut bam va trang thai game.
 *         Goi 1 lan trong main() sau khi HAL_Init() + SystemClock_Config().
 */
void Game_Init(void);

/**
 * @brief  Vong lap game (khong bao gio thoat). Goi trong while(1) cua main().
 *         Ben trong tu dieu tiet toc do khung hinh bang HAL_GetTick().
 */
void Game_Loop(void);

#ifdef __cplusplus
}
#endif

#endif /* GAME_H */

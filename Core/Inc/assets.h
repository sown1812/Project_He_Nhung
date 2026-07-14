#ifndef ASSETS_H
#define ASSETS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPRITE_BG_W 240
#define SPRITE_BG_H 320
extern const uint32_t sprite_background[76800];

#define SPRITE_GROUND_W 336
#define SPRITE_GROUND_H 48
extern const uint32_t sprite_ground[16128];

#define SPRITE_BIRD_W 34
#define SPRITE_BIRD_H 24
extern const uint32_t sprite_bird1[816];

#define SPRITE_BIRD_W 34
#define SPRITE_BIRD_H 24
extern const uint32_t sprite_bird2[816];

#define SPRITE_BIRD_W 34
#define SPRITE_BIRD_H 24
extern const uint32_t sprite_bird3[816];

#define SPRITE_PIPE_W 52
#define SPRITE_PIPE_H 320
extern const uint32_t sprite_pipe[16640];

#define SPRITE_GAMEOVER_W 192
#define SPRITE_GAMEOVER_H 42
extern const uint32_t sprite_gameover[8064];

#define SPRITE_MESSAGE_W 184
#define SPRITE_MESSAGE_H 267
extern const uint32_t sprite_message[49128];

#define SPRITE_NUMBER_0_W 24
extern const uint32_t sprite_number_0[864];

#define SPRITE_NUMBER_1_W 16
extern const uint32_t sprite_number_1[576];

#define SPRITE_NUMBER_2_W 24
extern const uint32_t sprite_number_2[864];

#define SPRITE_NUMBER_3_W 24
extern const uint32_t sprite_number_3[864];

#define SPRITE_NUMBER_4_W 24
extern const uint32_t sprite_number_4[864];

#define SPRITE_NUMBER_5_W 24
extern const uint32_t sprite_number_5[864];

#define SPRITE_NUMBER_6_W 24
extern const uint32_t sprite_number_6[864];

#define SPRITE_NUMBER_7_W 24
extern const uint32_t sprite_number_7[864];

#define SPRITE_NUMBER_8_W 24
extern const uint32_t sprite_number_8[864];

#define SPRITE_NUMBER_9_W 24
extern const uint32_t sprite_number_9[864];

#define SPRITE_NUMBER_H 36
extern const uint32_t* const sprite_numbers[10];
extern const uint8_t sprite_number_widths[10];


#ifdef __cplusplus
}
#endif

#endif /* ASSETS_H */

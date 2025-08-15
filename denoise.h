//
//  denoise.h
//  VoiceMouseDecode
//
//  Created by Qianqian Zu on 2025/7/8.
//

#ifndef DENOISE_H
#define DENOISE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_RATE     16000
#define ENERGY_THRESHOLD 500
#define SMOOTH_WINDOW 3

typedef struct {
    float x_prev;
    float y_prev;
} HighPassFilterState;

void denoise_init(HighPassFilterState *state);
void denoise_buffer(int16_t *data, size_t length, HighPassFilterState *state);

#ifdef __cplusplus
}
#endif
#endif

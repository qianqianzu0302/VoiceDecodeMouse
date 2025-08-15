//
//  denoise.c
//  VoiceMouseDecode
//
//  Created by Qianqian Zu on 2025/7/8.
//

#include "denoise.h"
#include <stdlib.h>
#include <math.h>

#define PI 3.141592653589793

static float high_pass_filter(float x, HighPassFilterState *state) {
    float rc = 1.0f / (2 * PI * 100.0f);  // cutoff 100Hz
    float dt = 1.0f / SAMPLE_RATE;
    float alpha = rc / (rc + dt);

    float y = alpha * (state->y_prev + x - state->x_prev);
    state->x_prev = x;
    state->y_prev = y;
    return y;
}

static int16_t energy_gate(int16_t sample) {
    return (abs(sample) < ENERGY_THRESHOLD) ? 0 : sample;
}

static void smooth_samples(int16_t *data, size_t length) {
    for (size_t i = SMOOTH_WINDOW; i < length - SMOOTH_WINDOW; ++i) {
        int32_t sum = 0;
        for (int j = -SMOOTH_WINDOW; j <= SMOOTH_WINDOW; ++j) {
            sum += data[i + j];
        }
        data[i] = sum / (2 * SMOOTH_WINDOW + 1);
    }
}

void denoise_init(HighPassFilterState *state) {
    state->x_prev = 0.0f;
    state->y_prev = 0.0f;
}

void denoise_buffer(int16_t *data, size_t length, HighPassFilterState *state) {
    for (size_t i = 0; i < length; ++i) {
        float hp = high_pass_filter((float)data[i], state);
        data[i] = energy_gate((int16_t)hp);
    }
    smooth_samples(data, length);
}


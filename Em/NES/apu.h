// This file has been modified from its original to fit this engine

// MIT License
// 
// Copyright (c) 2023 Emmanuel Obara
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* this holds the data required to update samples thru a filter */
typedef struct {
    double a0, a1, a2, a3, a4;
    double x1, x2, y1, y2;
}Biquad;

double biquad(double sample, Biquad *b);

void biquad_init(Biquad* b, int type,
    double dbGain, /* gain of filter */
    double freq, /* center frequency */
    double srate, /* sampling rate */
    double bandwidth); /* bandwidth in octaves */

#define SAMPLING_FREQUENCY 44100
// should be able to store samples produced in 1/60th of a second
// for the target sampling frequency
// higher sampling frequency will need a bigger buffer
#define AUDIO_BUFF_SIZE 1024
#define STATS_WIN_SIZE 20
#define AVERAGE_DOWNSAMPLING 0
#define NOMINAL_QUEUE_SIZE 6000

struct Emulator;
struct GraphicsContext;

enum {
    TIMER_HIGH = 0x7,
    PULSE_SHIFT = 0x7,
    PULSE_PERIOD = 0b01110000,
};

typedef struct {
    long long period;
    long long counter;
    uint32_t step;
    uint32_t limit;
    uint32_t from;
    uint8_t loop;
} Divider;

typedef struct {
    Divider t;     // 11 bit timer
    uint8_t l;      // length counter
    uint8_t id;     // pulse id whether 1 or 2
    uint8_t neg;
    uint8_t shift;
    Divider sweep;
    uint8_t enable_sweep;
    uint8_t duty;
    uint8_t const_volume;
    Divider envelope;
    uint8_t envelope_loop;
    uint8_t enabled;
    uint8_t mute;
    uint16_t target_period;
    uint8_t sweep_reload;
} Pulse;


typedef struct {
    Divider sequencer;
    uint8_t length_counter;
    uint8_t linear_reload;
    uint8_t linear_counter;
    uint8_t linear_reload_flag;
    uint8_t halt;
    uint8_t enabled;
} Triangle;


typedef struct {
    Divider timer;
    uint8_t mode;
    uint8_t l;
    uint16_t shift;
    uint8_t const_volume;
    Divider envelope;
    uint8_t envelope_loop;
    uint8_t enabled;
} Noise;

typedef struct {
    uint8_t enabled;
    uint8_t IRQ_enable;
    uint8_t loop;
    uint8_t counter;
    uint16_t sample_length;
    uint16_t sample_addr;
    uint8_t interrupt;
    uint8_t irq_set;
    uint16_t rate;
    uint16_t rate_index;
    // output unit
    uint8_t bits_remaining;
    uint8_t silence;
    uint8_t bits;
    // memory reader
    uint8_t sample;
    uint8_t empty;
    uint16_t bytes_remaining;
    uint16_t current_addr;
} DMC;

typedef struct {
    uint16_t factor_index;
    uint16_t target_factor;
    uint16_t equilibrium_factor;
    uint16_t max_factor;
    size_t samples;
    size_t max_period;
    size_t min_period;
    size_t period;
    size_t counter;
    size_t index;
    size_t max_index;
} Sampler;


typedef struct APU{
    struct Emulator* emulator;
    void *userdata;
    uint8_t (*GetMemoryFunc)(uint16_t address, void *userdata);
    int16_t buff[AUDIO_BUFF_SIZE];
    size_t stat_window[STATS_WIN_SIZE];
    Pulse pulse1;
    Pulse pulse2;
    Triangle triangle;
    Noise noise;
    DMC dmc;
    Sampler sampler;
    uint8_t frame_mode;
    uint8_t status;
    uint8_t IRQ_inhibit;
    uint8_t doIRQ;
    uint8_t doTickSkip;
    uint8_t frame_interrupt;
    uint8_t audio_start;
    uint8_t reset_sequencer;
    size_t cycles;
    size_t sequencer;
    float stat;
    size_t stat_index;
    Biquad filter;
    Biquad aa_filter;
    float volume;
} APU;


void init_APU(APU *apu);
void reset_APU(APU *apu);
void exit_APU();
void execute_apu(APU* apu);
void set_status(APU* apu, uint8_t value);
float get_sample(APU* apu);
void queue_audio(APU* apu, int16_t *outBuff, int *outSize);
uint8_t read_apu_status(APU* apu);
void set_frame_counter_ctrl(APU* apu, uint8_t value);

void set_pulse_ctrl(Pulse* pulse, uint8_t value);
void set_pulse_timer(Pulse* pulse, uint8_t value);
void set_pulse_sweep(Pulse* pulse, uint8_t value);
void set_pulse_length_counter(Pulse* pulse, uint8_t value);

void set_tri_counter(Triangle* triangle, uint8_t value);
void set_tri_timer_low(Triangle* triangle, uint8_t value);
void set_tri_length(Triangle* triangle, uint8_t value);

void set_noise_ctrl(Noise* noise, uint8_t value);
void set_noise_period(APU* apu, uint8_t value);
void set_noise_length(Noise* noise, uint8_t value);

void set_dmc_ctrl(APU* apu, uint8_t value);
void set_dmc_da(DMC* dmc, uint8_t value);
void set_dmc_addr(DMC* dmc, uint8_t value);
void set_dmc_length(DMC* dmc, uint8_t value);

#ifdef __cplusplus
}
#endif

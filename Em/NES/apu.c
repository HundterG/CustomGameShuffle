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

#include "apu.h"
#include <math.h>

#define TND_LUT_SIZE 203
#define PULSE_LUT_SIZE 31
#define AUDIO_TO_FILE 0

#define BIT_0 0x01
#define BIT_1 0x02
#define BIT_2 0x04
#define BIT_3 0x08
#define BIT_4 0x10
#define BIT_5 0x20
#define BIT_6 0x40
#define BIT_7 0x80

/* filter types */
enum {
    LPF, /* low pass filter */
    HPF, /* High pass filter */
    BPF, /* band pass filter */
    NOTCH, /* Notch Filter */
    PEQ, /* Peaking band EQ filter */
    LSH, /* Low shelf filter */
    HSH /* High shelf filter */
};

/* Simple implementation of Biquad filters -- Tom St Denis
 *
 * Based on the work

Cookbook formulae for audio EQ biquad filter coefficients
---------------------------------------------------------
by Robert Bristow-Johnson, pbjrbj@viconet.com  a.k.a. robert@audioheads.com

 * Available on the web at

http://www.smartelectronix.com/musicdsp/text/filters005.txt

 * Enjoy.
 *
 * This work is hereby placed in the public domain for all purposes, whether
 * commercial, free [as in speech] or educational, etc.  Use the code and please
 * give me credit if you wish.
 *
 * Tom St Denis -- http://tomstdenis.home.dhs.org
*/

#ifndef M_LN2
#define M_LN2	   0.69314718055994530942
#endif

#ifndef M_PI
#define M_PI		3.14159265358979323846
#endif


/* Below this would be biquad.c */
/* Computes a BiQuad filter on a sample */
double biquad(double sample, Biquad * b)
{
    double result;

    /* compute result */
    result = b->a0 * sample + b->a1 * b->x1 + b->a2 * b->x2 -
        b->a3 * b->y1 - b->a4 * b->y2;

    /* shift x1 to x2, sample to x1 */
    b->x2 = b->x1;
    b->x1 = sample;

    /* shift y1 to y2, result to y1 */
    b->y2 = b->y1;
    b->y1 = result;

    return result;
}

/* sets up a BiQuad Filter */
void biquad_init(Biquad* b, int type, double dbGain, double freq,
double srate, double bandwidth)
{
    double A, omega, sn, cs, alpha, beta;
    double a0, a1, a2, b0, b1, b2;

    /* setup variables */
    A = pow(10, dbGain /40);
    omega = 2 * M_PI * freq /srate;
    sn = sin(omega);
    cs = cos(omega);
    alpha = sn * sinh(M_LN2 /2 * bandwidth * omega /sn);
    beta = sqrt(A + A);

    switch (type) {
    case LPF:
        b0 = (1 - cs) /2;
        b1 = 1 - cs;
        b2 = (1 - cs) /2;
        a0 = 1 + alpha;
        a1 = -2 * cs;
        a2 = 1 - alpha;
        break;
    case HPF:
        b0 = (1 + cs) /2;
        b1 = -(1 + cs);
        b2 = (1 + cs) /2;
        a0 = 1 + alpha;
        a1 = -2 * cs;
        a2 = 1 - alpha;
        break;
    case BPF:
        b0 = alpha;
        b1 = 0;
        b2 = -alpha;
        a0 = 1 + alpha;
        a1 = -2 * cs;
        a2 = 1 - alpha;
        break;
    case NOTCH:
        b0 = 1;
        b1 = -2 * cs;
        b2 = 1;
        a0 = 1 + alpha;
        a1 = -2 * cs;
        a2 = 1 - alpha;
        break;
    case PEQ:
        b0 = 1 + (alpha * A);
        b1 = -2 * cs;
        b2 = 1 - (alpha * A);
        a0 = 1 + (alpha /A);
        a1 = -2 * cs;
        a2 = 1 - (alpha /A);
        break;
    case LSH:
        b0 = A * ((A + 1) - (A - 1) * cs + beta * sn);
        b1 = 2 * A * ((A - 1) - (A + 1) * cs);
        b2 = A * ((A + 1) - (A - 1) * cs - beta * sn);
        a0 = (A + 1) + (A - 1) * cs + beta * sn;
        a1 = -2 * ((A - 1) + (A + 1) * cs);
        a2 = (A + 1) + (A - 1) * cs - beta * sn;
        break;
    case HSH:
        b0 = A * ((A + 1) + (A - 1) * cs + beta * sn);
        b1 = -2 * A * ((A - 1) + (A + 1) * cs);
        b2 = A * ((A + 1) + (A - 1) * cs - beta * sn);
        a0 = (A + 1) - (A - 1) * cs + beta * sn;
        a1 = 2 * ((A - 1) - (A + 1) * cs);
        a2 = (A + 1) - (A - 1) * cs - beta * sn;
        break;
    default:
        return;
    }

    /* precompute the coefficients */
    b->a0 = b0 /a0;
    b->a1 = b1 /a0;
    b->a2 = b2 /a0;
    b->a3 = a1 /a0;
    b->a4 = a2 /a0;

    /* zero initial samples */
    b->x1 = b->x2 = 0;
    b->y1 = b->y2 = 0;
}



static const uint8_t length_counter_lookup[32] = {
    // HI/LO 0   1   2   3   4   5   6   7    8   9   A   B   C   D   E   F
    // ----------------------------------------------------------------------
    /* 0 */ 10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    /* 1 */ 12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

static uint8_t duty[4][8] =
{
    {0, 1, 0, 0, 0, 0, 0, 0}, // 12.5 %
    {0, 1, 1, 0, 0, 0, 0, 0}, // 25 %
    {0, 1, 1, 1, 1, 0, 0, 0}, // 50 %
    {1, 0, 0, 1, 1, 1, 1, 1} // 25 % negated
};

static uint8_t tri_sequence[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

static uint16_t noise_period_lookup_NTSC[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

/*
Rate   $0   $1   $2   $3   $4   $5   $6   $7   $8   $9   $A   $B   $C   $D   $E   $F
      ------------------------------------------------------------------------------
NTSC  428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106,  84,  72,  54
PAL   398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118,  98,  78,  66,  50
*/

static uint16_t dmc_rate_index_NTSC[16] = {
    428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106,  84,  72,  54
};


/*
Mode 0: 4-step sequence

Action      Envelopes &     Length Counter& Interrupt   Delay to next
            Linear Counter  Sweep Units     Flag        NTSC     PAL
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
$4017=$00   -               -               -           -       -
Step 1      Clock           -               -           7457    8313
Step 2      Clock           Clock           -           14913   16627
Step 3      Clock           -               -           22371   24939
                                        Set if enabled  29828   33252
Step 4      Clock           Clock       Set if enabled  29829   33253
                                        Set if enabled  0       0

Mode 1: 5-step sequence

Action      Envelopes &     Length Counter& Interrupt   CPU cycle
            Linear Counter  Sweep Units     Flag        NTSC     PAL
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
$4017=$80   -               -               -           -       -
Step 1      Clock           -               -           7457    8313
Step 2      Clock           Clock           -           14913   16627
Step 3      Clock           -               -           22371   24939
Step 4      -               -               -           29829   33253
Step 5      Clock           Clock           -           37281   41565
            -               -               -           0       0
 */


static float tnd_LUT[TND_LUT_SIZE];
static float pulse_LUT[PULSE_LUT_SIZE];

static void compute_mixer_LUT();

static void init_audio_device(const APU* apu);

static void init_pulse(Pulse *pulse, uint8_t id);

static void init_triangle(Triangle *triangle);

static void init_noise(Noise *noise);

static void init_dmc(DMC* dmc);

static void init_sampler(APU* apu, int frequency);

static void length_sweep_pulse(Pulse *pulse);

static uint8_t clock_divider(Divider *divider);

static uint8_t clock_triangle(Triangle *triangle);

static uint8_t clock_divider_inverse(Divider *divider);

static void update_target_period(Pulse* pulse);

static void clock_dmc(APU* apu);

static void quarter_frame(APU *apu);

static void half_frame(APU *apu);

static void sample(APU* apu);

void init_APU(APU *apu) {
    memset(apu, 0, sizeof(APU));
    compute_mixer_LUT();
    apu->volume = 1;
    apu->cycles = 0;
    apu->sequencer = 0;
    apu->reset_sequencer = 0;
    apu->audio_start = 0;
    apu->IRQ_inhibit = 0;

    // For keeping track of queue_size statistics for use by the adaptive sampler
    memset(apu->stat_window, 0, sizeof(apu->stat_window));
    apu->stat = 0;
    apu->stat_index = 0;

    init_pulse(&apu->pulse1, 1);
    init_pulse(&apu->pulse2, 2);
    init_triangle(&apu->triangle);
    init_noise(&apu->noise);
    init_dmc(&apu->dmc);
    init_sampler(apu, SAMPLING_FREQUENCY);
    init_audio_device(apu);
    set_status(apu, 0);
    set_frame_counter_ctrl(apu, 0);
}

void reset_APU(APU *apu) {
    set_status(apu, 0);
    apu->triangle.sequencer.step = 0;
    apu->dmc.counter &= 1;
    apu->frame_interrupt = 0;
}

void init_audio_device(const APU* apu) {
}

void exit_APU() {
}

void execute_apu(APU *apu) {
    // Perform necessary reset after $4017 write
    if (apu->reset_sequencer) {
        apu->reset_sequencer = 0;
        if (apu->frame_mode == 1) {
            quarter_frame(apu);
            half_frame(apu);
        }
        apu->sequencer = 0;
        goto post_sequencer;
    }

        switch (apu->sequencer) {
            case 0:
                apu->sequencer++;
                break;
            case 7457:
                quarter_frame(apu);
                apu->sequencer++;
                break;
            case 14913:
                quarter_frame(apu);
                half_frame(apu);
                apu->sequencer++;
                break;
            case 22371:
                quarter_frame(apu);
                apu->sequencer++;
                break;
            case 29828:
                apu->sequencer++;
                break;
            case 29829:
                if (apu->frame_mode == 1) {
                    apu->sequencer++;
                    break;
                }

                quarter_frame(apu);
                half_frame(apu);

                if (!apu->IRQ_inhibit) {
                    apu->frame_interrupt = 1;
                    apu->doIRQ = 1;
                }
                apu->sequencer = 0;
                break;
            case 37281:
                quarter_frame(apu);
                half_frame(apu);
                apu->sequencer = 0;
                break;
            default:
                apu->sequencer++;
        }
    

post_sequencer:

    if (apu->cycles & 1) {
        // channel sequencer
        clock_divider(&apu->pulse1.t);
        clock_divider(&apu->pulse2.t);

        // noise timer
        if (clock_divider(&apu->noise.timer)) {
            Noise *noise = &apu->noise;
            uint8_t feedback = (noise->shift & BIT_0) ^ (((noise->mode ? BIT_6 : BIT_1) & noise->shift) > 0);
            noise->shift >>= 1;
            noise->shift |= feedback ? (1 << 14) : 0;
        }
    }

    // DMC
    clock_dmc(apu);

    // triangle timer
    clock_triangle(&apu->triangle);

    // sample
    sample(apu);

    apu->cycles++;
}

void quarter_frame(APU *apu) {
    Triangle *triangle = &apu->triangle;
    //envelope
    clock_divider_inverse(&apu->pulse1.envelope);
    clock_divider_inverse(&apu->pulse2.envelope);
    clock_divider_inverse(&apu->noise.envelope);

    // triangle linear counter
    if (triangle->linear_reload_flag)
        triangle->linear_counter = triangle->linear_reload;
    else if (triangle->linear_counter)
        triangle->linear_counter--;
    // if halt is clear, clear linear reload flag
    triangle->linear_reload_flag = triangle->halt ? triangle->linear_reload_flag : 0;
}

void half_frame(APU *apu) {
    // length and sweep
    length_sweep_pulse(&apu->pulse1);
    length_sweep_pulse(&apu->pulse2);
    // triangle length counter
    if (!apu->triangle.halt && apu->triangle.length_counter) {
        apu->triangle.length_counter--;
    }
    // noise length counter
    Noise *noise = &apu->noise;
    if (noise->l && !noise->envelope.loop)
        noise->l--;
}

void init_sampler(APU* apu, int frequency) {
    float cycles_per_frame = 29780.5;
    float rate = 60.0f;
    Sampler* sampler = &apu->sampler;
    // Q = 0.707 => BW = 1.414 (1 octave)
    biquad_init(&apu->filter, HPF, 0, 20, frequency, 1);
    // anti-aliasing filter.
    biquad_init(&apu->aa_filter, LPF, 0, 20000, cycles_per_frame * rate, 1);

    sampler->max_period = cycles_per_frame * rate / frequency;
    sampler->min_period = sampler->max_period - 1;
    sampler->period = sampler->min_period;
    sampler->index = 0;
    sampler->max_index = AUDIO_BUFF_SIZE;
    sampler->samples = 0;
    sampler->counter = 0;
    sampler->factor_index = 0;
    // basically the precision with which we vary the sampling rate
    // 100 ->2 d.p, 1000->3 d.p, etc.
    sampler->max_factor = 100;
    // this may need to be calibrated to suit the current sampling frequency
    // the current equilibrium is for 48000 hz
    sampler->target_factor = sampler->equilibrium_factor = 48;
}


void sample(APU* apu) {
    float sample = biquad(get_sample(apu), &apu->aa_filter);
#if AVERAGE_DOWNSAMPLING
    static float avg = -1;
    // average samples in a bin
    if(avg < 0)
        avg = sample;
    else
        avg = (avg + sample)/2;
#endif

    Sampler* sampler = &apu->sampler;
    sampler->counter++;
    if(sampler->counter >= sampler->period) {
#if AVERAGE_DOWNSAMPLING
        apu->buff[sampler->index++] = 32767 * biquad(avg, &apu->filter);
        // begin fresh average for the next bin
        avg = -1;
#else

        apu->buff[sampler->index++] = 32000 * biquad(sample, &apu->filter) * apu->volume;
#endif
        if(sampler->index >= sampler->max_index) {
            sampler->index = 0;
        }
        sampler->samples++;
        sampler->counter = 0;
        if(apu->sampler.factor_index <= apu->sampler.target_factor) {
            sampler->period = sampler->max_period;
        }else {
            sampler->period = sampler->min_period;
        }
        sampler->factor_index++;
        if(sampler->factor_index > sampler->max_factor) {
            sampler->factor_index = 0;
        }
    }
}


void queue_audio(APU* apu, int16_t *outBuff, int *outSize) {
    Sampler* s = &apu->sampler;

    memcpy(outBuff, apu->buff, AUDIO_BUFF_SIZE * 2);
    *outSize = s->index;
    memset(apu->buff, 0, AUDIO_BUFF_SIZE * 2);
    // reset sampler
    s->index = 0;
}


float get_sample(APU *apu) {
    uint8_t pulse_out = 0, tnd_out = 0;

    if (apu->pulse1.enabled && apu->pulse1.l && !apu->pulse1.mute)
        pulse_out += (apu->pulse1.const_volume ? apu->pulse1.envelope.period : apu->pulse1.envelope.step) * (duty[apu->
            pulse1.duty][apu->pulse1.t.step]);

    if (apu->pulse2.enabled && apu->pulse2.l && !apu->pulse2.mute)
        pulse_out += (apu->pulse2.const_volume ? apu->pulse2.envelope.period : apu->pulse2.envelope.step) * (duty[apu->
            pulse2.duty][apu->pulse2.t.step]);

    if (apu->triangle.enabled && apu->triangle.sequencer.period > 1)
        tnd_out += tri_sequence[apu->triangle.sequencer.step] * 3;

    if (apu->noise.enabled && !(apu->noise.shift & BIT_0) && apu->noise.l > 0)
        tnd_out += 2 * (apu->noise.const_volume ? apu->noise.envelope.period : apu->noise.envelope.step);

    tnd_out += apu->dmc.counter;

    float amp = pulse_LUT[pulse_out] + tnd_LUT[tnd_out];

    // clamp to within 1 just in case
    return amp > 1 ? 1 : amp;
}


void set_status(APU *apu, uint8_t value) {
    apu->pulse1.enabled = (value & BIT_0) > 0;
    apu->pulse2.enabled = (value & BIT_1) > 0;
    apu->triangle.enabled = (value & BIT_2) > 0;
    apu->noise.enabled = (value & BIT_3) > 0;
    apu->dmc.enabled = (value & BIT_4) > 0;

    if(apu->dmc.enabled && apu->dmc.bytes_remaining == 0) {
        // restart it
        apu->dmc.bytes_remaining = apu->dmc.sample_length;
        apu->dmc.current_addr = apu->dmc.sample_addr;
    }else if(!apu->dmc.enabled) {
        apu->dmc.bytes_remaining = 0;
    }
    apu->dmc.interrupt = 0;


    // reset length counters if disabled
    apu->pulse1.l = apu->pulse1.enabled ? apu->pulse1.l : 0;
    apu->pulse2.l = apu->pulse2.enabled ? apu->pulse2.l : 0;
    apu->triangle.length_counter = apu->triangle.enabled ? apu->triangle.length_counter : 0;
    apu->noise.l = apu->noise.enabled ? apu->noise.l : 0;
}


uint8_t read_apu_status(APU *apu) {
    uint8_t status = (apu->pulse1.l > 0);
    status |= (apu->pulse2.l > 0 ? BIT_1 : 0);
    status |= (apu->triangle.length_counter > 0 ? BIT_2 : 0);
    status |= (apu->noise.l > 0 ? BIT_3 : 0);
    status |= (apu->frame_interrupt ? BIT_6 : 0);
    status |= (apu->dmc.interrupt ? BIT_7 : 0);
    status |= (apu->dmc.bytes_remaining? BIT_4: 0);
    // clear frame interrupt
    apu->frame_interrupt = 0;
    return status;
}


void set_frame_counter_ctrl(APU *apu, uint8_t value) {
    // $4017
    apu->IRQ_inhibit = (value & BIT_6) > 0;
    apu->frame_mode = (value & BIT_7) > 0;
    // clear interrupt if IRQ disable set
    if (value & BIT_6)
        apu->frame_interrupt = 0;
    apu->reset_sequencer = 1;
}

void set_pulse_ctrl(Pulse *pulse, uint8_t value) {
    pulse->const_volume = (value & BIT_4) > 0;
    pulse->envelope.loop = (value & BIT_5) > 0;
    pulse->envelope.period = value & 0xF;
    pulse->envelope.counter = pulse->envelope.period;
    // reload divider step counter
    // this should be set on next envelope clock but this will do for now
    pulse->envelope.step = 15;
    pulse->duty = value >> 6;
}

void set_pulse_timer(Pulse *pulse, uint8_t value) {
    pulse->t.period = pulse->t.period & ~0xff | value;
    update_target_period(pulse);
}

void set_pulse_sweep(Pulse *pulse, uint8_t value) {
    pulse->enable_sweep = (value & BIT_7) > 0;
    pulse->sweep.period = ((value & PULSE_PERIOD) >> 4) + 1;
    pulse->sweep.counter = pulse->sweep.period;
    pulse->shift = value & PULSE_SHIFT;
    pulse->neg = value & BIT_3;
    pulse->sweep_reload = 1;
    update_target_period(pulse);
}

void set_pulse_length_counter(Pulse *pulse, uint8_t value) {
    pulse->t.period = pulse->t.period & 0xff | (value & 0x7) << 8;
    if (pulse->enabled)
        pulse->l = length_counter_lookup[value >> 3];
    update_target_period(pulse);
    pulse->envelope.step = 15;
}

void set_tri_counter(Triangle *triangle, uint8_t value) {
    triangle->linear_reload = value & 0x7f;
    triangle->halt = (value & BIT_7) > 0;
}

void set_tri_timer_low(Triangle *triangle, uint8_t value) {
    triangle->sequencer.period = triangle->sequencer.period & ~0xff | value;
}

void set_tri_length(Triangle *triangle, uint8_t value) {
    triangle->sequencer.period = triangle->sequencer.period & 0xff | (value & 0x7) << 8;
    triangle->linear_reload_flag = 1;
    if (triangle->enabled)
        triangle->length_counter = length_counter_lookup[value >> 3];
}

void set_noise_ctrl(Noise *noise, uint8_t value) {
    noise->const_volume = (value & BIT_4) > 0;
    noise->envelope.loop = (value & BIT_5) > 0;
    noise->envelope.period = value & 0xF;
}

void set_noise_period(APU* apu, uint8_t value) {
    Noise* noise = &apu->noise;
    noise->timer.period = noise_period_lookup_NTSC[value & 0xF];
    noise->mode = (value & BIT_7) > 0;
}

void set_noise_length(Noise *noise, uint8_t value) {
    if (noise->enabled)
        noise->l = length_counter_lookup[value >> 3];
    noise->envelope.step = 15;
}

void set_dmc_ctrl(APU* apu, uint8_t value) {
    apu->dmc.loop = (value & BIT_6) > 0;
    apu->dmc.IRQ_enable = (value & BIT_7) > 0;
    if(!apu->dmc.IRQ_enable) {
        apu->dmc.interrupt = 0;
    }
    apu->dmc.rate = dmc_rate_index_NTSC[value & 0xf] - 1;
}

void set_dmc_da(DMC* dmc, uint8_t value) {
    dmc->counter = value & 0x7F;
}

void set_dmc_addr(DMC* dmc, uint8_t value) {
    dmc->sample_addr = 0xC000 + (uint16_t)value * 64;
}

void set_dmc_length(DMC* dmc, uint8_t value) {
    dmc->sample_length = (uint16_t)value * 16 + 1;
}

void clock_dmc(APU* apu) {
    DMC* dmc = &apu->dmc;

    if(dmc->enabled && dmc->empty) {
        if(dmc->bytes_remaining > 0) {
            apu->doTickSkip = 1;
            dmc->sample = apu->GetMemoryFunc(dmc->current_addr, apu->userdata);
            dmc->empty = 0;
            dmc->bytes_remaining--;
            if(dmc->current_addr == 0xffff)
                dmc->current_addr = 0x8000;
            else
                dmc->current_addr++;
            dmc->irq_set = 0;
        }
        if(dmc->bytes_remaining == 0) {
            if(dmc->loop) {
                dmc->current_addr = dmc->sample_addr;
                dmc->bytes_remaining = dmc->sample_length;
            }else if(dmc->IRQ_enable && !dmc->irq_set) {
                dmc->interrupt = 1;
                dmc->irq_set = 1;
                apu->doIRQ = 1;
            }
        }
    }

    if(dmc->rate_index > 0) {
        dmc->rate_index--;
        return;
    }
    dmc->rate_index = dmc->rate;

    if(dmc->bits_remaining > 0) {
        // clamped counter update
        if(!dmc->silence) {
            if(dmc->bits & 1) {
                dmc->counter+=2;
                dmc->counter = dmc->counter > 127 ? 127 : dmc->counter;
            }
            else if(dmc->counter > 1)
                dmc->counter-=2;
            dmc->bits >>= 1;
        }
        dmc->bits_remaining--;
    }
    if(dmc->bits_remaining == 0) {
        if(dmc->empty)
            dmc->silence = 1;
        else {
            dmc->bits = dmc->sample;
            dmc->empty = 1;
            dmc->silence = 0;
        }
        dmc->bits_remaining = 8;
    }
}

static void compute_mixer_LUT() {
    pulse_LUT[0] = 0;
    for (int i = 1; i < PULSE_LUT_SIZE; i++)
        pulse_LUT[i] = 95.52f / (8128.0f / (float) i + 100);
    tnd_LUT[0] = 0;
    for (int i = 1; i < TND_LUT_SIZE; i++)
        tnd_LUT[i] = 163.67f / (24329.0f / (float) i + 100);
}

static void init_pulse(Pulse *pulse, uint8_t id) {
    pulse->id = id;
    // start with large period until its set
    pulse->t.step = 0;
    pulse->t.from = 0;
    pulse->t.limit = 7;
    pulse->t.loop = 1;
    pulse->sweep.limit = 0;
    pulse->enabled = 0;
    pulse->sweep_reload = 0;
}

static void init_triangle(Triangle *triangle) {
    triangle->sequencer.step = 0;
    triangle->sequencer.limit = 31;
    triangle->sequencer.from = 0;
    triangle->enabled = 0;
    triangle->halt = 1;
}

static void init_noise(Noise *noise) {
    noise->enabled = 0;
    noise->timer.limit = 0;
    noise->shift = 1;
}

static void init_dmc(DMC* dmc) {
    dmc->empty = 1;
    dmc->silence = 1;
}

static uint8_t clock_divider(Divider *divider) {
    if (divider->counter) {
        divider->counter--;
        return 0;
    }

    divider->counter = divider->period;
    divider->step++;
    if (divider->limit && divider->step > divider->limit)
        divider->step = divider->from;
    // trigger clock
    return 1;
}

static uint8_t clock_triangle(Triangle *triangle) {
    Divider *divider = &triangle->sequencer;
    if (divider->counter) {
        divider->counter--;
        return 0;
    }

    divider->counter = divider->period;
    if (triangle->length_counter && triangle->linear_counter)
        divider->step++;
    if (divider->limit && divider->step > divider->limit)
        divider->step = divider->from;
    // trigger clock
    return 1;
}

static uint8_t clock_divider_inverse(Divider *divider) {
    if (divider->counter) {
        divider->counter--;
        return 0;
    }
    divider->counter = divider->period;
    if (divider->limit && divider->step == 0 && divider->loop)
        divider->step = divider->limit;
    else if (divider->step)
        divider->step--;
    // trigger clock
    return 1;
}

static void update_target_period(Pulse* pulse) {
    int change = pulse->t.period >> pulse->shift;
    change = pulse->neg ? pulse->id == 1 ? - change - 1 : -change : change;
    // add 1 (2's complement) for pulse 2
    change = pulse->t.period + change;
    pulse->target_period = change < 0 ? 0 : change;
    if(pulse->t.period < 8 || pulse->target_period > 0x7ff) {
        pulse->mute = 1;
    }else {
        pulse->mute = 0;
    }
}

static void length_sweep_pulse(Pulse *pulse) {
    if (pulse->sweep_reload) {
        // trigger a reload
        pulse->sweep_reload = 0;
        pulse->sweep.counter = 0;
    }

    if(clock_divider(&pulse->sweep)) {
        if(pulse->enable_sweep && pulse->shift > 0 && !pulse->mute) {
            pulse->t.period = pulse->target_period;
            update_target_period(pulse);
        }
    }

    // length counter
    if (pulse->l && !pulse->envelope.loop)
        pulse->l--;
}

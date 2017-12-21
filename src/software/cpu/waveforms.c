/*
 * Copyright 2007-2017 by Erik Hofman.
 * Copyright 2009-2017 by Adalin B.V.
 *
 * This file is part of AeonWave
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_RMALLOC_H
# include <rmalloc.h>
#else
# include <stdlib.h>
#endif
#include <math.h>	/* for floorf() */
#include <time.h>	/* for time() */
#include <assert.h>

#include <base/gmath.h>
#include <base/types.h>

#include <api.h>
#include <arch.h>
#include <analyze.h>

#define MAX_HARMONICS		16

static float _gains[MAX_WAVE];

static float _aax_rand_sample();
static unsigned int WELLRNG512(void);
static void _aax_pinknoise_filter(float32_ptr, size_t, float);
static void _aax_resample_float(float32_ptr, const_float32_ptr, size_t, float);
static void _aax_add_data(void_ptrptr, const_float32_ptr, int, unsigned int, char, float);
static void _aax_mul_data(void_ptrptr, const_float32_ptr, int, unsigned int, char, float);
static float* _aax_generate_waveform(size_t, float, float, float, float*);
static float* _aax_generate_noise(size_t, float, unsigned char);


void
_bufferMixSineWave(void** data, float freq, char bps, size_t no_samples, int tracks, float gain, float phase)
{
   char ringmodulate = (gain < 0.0f) ? 1 : 0;
   gain = fabsf(gain) * _gains[_SINE_WAVE];
   if (data && gain)
   {
      float *ptr = _aax_generate_waveform(no_samples, freq, phase, gain, _harmonics[_SINE_WAVE]);
      if (ptr)
      {
         if (ringmodulate) {
            _aax_mul_data(data, ptr, tracks, no_samples, bps, gain);
         } else {
            _aax_add_data(data, ptr, tracks, no_samples, bps, gain);
         }
         _aax_aligned_free(ptr);
      }
   }
}

void
_bufferMixSquareWave(void** data, float freq, char bps, size_t no_samples, int tracks, float gain, float phase)
{
   char ringmodulate = (gain < 0.0f) ? 1 : 0;
   gain = fabsf(gain) * _gains[_SQUARE_WAVE];
   if (data && gain)
   {
      float *ptr = _aax_generate_waveform(no_samples, freq, phase, gain, _harmonics[_SQUARE_WAVE]);
      if (ptr)
      {
         if (ringmodulate) {
            _aax_mul_data(data, ptr, tracks, no_samples, bps, gain);
         } else {
            _aax_add_data(data, ptr, tracks, no_samples, bps, gain);
         }
         _aax_aligned_free(ptr);
      }
   }
}

void
_bufferMixTriangleWave(void** data, float freq, char bps, size_t no_samples, int tracks, float gain, float phase)
{
   char ringmodulate = (gain < 0.0f) ? 1 : 0;
   gain = fabsf(gain) * _gains[_TRIANGLE_WAVE];
   if (data && gain)
   {
      float *ptr = _aax_generate_waveform(no_samples, freq, phase, gain, _harmonics[_TRIANGLE_WAVE]);
      if (ptr)
      {
         if (ringmodulate) {
            _aax_mul_data(data, ptr, tracks, no_samples, bps, gain);
         } else {
            _aax_add_data(data, ptr, tracks, no_samples, bps, gain);
         }
         _aax_aligned_free(ptr);
      }
   }
}

void
_bufferMixSawtooth(void** data, float freq, char bps, size_t no_samples, int tracks, float gain, float phase)
{
   char ringmodulate = (gain < 0.0f) ? 1 : 0;
   gain = fabsf(gain) * _gains[_SAWTOOTH_WAVE];
   if (data && gain)
   {
      float *ptr = _aax_generate_waveform(no_samples, freq, phase, gain, _harmonics[_SAWTOOTH_WAVE]);
      if (ptr)
      {
         if (ringmodulate) {
            _aax_mul_data(data, ptr, tracks, no_samples, bps, gain);
         } else {
            _aax_add_data(data, ptr, tracks, no_samples, bps, gain);
         }
         _aax_aligned_free(ptr);
      }
   }
}

void
_bufferMixImpulse(void** data, float freq, char bps, size_t no_samples, int tracks, float gain, float phase)
{
   char ringmodulate = (gain < 0.0f) ? 1 : 0;
   gain = fabsf(gain) * _gains[_IMPULSE_WAVE];
   if (data && gain)
   {
      float *ptr = _aax_generate_waveform(no_samples, freq, phase, gain, _harmonics[_IMPULSE_WAVE]);
      if (ptr)
      {
         if (ringmodulate) {
            _aax_mul_data(data, ptr, tracks, no_samples, bps, gain);
         } else {
            _aax_add_data(data, ptr, tracks, no_samples, bps, gain);
         }
         _aax_aligned_free(ptr);
      }
   }
}

void
_bufferMixWhiteNoise(void** data, size_t no_samples, char bps, int tracks, float pitch, float gain, unsigned char skip)
{
   char ringmodulate = (gain < 0.0f) ? 1 : 0;
   gain = fabsf(gain);
   if (data && gain)
   {
      float *ptr2 = _aax_generate_noise(pitch*no_samples, gain, skip);
      float *ptr = _aax_aligned_alloc(no_samples*sizeof(float));

      if (ptr && ptr2)
      {
         _aax_resample_float(ptr, ptr2, no_samples, pitch);
         if (ringmodulate) {
            _aax_mul_data(data, ptr, tracks, no_samples, bps, gain);
         } else {
            _aax_add_data(data, ptr, tracks, no_samples, bps, gain);
         }

         _aax_aligned_free(ptr);
         _aax_aligned_free(ptr2);
      }
   }
}

void
_bufferMixPinkNoise(void** data, size_t no_samples, char bps, int tracks, float pitch, float gain, float fs, unsigned char skip)
{
   size_t noise_samples = pitch*no_samples;
   char ringmodulate = (gain < 0.0f) ? 1 : 0;
   gain = fabsf(gain);
   if (data && gain)
   {	// _aax_pinknoise_filter requires twice noise_samples buffer space
      float *ptr2 = _aax_generate_noise(2*noise_samples, gain, skip);
      float *ptr = _aax_aligned_alloc(no_samples*sizeof(float));

      if (ptr && ptr2)
      {
         _aax_pinknoise_filter(ptr2, noise_samples, fs);
         _batch_fmul_value(ptr2, sizeof(float), noise_samples, 1.5f);
         _aax_resample_float(ptr, ptr2, no_samples, pitch);

         if (ringmodulate) {
            _aax_mul_data(data, ptr, tracks, no_samples, bps, gain);
         } else {
            _aax_add_data(data, ptr, tracks, no_samples, bps, gain);
         }

         _aax_aligned_free(ptr);
         _aax_aligned_free(ptr2);
      }
   }
}

void
_bufferMixBrownianNoise(void** data, size_t no_samples, char bps, int tracks, float pitch, float gain, float fs, unsigned char skip)
{
   size_t noise_samples = pitch*no_samples;
   char ringmodulate = (gain < 0.0f) ? 1 : 0;
   gain = fabsf(gain);
   if (data && gain)
   {
      float *ptr2 = _aax_generate_noise(noise_samples, gain, skip);
      float *ptr = _aax_aligned_alloc(no_samples*sizeof(float));

      if (ptr && ptr2)
      {
         float k = _aax_movingaverage_compute(100.0f, fs);
         float hist = 0.0f;

         _batch_movingaverage_float(ptr2, ptr2, noise_samples, &hist, k);
         _batch_fmul_value(ptr2, sizeof(int32_t), noise_samples, 3.5f);
         _aax_resample_float(ptr, ptr2, no_samples, pitch);

         if (ringmodulate) {
            _aax_mul_data(data, ptr, tracks, no_samples, bps, gain);
         } else {
            _aax_add_data(data, ptr, tracks, no_samples, bps, gain);
         }

         _aax_aligned_free(ptr);
         _aax_aligned_free(ptr2);
      }
   }
}

/* -------------------------------------------------------------------------- */

static float _gains[MAX_WAVE] = { 0.95f, 0.9f, 0.7f, 1.1f, 1.0f };

float _harmonics[MAX_WAVE][_AAX_SYNTH_MAX_HARMONICS] =
{
  /* _SQUARE_WAVE */
  { 1.f, 0.0f, 1.f/3.f, 0.f, 1.f/5.f, 0.f, 1.f/7.f, 0.f,
    1.f/9.f, 0.f, 1.f/11.f, 0.f, 1.f/13.f, 0.f, 1.f/15.f, 0.f },

  /* _TRIANGLE_WAVE */
  { 1.f, 0.f, -1.f/9.f, 0.f, 1.f/25.f, 0.f, -1.f/49.f, 0.f,
    1.f/81.f, 0.f, -1.f/121.f, 0.f, 1.f/169.f, 0.0f, -1.f/225.f, 0.f },

  /* _SAWTOOTH_WAVE */
  { 1.f, 1.f/2.f, 1.f/3.f, 1.f/4.f, 1.f/5.f, 1.f/6.f, 1.f/7.f, 1.f/8.f,
    1.f/9.f, 1.f/10.f, 1.f/11.f, 1.f/12.f, 1.f/13.f, 1.f/14.f, 1.f/15.f,
    1.f/16.f},

  /* _IMPULSE_WAVE */
  { 1.f/16.f, 1.f/16.f, 1.f/16.f, 1.f/16.f, 1.f/16.f, 1.f/16.f, 1.f/16.f,
    1.f/16.f, 1.f/16.f, 1.f/16.f, 1.f/16.f, 1.f/16.f, 1.f/16.f, 1.f/16.f,
    1.f/16.f, 1.f/16.f },

  /* _SINE_WAVE */
  { 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
    0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f }

};

/** MT199367                                                                  *
 * "Cleaned up" and simplified Mersenne Twister implementation.               *
 * Vastly smaller and more easily understood and embedded.  Stores the        *
 * state in a user-maintained structure instead of static memory, so          *
 * you can have more than one, or save snapshots of the RNG state.            *
 * Lacks the "init_by_array()" feature of the original code in favor          *
 * of the simpler 32 bit seed initialization.                                 *
 * Verified to be identical to the original MT199367ar code through           *
 * the first 10M generated numbers.                                           *
 *                                                                            *
 * Note: Code Taken from SimGear.                                             *
 *       The original Mersenne Twister implementation is in public domain.    *
 */
#define MT_N 624
#define MT_M 397
#define MT(i) mt->array[i]

typedef struct {unsigned int array[MT_N]; int index; } mt;
static mt random_seed;

static void
mt_init(mt *mt, unsigned int seed)
{
   int i;
   MT(0)= seed;
   for(i=1; i<MT_N; i++) {
      MT(i) = (1812433253 * (MT(i-1) ^ (MT(i-1) >> 30)) + i);
   }
   mt->index = MT_N+1;
}

static unsigned int
mt_rand32(mt *mt)
{
   unsigned int i, y;
   if(mt->index >= MT_N)
   {
      for(i=0; i<MT_N; i++)
      {
         y = (MT(i) & 0x80000000) | (MT((i+1)%MT_N) & 0x7fffffff);
         MT(i) = MT((i+MT_M)%MT_N) ^ (y>>1) ^ (y&1 ? 0x9908b0df : 0);
      }
      mt->index = 0;
   }
   y = MT(mt->index++);
   y ^= (y >> 11);
   y ^= (y << 7) & 0x9d2c5680;
   y ^= (y << 15) & 0xefc60000;
   y ^= (y >> 18);
   return y;
}

/** WELL 512, Note: also in Public Domain */
#define MAX_RANDOM		4294967295.0f
#define MAX_RANDOM_2		(MAX_RANDOM/2)

#define _aax_random()		((float)WELLRNG512()/MAX_RANDOM)

static unsigned long state[16];
static unsigned int idx = 0;

static unsigned int
WELLRNG512(void)
{
   unsigned int a, b, c, d;
   a = state[idx];
   c = state[(idx+13) & 15];
   b = a^c^(a<<16)^(c<<15);
   c = state[(idx+9) & 15];
   c ^= (c>>11);
   a = state[idx] = b^c;
   d = a^((a<<5) & 0xDA442D24UL);
   idx = (idx + 15) & 15;
   a = state[idx];
   state[idx] = a^b^d^(a<<2)^(b<<18)^(c<<28);

   return state[idx];
}

static void
_aax_srandom()
{
   static int init = -1;
   if (init < 0)
   {
      unsigned int i, num;

      mt_init(&random_seed, time(NULL));
      num = 100 + (mt_rand32(&random_seed) & 255);
      for (i=0; i<num; i++) {
          mt_rand32(&random_seed);
      }

      for (i=0; i<15; i++) {
         state[i] = mt_rand32(&random_seed);
      }

      num = 1024+(WELLRNG512()>>22);
      for (i=0; i<num; i++) {
         WELLRNG512();
      }
   }
}

#define AVG     14
#define MAX_AVG 64
static float
_aax_rand_sample()
{
   static unsigned int rvals[MAX_AVG];
   static int init = 1;
   unsigned int r, p;
   float rv;

   if (init)
   {
      p = MAX_AVG-1;
      do
      {
         r = AVG;
         rv = 0.0f;
         do {
            rv += WELLRNG512();
         } while(--r);
         r = (unsigned int)rintf(rv/AVG);
         rvals[p] = r;
      }
      while(p--);
      init = 0;
   }

   r = AVG;
   rv = 0.0f;
   do {
      rv += WELLRNG512();
   } while(--r);
   r = (unsigned int)rintf(rv/AVG);

   p = (r >> 2) & (MAX_AVG-1);
   rv = rvals[p];
   rvals[p] = r;

   rv = 2.0f*(-1.0f + rv/MAX_RANDOM_2);

   return rv;
}

/**
 * Generate a waveform based on the harminics list
 * output range is -1.0 .. 1.0
 */
static float *
_aax_generate_waveform(size_t no_samples, float freq, float phase, float gain, float *harmonics)
{
   float *rv = _aax_aligned_alloc(no_samples*sizeof(float));
   if (rv)
   {
      unsigned int h = MAX_HARMONICS;

      memset(rv, 0, no_samples*sizeof(float));
      do
      {
         float nfreq = freq/h--;
         float ngain = gain * harmonics[h];
         if (ngain)
         {
            int i = no_samples;
            float hdt = GMATH_2PI/nfreq;
            float s = phase;
            float *ptr = rv;

            do
            {
               *ptr++ += ngain * fast_sin(s);
               s = fmodf(s+hdt, GMATH_2PI);
            }
            while (--i);
         }
      }
      while (h);
   }
   return rv;
}


/**
 * Generate an array of random samples
 * output range is -1.0 .. 1.0
 */
static float *
_aax_generate_noise(size_t no_samples, UNUSED(float gain), unsigned char skip)
{
   float *rv = _aax_aligned_alloc(no_samples*sizeof(float));
   if (rv)
   {
      float rnd_skip = skip ? skip : 1.0f;
      int i = no_samples;
      float *ptr = rv;

      _aax_srandom();
      memset(rv, 0, no_samples*sizeof(float));
      do
      {
         *ptr += _aax_rand_sample();

         ptr += (int)rnd_skip;
         i -= (int)rnd_skip;
         if (skip) rnd_skip = 1.0f + (2*skip-rnd_skip)*_aax_random();
      }
      while (i>=skip);
   }
   return rv;
}

#define AVERAGE_SAMPS		8
static void
_aax_resample_float(float32_ptr dptr, const_float32_ptr sptr, size_t dmax, float freq_factor)
{
   size_t i, smax = floorf(dmax * freq_factor);
   const float *s = sptr;
   float *d = dptr;
   float smu = 0.0f;
   float samp, dsamp;

   samp = *(s+smax-1);
   dsamp = *(++s) - samp;
   i = dmax;
   if (i)
   {
      do
      {
         *d++ = samp + (dsamp * smu);
         smu += freq_factor;
         if (smu >= 1.0f)
         {
            smu -= 1.0f;
            samp = *s++; 
            dsamp = *s - samp;
         }
      }
      while (--i);

      for (i=0; i<AVERAGE_SAMPS; i++)
      {
         float fact = 0.5f - (float)i/(4.0f*AVERAGE_SAMPS);
         float dst = dptr[dmax-i-1];
         float src = dptr[i];

         dptr[dmax-i-1] = fact*src + (1.0f - fact)*dst;
         dptr[i] = (1.0f - fact)*src + fact*dst;
      }
   }
}

static void
_aax_add_data(void_ptrptr data, const_float32_ptr mix, int tracks, unsigned int no_samples, char bps, float gain)
{
   int track;
   for(track=0; track<tracks; track++)
   {
      unsigned int i  = no_samples;
      const float *m = mix;
      if (bps == 1)
      {
         static const float max = 127.0f;
         static const float mul = 127.0f / GMATH_PI_2;
         int8_t *d = data[track];
         do {
            *d = mul * atanf(*d/max + *m++ * gain);
             ++d;
         } while (--i);
      }
      else if (bps == 2)
      {
         static const float max = 32765.0f;
         static const float mul = 32765.0f / GMATH_PI_2;
         int16_t *d = data[track];
         do {
            *d = mul * atanf(*d/max + *m++ * gain);
             ++d;
         } while (--i);
      }
      else if (bps == 3 || bps == 4)
      {
         static const float max = 256.0f*32765.0f;
         static const float mul = 256.0f*32765.0f / GMATH_PI_2;
         int32_t *d = data[track];
         do {
            *d = mul * atanf(*d/max + *m++ * gain);
             ++d;
         } while (--i);
      }
   }
}


#define RINGMODULATE(a,b,c,d)	((c)*(((float)(a)/(d))*((b)/(c))))
static void
_aax_mul_data(void_ptrptr data, const_float32_ptr mix, int tracks, unsigned int no_samples, char bps, float gain)
{
   int track;
   for(track=0; track<tracks; track++)
   {
      unsigned int i  = no_samples;
      const float *m = mix;
      if (bps == 1)
      {
         static const float max = 127.0f;
         static const float mul = 127.0f / GMATH_PI_2;
         int8_t *d = data[track];
         do {
            *d = mul * atanf(*d/max * *m++ * gain);
            ++d;
         } while (--i);
      }
      else if (bps == 2)
      {
         static const float max = 32765.0f;
         static const float mul = 32765.0f / GMATH_PI_2;
         int16_t *d = data[track];
         do {
            *d = mul * atanf(*d/max * *m++ * gain);
            ++d;
         } while (--i);
      }
      else if (bps == 3 || bps == 4)
      {  
         static const float max = 255.0f*32765.0f;
         static const float mul = 255.0f*32765.0f / GMATH_PI_2;
         int32_t *d = data[track];
         do {
            *d = mul * atanf(*d/max * *m++ * gain);
            ++d;
         } while (--i);
      }
   }
}

#define NO_FILTER_STEPS         6
void
_aax_pinknoise_filter(float32_ptr data, size_t no_samples, float fs)
{
   float f = (float)logf(fs/100.0f)/(float)NO_FILTER_STEPS;
   _aaxRingBufferFreqFilterData filter;
   unsigned int q = NO_FILTER_STEPS;
   float *dst, *tmp, *ptr = data;
   dst = ptr + no_samples;

   filter.type = LOWPASS;
   filter.no_stages = 1;
   filter.Q = 1.0f;
   filter.fs = fs;

   do
   {
      float fc, v1, v2;

      v1 = powf(1.003f, q);
      v2 = powf(0.90f, q);
      filter.high_gain = v1-v2;
      filter.low_gain = 0.0f;
      filter.freqfilter_history[0][0] = 0.0f;
      filter.freqfilter_history[0][1] = 0.0f;
      filter.k = 1.0f;

      fc = expf((float)(q-1)*f)*100.0f;
      _aax_bessel_compute(fc, &filter);
      _batch_freqfilter_float(dst, ptr, 0, no_samples, &filter);
      _batch_fmadd(dst, ptr, no_samples,  v2, 0.0);

      tmp = dst;
      dst = ptr;
      ptr = tmp;
   }
   while (--q);
}

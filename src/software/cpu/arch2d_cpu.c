/*
 * Copyright 2005-2019 by Erik Hofman.
 * Copyright 2009-2019 by Adalin B.V.
 *
 * This file is part of AeonWave
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  version 3 of the License.
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

#include <math.h>	/* rinft */

#include <base/random.h>
#include <dsp/common.h>
#include <software/rbuf_int.h>
#include "waveforms.h"
#include "arch2d_simd.h"

/**
 * Generate a waveform based on the harminics list
 * output range is -1.0 .. 1.0
 */

float	// range -1.0f .. 2.0f
fast_sin_cpu(float x)
{
   return -4.0f*(x - x*fabsf(x));
}

float *
_aax_generate_waveform_cpu(float32_ptr rv, size_t no_samples, float freq, float phase, enum wave_types wtype)
{
   const_float32_ptr harmonics = _harmonics[wtype];
   if (rv)
   {
      float ngain = harmonics[0];
      unsigned int h, i = no_samples;
      float hdt = 2.0f/freq;
      float s = -1.0f + phase/GMATH_PI;
      float *ptr = rv;

      do
      {
         *ptr++ = ngain * fast_sin_cpu(s);
         s = s+hdt;
         if (s >= 1.0f) s -= 2.0f;
      }
      while (--i);

      for(h=1; h<MAX_HARMONICS; ++h)
      {
         float nfreq = freq/(h+1);
         if (nfreq < 2.0f) break;       // higher than the nyquist-frequency

         ngain = harmonics[h];
         if (ngain)
         {
            int i = no_samples;
            float hdt = 2.0f/nfreq;
            float s = -1.0f + phase/GMATH_PI;
            float *ptr = rv;

            do
            {
               *ptr++ += ngain * fast_sin_cpu(s);
               s = s+hdt;
               if (s >= 1.0f) s -= 2.0f;
            }
            while (--i);
         }
      }
   }
   return rv;
}


void
_batch_imadd_cpu(int32_ptr dptr, const_int32_ptr sptr, size_t num, float v, float vstep)
{
   int32_t* s = (int32_t* )sptr;
   int32_t* d = dptr;
   size_t i = num;

   if (!num || (v <= LEVEL_128DB && vstep <= LEVEL_128DB)) return;

   /* f == 1.0f && step = 0.0f */
   if (fabsf(v - 1.0f) < LEVEL_96DB && vstep <=  LEVEL_96DB)
   {
      do {
         *d++ += *s++;
      }
      while (--i);
   }
   else
   {
      int32_t f = (int32_t)(v*1024.0f);
      do {
         *d++ += ((*s++ >> 2) * f) >> 8;
      }
      while (--i);
   }
}

#if 0
void
_batch_hmadd_cpu(float32_ptr dptr, const_float16_ptr sptr, size_t num, float v, float vstep)
{
   int16_t *s = (int16_t*)sptr;
   float *d = dptr;
   size_t i = num;

   if (!num || (v <= LEVEL_128DB && vstep <= LEVEL_128DB)) return;

   /* v == 1.0f && step = 0.0f */
   if (fabsf(v - 1.0f) < LEVEL_96DB && vstep <=  LEVEL_96DB)
   {
      do {
         *d++ += HALF2FLOAT(*s);
         s++;
      }
      while (--i);
   }
   else
   {
      do {
         *d++ += HALF2FLOAT(*s) * v;
         v += vstep;
         s++;
      }
      while (--i);
   }
}
#endif

void
_batch_fmadd_cpu(float32_ptr dptr, const_float32_ptr sptr, size_t num, float v, float vstep)
{
   int need_step = (fabsf(vstep) <= LEVEL_90DB) ? 0 : 1;
   float *s = (float*)sptr;
   float *d = dptr;
   size_t i = num;

   if (!num || (fabsf(v) <= LEVEL_90DB && !need_step)) return;

   if (fabsf(v - 1.0f) < LEVEL_90DB && !need_step)
   {
      do {
         *d++ += *s++;
      }
      while (--i);
   }
   else if (need_step)
   {
      do {
         *d++ += (*s++ * v);
         v += vstep;
      }
      while (--i);
   }
   else
   {
      do {
         *d++ += (*s++ * v);
      }
      while (--i);
   }
}

void
_batch_imul_value_cpu(void* dptr, const void* sptr, unsigned bps, size_t num, float f)
{
   size_t i = num;
   if (num)
   {
      switch (bps)
      {
      case 1:
      {
         int8_t* s = (int8_t*)sptr;
         int8_t* d = (int8_t*)dptr;
         do {
            *d++ = *s++ * f;
         }
         while (--i);
         break;
      }
      case 2:
      {
         int16_t* s = (int16_t*)sptr;
         int16_t* d = (int16_t*)dptr;
         do {
            *d++ = *s++ * f;
         }
         while (--i);
         break;
      }
      case 4:
      {
         int32_t* s = (int32_t*)sptr;
         int32_t* d = (int32_t*)dptr;
         do {
            *d++ = *s++ * f;
         }
         while (--i);
         break;
      }
      default:
         break;
      }
   }
}

void
_batch_fmul_value_cpu(void* dptr, const void* sptr, unsigned bps, size_t num, float f)
{
   if (!num) return;

   if (fabsf(f - 1.0f) < LEVEL_96DB) {
      if (sptr != dptr) memcpy(dptr, sptr,  num*bps);
   } else if (f <= LEVEL_96DB) {
      memset(dptr, 0, num*bps);
   }
   else if (num)
   {
      size_t i = num;

      switch (bps)
      {
      case 4:
      {
         float *s = (float*)sptr;
         float *d = (float*)dptr;
         do {
            *d++ = *s++ * f;
         }
         while (--i);
         break;
      }
      case 8:
      {
         double *s = (double*)sptr;
         double *d = (double*)dptr;
         do {
            *d++ = *s++ * f;
         }
         while (--i);
         break;
      }
      default:
         break;
      }
   }
}

void
_batch_cvt24_24_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num && dptr != sptr) {
      memcpy(dptr, sptr, num*sizeof(int32_t));
   }
}

void
_batch_cvt24_32_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      int32_t *s = (int32_t *)sptr;
      int32_t *d = dptr;
      size_t i = num;

      do {
         *d++ = *s++ >> 8;
      }
      while (--i);
   }
}

void
_batch_cvt32_24_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      int32_t *d = (int32_t *)dptr;
      int32_t *s = (int32_t *)sptr;
      size_t i = num;

      do {
         *d++ = *s++ << 8;
      }
      while (--i);
   }
}

void
_batch_cvt24_ps24_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      int32_t* d = (int32_t*)dptr;
      float* s = (float*)sptr;
      size_t i = num;

      do {
         *d++ = (int32_t)*s++;
      } while (--i);
   }
}

void
_batch_cvtps24_24_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      float* d = (float*)dptr;
      int32_t* s = (int32_t*)sptr;
      size_t i = num;

      do {
         *d++ = (float)*s++;
      } while (--i);
   }
}

void _batch_roundps_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   _batch_cvt24_ps24(dptr, sptr, num);
   _batch_cvtps24_24(dptr, dptr, num);
}

#if 0
void
_batch_cvt24_ph_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      static const float mul = (float)(1<<23);
      int32_t* d = (int32_t*)dptr;
      int16_t* s = (int16_t*)sptr;
      size_t i = num;

      do {
         *d++ = (int32_t)(HALF2FLOAT(*s) * mul);
         s++;
      } while (--i);
   }
}
#endif

void
_batch_cvt24_ps_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      static const float mul = (float)(1<<23);
      int32_t* d = (int32_t*)dptr;
      float* s = (float*)sptr;
      size_t i = num;

      do {
         *d++ = (int32_t)(*s++ * mul);
      } while (--i);
   }
}

#if 0
void
_batch_cvtph_24_cpu(void_ptr dst, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      static const float mul = 1.0f/(float)(1<<23);
      int32_t* s = (int32_t*)sptr;
      int16_t* d = (int16_t*)dst;
      size_t i = num;

      do {
         *d++ = FLOAT2HALF((float)*s++ * mul);
      } while (--i);
   }
}
#endif

void
_batch_cvtps_24_cpu(void_ptr dst, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      static const float mul = 1.0f/(float)(1<<23);
      int32_t* s = (int32_t*)sptr;
      float* d = (float*)dst;
      size_t i = num;

      do {
         *d++ = (float)*s++ * mul;
      } while (--i);
   }
}

void
_batch_cvt24_pd_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      static const double mul = (double)(1<<23);
      int32_t* d = (int32_t*)dptr;
      double* s = (double*)sptr;
      size_t i = num;

      do {
         *d++ = (int32_t)(*s++ * mul);
      } while (--i);
   }
}

void
_batch_cvt24_8_intl_cpu(int32_ptrptr dptr, const_void_ptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      if (tracks == 1) {
         _batch_cvt24_8(dptr[0]+offset, sptr, num);
      }
      else if (tracks)
      {
         size_t t;
         for (t=0; t<tracks; t++)
         {
            int8_t *s = (int8_t *)sptr + t;
            int32_t *d = dptr[t] + offset;
            size_t i = num;

            do
            {
               *d++ = ((int32_t)*s + 128) << 16;
               s += tracks;
            }
            while (--i);
         }
      }
   }
}

void
_batch_cvt24_16_intl_cpu(int32_ptrptr dptr, const_void_ptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      if (tracks == 1) {
         _batch_cvt24_16(dptr[0]+offset, sptr, num);
      }
      else if (tracks)
      {
         size_t t;
         for (t=0; t<tracks; t++)
         {
            int16_t *s = (int16_t *)sptr + t;
            int32_t *d = dptr[t] + offset;
            size_t i = num;

            do
            {
               *d++ = (int32_t)*s << 8;
               s += tracks;
            }
            while (--i);
         }
      }
   }
}

void
_batch_cvt24_24_intl_cpu(int32_ptrptr dptr, const_void_ptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      if (tracks == 1) {
         _batch_cvt24_24(dptr[0]+offset, sptr, num);
      }
      else if (tracks)
      {
         size_t t;
         for (t=0; t<tracks; t++)
         {
            int32_t *s = (int32_t *)sptr + t;
            int32_t *d = dptr[t] + offset;
            size_t i = num;

            do {
               *d++ = *s;
               s += tracks;
            }
            while (--i);
         }
      }
   }
}

void
_batch_cvt24_24_3intl_cpu(int32_ptrptr dptr, const_void_ptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      if (tracks == 1) {
         _batch_cvt24_24_3(dptr[0]+offset, sptr, num);
      }
      else if (tracks)
      {
         size_t t;
         for (t=0; t<tracks; t++)
         {
            uint8_t *s = (uint8_t*)sptr + 3*t;
            int32_t smp, *d = dptr[t] + offset;
            size_t i = num;

            do {
               smp = *s++;
               smp |= (*s++ << 8);
               smp |= (*s++ << 16);
               if ((smp & 0x00800000) > 0) smp |= 0xFF000000;

               *d++ = smp;
               s += 3*(tracks-1);
            }
            while (--i);
         }
      }
   }
}

void
_batch_cvt24_32_intl_cpu(int32_ptrptr dptr, const_void_ptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      if (tracks == 1) {
         _batch_cvt24_32(dptr[0]+offset, sptr, num);
      }
      else if (tracks)
      {
         size_t t;
         for (t=0; t<tracks; t++)
         {
            int32_t *s = (int32_t *)sptr + t;
            int32_t *d = dptr[t] + offset;
            size_t i = num;

            do {
               *d++ = *s >> 8;
               s += tracks;
            }
            while (--i);
         }
      }
   }
}

#if 0
void
_batch_cvt24_ph_intl_cpu(int32_ptrptr dptr, const_void_ptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      if ((tracks == 1) &&
          ((size_t)(dptr[0]+offset) & MEMMASK) == 0 &&
          ((size_t)sptr & MEMMASK) == 0)
      {
         _batch_cvt24_ph(dptr[0]+offset, sptr, num);
      }
      else if (tracks)
      {
         static const float mul = (float)(1<<23);
         size_t t;

         for (t=0; t<tracks; t++)
         {
            int16_t *s = (int16_t*)sptr + t;
            int32_t *d = dptr[t] + offset;
            size_t i = num;

            do {
               *d++ = (int32_t)(HALF2FLOAT(*s) * mul);
               s += tracks;
            }
            while (--i);
         }
      }
   }
}
#endif

void
_batch_cvt24_ps_intl_cpu(int32_ptrptr dptr, const_void_ptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      if ((tracks == 1) &&
          ((size_t)(dptr[0]+offset) & MEMMASK) == 0 &&
          ((size_t)sptr & MEMMASK) == 0)
      {
         _batch_cvt24_ps(dptr[0]+offset, sptr, num);
      }
      else if (tracks)
      {
         static const float mul = (float)(1<<23);
         size_t t;

         for (t=0; t<tracks; t++)
         {
            float *s = (float*)sptr + t;
            int32_t *d = dptr[t] + offset;
            size_t i = num;

            do {
               *d++ = (int32_t)(*s * mul);
               s += tracks;
            }
            while (--i);
         }
      }
   }
}

void
_batch_cvt24_pd_intl_cpu(int32_ptrptr dptr, const_void_ptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      if (tracks == 1) {
         _batch_cvt24_pd(dptr[0]+offset, sptr, num);
      }
      else if (tracks)
      {
         static const double mul = (double)(1<<23);
         size_t t;
         for (t=0; t<tracks; t++)
         {
            double *s = (double*)sptr + t;
            int32_t *d = dptr[t] + offset;
            size_t i = num;

            do {
               *d++ = (int32_t)(*s * mul);
               s += tracks;
            }
            while (--i);
         }
      }
   }
}

void
_batch_cvtpd_24_cpu(void_ptr dst, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      static const double mul = 1.0/(double)(1<<23);
      int32_t* s = (int32_t*)sptr;
      double* d = (double*)dst;
      size_t i = num;
      do {
         *d++ = (double)*s++ * mul;
      } while (--i);
   }
}

void
_batch_cvt24_8_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      int8_t* s = (int8_t*)sptr;
      int32_t* d = dptr;
      size_t i = num;

      do {
         *d++ = (int32_t)*s++ << 16;
      }
      while (--i);
   }
}

void
_batch_cvt24_16_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      int16_t* s = (int16_t*)sptr;
      int32_t* d = dptr;
      size_t i = num;

      do {
         *d++ = (int32_t)*s++ << 8;
      }
      while (--i);
   }
}

void
_batch_cvt24_24_3_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      uint8_t *s = (uint8_t *)sptr;
      int32_t smp, *d = dptr;
      size_t i = num;

      do
      {
         smp = ((int32_t)*s++);
         smp |= ((int32_t)*s++) << 8;
         smp |= ((int32_t)*s++) << 16;
         if ((smp & 0x00800000) > 0) smp |= 0xFF000000;

         *d++ = smp;
      }
      while (--i);
   }
}


void
_batch_cvt8_24_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      int32_t* s = (int32_t*)sptr;
      int8_t* d = (int8_t*)dptr;
      size_t i = num;

      _batch_dither_cpu(s, 1, num);
      do {
         *d++ = *s++ >> 16;
      }
      while (--i);
   }
}

void
_batch_cvt16_24_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      int32_t* s = (int32_t*)sptr;
      int16_t* d = (int16_t*)dptr;
      size_t i = num;

      _batch_dither_cpu(s, 2, num);
      do {
         *d++ = *s++ >> 8;
      }
      while (--i);
   }
}

void
_batch_cvt24_3_24_cpu(void_ptr dptr, const_void_ptr sptr, size_t num)
{
   if (num)
   {
      uint8_t *d = (uint8_t *)dptr;
      int32_t *s = (int32_t*)sptr;
      size_t i = num;

      do
      {
         *d++ = (uint8_t)(*s & 0xFF);
         *d++ = (uint8_t)((*s >> 8) & 0xFF);
         *d++ = (uint8_t)((*s++ >> 16) & 0xFF);
      }
      while (--i);
   }
}

void
_batch_cvt8_intl_24_cpu(void_ptr dptr, const_int32_ptrptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      size_t t;

      for (t=0; t<tracks; t++)
      {
         int32_t *s = (int32_t *)sptr[t] + offset;
         int8_t *d = (int8_t *)dptr + t;
         size_t i = num;

         _batch_dither_cpu(s, 1, num);
         do
         {
            *d = (*s++ >> 16) - 128;
            d += tracks;
         }
         while (--i);
      }
   }
}

void
_batch_cvt16_intl_24_cpu(void_ptr dptr, const_int32_ptrptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      size_t t;
      for (t=0; t<tracks; t++)
      {
         int32_t *s = (int32_t *)sptr[t] + offset;
         int16_t *d = (int16_t *)dptr + t;
         size_t i = num;

         _batch_dither_cpu(s, 2, num);
         do
         {
            *d = *s++ >> 8;
            d += tracks;
         }
         while (--i);
      }
   }
}

void
_batch_cvt24_3intl_24_cpu(void_ptr dptr, const_int32_ptrptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      size_t t;

      for (t=0; t<tracks; t++)
      {
         int32_t *s = (int32_t *)sptr[t] + offset;
         int8_t *d = (int8_t *)dptr + 3*t;
         size_t i = num;

         do
         {
            *d++ = *s & 0xFF;
            *d++ = (*s >> 8) & 0xFF;
            *d++ = (*s++ >> 16) & 0xFF;
         }
         while (--i);
      }
   }
}

void
_batch_cvt24_intl_24_cpu(void_ptr dptr, const_int32_ptrptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      size_t t;

      for (t=0; t<tracks; t++)
      {
         int32_t *s = (int32_t *)sptr[t] + offset;
         int32_t *d = (int32_t *)dptr + t;
         size_t i = num;

         do
         {
            *d = *s++;
            d += tracks;
         }
         while (--i);
      }
   }
}

void
_batch_cvt24_intl_ps_cpu(void_ptr dptr, const_int32_ptrptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      static const float mul = (float)(1<<23);
      size_t t;

      for (t=0; t<tracks; t++)
      {
         float *s = (float *)sptr[t] + offset;
         int32_t *d = (int32_t *)dptr + t;
         size_t i = num;

         do
         {
            *d = (int32_t)(*s++ * mul);
            d += tracks;
         }
         while (--i);
      }
   }
}


void
_batch_cvt32_intl_24_cpu(void_ptr dptr, const_int32_ptrptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      size_t t;

      for (t=0; t<tracks; t++)
      {
         int32_t *s = (int32_t *)sptr[t] + offset;
         int32_t *d = (int32_t *)dptr + t;
         size_t i = num;

         do
         {
            *d = *s++ << 8;
            d += tracks;
         }
         while (--i);
      }
   }
}

void
_batch_cvtps_intl_24_cpu(void_ptr dptr, const_int32_ptrptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      static const float mul = 1.0/(float)(1<<23);
      size_t t;

      for (t=0; t<tracks; t++)
      {
         int32_t *s = (int32_t*)sptr[t] + offset;
         float *d = (float*)dptr + t;
         size_t i = num;

         do
         {
            *d = (float)*s++ * mul;
            d += tracks;
         }
         while (--i);
      }
   }
}

void
_batch_cvtpd_intl_24_cpu(void_ptr dptr, const_int32_ptrptr sptr, size_t offset, unsigned int tracks, size_t num)
{
   if (num)
   {
      static const double mul = 1.0/(double)(1<<23);
      size_t t;

      for (t=0; t<tracks; t++)
      {
         int32_t *s = (int32_t *)sptr[t] + offset;
         double *d = (double*)dptr + t;
         size_t i = num;

         do
         {
            *d = (double)*s++ * mul;
            d += tracks;
         }
         while (--i);
      }
   }
}

void
_batch_get_average_rms_cpu(const_float32_ptr data, size_t num, float *rms, float *peak)
{
   if (num)
   {
      double rms_total = 0.0;
      float peak_cur = 0.0f;

      size_t j = num;
      do
      {
         float samp = *data++;            // rms
         float val = samp*samp;
         rms_total += val;
         if (val > peak_cur) peak_cur = val;
      }
      while (--j);

      *rms = sqrt(rms_total/num);
      *peak = sqrtf(peak_cur);
   }
   else {
      *rms = *peak = 0;
   }
}

void
_batch_saturate24_cpu(void *data, size_t num)
{
   if (num)
   {
      int32_t* p = (int32_t*)data;
      size_t i = num;
      do
      {
         int32_t samp = _MINMAX(*p, -8388607, 8388607);
         *p++ = samp;
      }
      while(--i);
   }
}

void
_batch_cvt8u_8s_cpu(void *data, size_t num)
{
   if (num)
   {
      int8_t* p = (int8_t*)data;
      size_t i = num;

      do {
         *p++ -= 128;
      } while (--i);
   }
}

void
_batch_cvt8s_8u_cpu(void *data, size_t num)
{
   if (num)
   {
      uint8_t* p = (uint8_t*)data;
      size_t i = num;

      do {
         *p++ += 128;
      } while (--i);
   }
}

void
_batch_cvt16u_16s_cpu(void *data, size_t num)
{
   if (num)
   {
      int16_t* p = (int16_t*)data;
      size_t i = num;

      do {
         *p++ -= (int16_t)32768;
      } while (--i);
   }
}

void
_batch_cvt16s_16u_cpu(void *data, size_t num)
{
   if (num)
   {
      uint16_t* p = (uint16_t*)data;
      size_t i = num;

      do {
         *p++ += (uint16_t)32768;
      } while (--i);
   }
}

void
_batch_cvt24u_24s_cpu(void *data, size_t num)
{
   if (num)
   {
      int32_t* p = (int32_t*)data;
      size_t i = num;

      do {
         *p++ -= (int32_t)8388608;
      } while (--i);
   }
}

void
_batch_cvt24s_24u_cpu(void *data, size_t num)
{
   if (num)
   {
      uint32_t* p = (uint32_t*)data;
      size_t i = num;

      do {
         *p++ += (uint32_t)8388608;
      } while (--i);
   }
}

void
_batch_cvt32u_32s_cpu(void *data, size_t num)
{
   if (num)
   {
      int32_t* p = (int32_t*)data;
      size_t i = num;

      do {
         *p++ -= (int32_t)2147483647;
      } while (--i);
   }
}

void
_batch_cvt32s_32u_cpu(void *data, size_t num)
{
   if (num)
   {
      uint32_t* p = (uint32_t*)data;
      size_t i = num;

      do {
         *p++ += (uint32_t)2147483647;
      } while (--i);
   }
}

// You could create a triangular probability density function by getting a
// random number between -1 and 0 and another between 0 and 1 and summing them.
// When noise shaping is added to dithering, there is less noise at low
// frequency and more noise at high frequency.
// Note: apply this after converting to a lower number of bits.
static inline int sign15(int32_t x) { return (!x)?0:((x<0)?-32768:32768); }
static inline int sign7(int32_t x) { return (!x)?0:((x<0)?-128:128); }
void
_batch_dither_cpu(void *data, unsigned new_bps, size_t num)
{
   size_t i = num;
   if (num)
   {
      switch (new_bps)
      {
      case 1:
      {
         uint32_t s1 = xoroshiro128plus() >> 63;
         int32_t* d = (int32_t*)data;
         do
         {
            uint32_t s2 = xoroshiro128plus() >> 63;
            int32_t tpdf = (s1 - s2);
            *d++ += sign15(tpdf);
            s1 = s2;
         }
         while (--i);
         break;
      }
      case 2:
      {
         uint32_t s1 = xoroshiro128plus() >> 63;
         int32_t* d = (int32_t*)data;
         do
         {
            uint32_t s2 = xoroshiro128plus() >> 63;
            int32_t tpdf = (s1 - s2);
            *d++ += sign7(tpdf);
            s1 = s2;
         }
         while (--i);
         break;
      }
      default:
         break;
      }
   }
}

void
_batch_endianswap16_cpu(void* data, size_t num)
{
   if (num)
   {
      int16_t* p = (int16_t*)data;
      size_t i = num;

      do
      {
         *p = _aax_bswap16(*p);
         p++;
      }
      while (--i);
   }
}

void
_batch_endianswap24_cpu(void* data, size_t num)
{
   if (num)
   {
      int32_t* p = (int32_t*)data;
      size_t i = num*3;

      do
      {
         char s = *p;
         *p = *(p+2);
         p += 2;
         *p++ = s;
      }
      while (--i);
   }
}

void
_batch_endianswap32_cpu(void* data, size_t num)
{
   if (num)
   {
      int32_t* p = (int32_t*)data;
      size_t i = num;

      do
      {
         *p = _aax_bswap32(*p);
         p++;
      }
      while (--i);
   }
}

void
_batch_endianswap64_cpu(void* data, size_t num)
{
   if (num)
   {
      int64_t* p = (int64_t*)data;
      size_t i = num;

      do
      {
         *p = _aax_bswap64(*p);
         p++;
      }
      while (--i);
   }
}

void
_batch_convolution_cpu(float32_ptr hcptr, const_float32_ptr cptr, const_float32_ptr sptr, unsigned int cnum, unsigned int dnum, int step, float v, float threshold)
{
   unsigned int q = cnum/step;
   do
   {
      float volume = *cptr * v;
      if (fabsf(volume) > threshold) {
         _batch_fmadd(hcptr, sptr, dnum, volume, 0.0f);
      }
      cptr += step;
      hcptr += step;
   }
   while (--q);
}

/**
 * A mixer callback function mixes the audio from one mono source track and
 * the already existing audio in the mono destination track. The result is
 * presented in the destination buffer.
 *
 * @d 32-bit destination buffer
 * @s 32-bit source buffer
 * @dmin starting sample position in the dest buffer
 * @dmax last sample position in the dest buffer
 * @smin sample position where to return to in case of sample looping
 * @smu normalised starting position
 * @freq_factor stepsize in the src buffer for one step in the dest buffer.
 * @smax last sample position in the src buffer
 *
 * Note: smax is only used in the *Loop mixing functions
 */
static inline void
_aaxBufResampleDecimate_cpu(int32_ptr dptr, const_int32_ptr sptr, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   int32_ptr s = (int32_ptr)sptr;
   int32_ptr d = dptr;
   int32_t samp, dsamp;
   size_t i;

   assert(s != 0);
   assert(d != 0);
   assert(dmin < dmax);
   assert(freq_factor >= 1.0f);
   assert(0.0f <= smu && smu < 1.0f);

   d += dmin;

   samp = *s++;			// n+(step-1)
   dsamp = *s - samp;		// (n+1) - n

   i=dmax-dmin;
   if (i)
   {
      if (freq_factor == 2.0f)
      {
         do {
            *d++ = (*s + *(s+1))*0.5f;
            s += 2;
         }
         while (--i);
      }
      else
      {
         do
         {
            size_t step;

            *d++ = samp + (int32_t)(dsamp * smu);

            smu += freq_factor;
            step = (size_t)floorf(smu);

            smu -= step;
            s += step-1;
            samp = *s++;
            dsamp = *s - samp;
         }
         while (--i);
      }
   }
}

#if 0
static inline void
_aaxBufResampleNearest_cpu(int32_ptr dptr, const_int32_ptr sptr, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   if (freq_factor == 1.0f) {
      memcpy(dptr+dmin, sptr, (dmax-dmin)*sizeof(int32_t));
   }
   else
   {
      int32_ptr s = (int32_ptr)sptr;
      int32_ptr d = dptr;
      size_t i;

      assert(s != 0);
      assert(d != 0);
      assert(dmin < dmax);
      assert(0.95f <= freq_factor && freq_factor <= 1.05f);
      assert(0.0f <= smu && smu < 1.0f);

      d += dmin;

      i = dmax-dmin;
      if (i)
      {
         do
         {
            *d++ = *s;

            smu += freq_factor;
            if (smu > 0.5f)
            {
               s++;
               smu -= 1.0f;
            }
         }
         while (--i);
      }
   }
}
#endif

static inline void
_aaxBufResampleLinear_cpu(int32_ptr dptr, const_int32_ptr sptr, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   int32_ptr s = (int32_ptr)sptr;
   int32_ptr d = dptr;
   int32_t samp, dsamp;
   size_t i;

   assert(s != 0);
   assert(d != 0);
   assert(dmin < dmax);
   assert(freq_factor < 1.0f);
   assert(0.0f <= smu && smu < 1.0f);

   d += dmin;

   samp = *s++;		// n
   dsamp = *s - samp;	// (n+1) - n

   i = dmax-dmin;
   if (i)
   {
      do
      {
         *d++ = samp + (int32_t)(dsamp * smu);

         smu += freq_factor;
         if (smu >= 1.0f)
         {
            smu -= 1.0f;
            samp = *s++;
            dsamp = *s - samp;
         }
      }
      while (--i);
   }

#if 0
 printf("dptr: %x, d+dmax: %x, dptr-d: %i (%x)\n", d, dptr+dmax, d-dptr, samp);
 for (i=0; i<dmax; i++)
    if (d[i] != 0x333300) printf("->d[%i] = %x\n", i, d[i]);
#endif
}

static inline void
_aaxBufResampleCubic_cpu(int32_ptr dptr, const_int32_ptr sptr, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   float y0, y1, y2, y3, a0, a1, a2;
   int32_ptr s = (int32_ptr)sptr;
   int32_ptr d = dptr;
   size_t i;

   assert(s != 0);
   assert(d != 0);
   assert(dmin < dmax);
   assert(0.0f <= smu && smu < 1.0f);
   assert(0.0f < freq_factor && freq_factor <= 1.0f);

   d += dmin;

   y0 = (float)*s++;
   y1 = (float)*s++;
   y2 = (float)*s++;
   y3 = (float)*s++;

   a0 = y3 - y2 - y0 + y1;
   a1 = y0 - y1 - a0;
   a2 = y2 - y0;

   i = dmax-dmin;
   if (i)
   {
      do
      {
         float smu2, ftmp;

         smu2 = smu*smu;
         ftmp = (a0*smu*smu2 + a1*smu2 + a2*smu + y1);
         *d++ = (int32_t)ftmp;

         smu += freq_factor;
         if (smu >= 1.0f)
         {
            smu--;
#if 0
            /* original code */
            s -= 3;
            y0 = *s++;
            y1 = *s++;
            y2 = *s++;
            y3 = *s++;

            a0 = y3 - y2 - y0 + y1;
            a1 = y0 - y1 - a0;
            a2 = y2 - y0;
#else
            /* optimized code */
            a0 += y0;
            y0 = y1;
            y1 = y2;
            y2 = y3;
            y3 = (float)*s++;
            a0 = -a0 + y3;			/* a0 = y3 - y2 - y0 + y1; */
            a1 = y0 - y1 - a0;
            a2 = y2 - y0;
#endif
         }
      }
      while (--i);
   }
}

void
_batch_resample_cpu(int32_ptr d, const_int32_ptr s, size_t dmin, size_t dmax, float smu, float fact)
{
   assert(fact > 0.0f);
   assert(d != s);

   if (fact < CUBIC_TRESHOLD) {
      _aaxBufResampleCubic_cpu(d, s, dmin, dmax, smu, fact);
   }
   else if (fact < 1.0f) {
      _aaxBufResampleLinear_cpu(d, s, dmin, dmax, smu, fact);
   }
   else if (fact >= 1.0f) {
      _aaxBufResampleDecimate_cpu(d, s, dmin, dmax, smu, fact);
   } else {
//    _aaxBufResampleNearest_cpu(d, s, dmin, dmax, smu, fact);
      memcpy(d+dmin, s, (dmax-dmin)*sizeof(int32_t));
   }
}


static inline void
_aaxBufResampleDecimate_float_cpu(float32_ptr dptr, const_float32_ptr sptr, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   float32_ptr s = (float32_ptr)sptr;
   float32_ptr d = dptr;
   float samp, dsamp;
   size_t i;

   assert(s != 0);
   assert(d != 0);
   assert(dmin < dmax);
   assert(freq_factor >= 1.0f);
   assert(freq_factor < 10.0f);
   assert(0.0f <= smu && smu < 1.0f);

   d += dmin;

   samp = *s++;                 // n+(step-1)
   dsamp = *s - samp;           // (n+1) - n

   i = dmax-dmin;
   if (i)
   {
      if (freq_factor == 2.0f)
      {
         do {
            *d++ = (*s + *(s+1))*0.5f;
            s += 2;
         }
         while (--i);
      }
      else
      {
         do
         {
            size_t step;

            *d++ = samp + (dsamp * smu);

            smu += freq_factor;
            step = (size_t)floorf(smu);

            smu -= step;
            s += step-1;
            samp = *s++;
            dsamp = *s - samp;
         }
         while (--i);
      }
   }
}

#if 0
static inline void
_aaxBufResampleNearest_float_cpu(float32_ptr dptr, const_float32_ptr sptr, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   if (freq_factor == 1.0f) {
      memcpy(dptr+dmin, sptr, (dmax-dmin)*sizeof(float));
   }
   else
   {
      float32_ptr s = (float32_ptr)sptr;
      float32_ptr d = dptr;
      size_t i;

      assert(s != 0);
      assert(d != 0);
      assert(dmin < dmax);
      assert(0.95f <= freq_factor && freq_factor <= 1.05f);
      assert(0.0f <= smu && smu < 1.0f);

      d += dmin;

      i = dmax-dmin;
      if (i)
      {
         do
         {
            *d++ = *s;

            smu += freq_factor;
            if (smu > 0.5f)
            {
               s++;
               smu -= 1.0f;
            }
         }
         while (--i);
      }
   }
}
#endif

static inline void
_aaxBufResampleLinear_float_cpu(float32_ptr dptr, const_float32_ptr sptr, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   float32_ptr s = (float32_ptr)sptr;
   float32_ptr d = dptr;
   float samp, dsamp;
   size_t i;

   assert(s != 0);
   assert(d != 0);
   assert(dmin < dmax);
   assert(freq_factor < 1.0f);
   assert(0.0f <= smu && smu < 1.0f);

   d += dmin;

   samp = *s++;         // n
   dsamp = *s - samp;   // (n+1) - n

   i = dmax-dmin;
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
   }

#if 0
 printf("dptr: %x, d+dmax: %x, dptr-d: %i (%f)\n", d, dptr+dmax, d-dptr, samp);
#endif
}

static inline void
_aaxBufResampleCubic_float_cpu(float32_ptr dptr, const_float32_ptr sptr, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   float y0, y1, y2, y3, a0, a1, a2;
   float32_ptr s = (float32_ptr)sptr;
   float32_ptr d = dptr;
   size_t i;

   assert(s != 0);
   assert(d != 0);
   assert(dmin < dmax);
   assert(0.0f <= smu && smu < 1.0f);
   assert(0.0f < freq_factor && freq_factor <= 1.0f);

   d += dmin;

   y0 = *s++;
   y1 = *s++;
   y2 = *s++;
   y3 = *s++;

   a0 = y3 - y2 - y0 + y1;
   a1 = y0 - y1 - a0;
   a2 = y2 - y0;

   i = dmax-dmin;
   if (i)
   {
      do
      {
         float smu2, ftmp;

         smu2 = smu*smu;
         ftmp = (a0*smu*smu2 + a1*smu2 + a2*smu + y1);
         *d++ = ftmp;

         smu += freq_factor;
         if (smu >= 1.0f)
         {
            smu--;
#if 0
            // http://paulbourke.net/miscellaneous/interpolation/
            s -= 3;
            y0 = *s++;
            y1 = *s++;
            y2 = *s++;
            y3 = *s++;

            a0 = y3 - y2 - y0 + y1;
            a1 = y0 - y1 - a0;
            a2 = y2 - y0;
#else
            /* optimized code */
            a0 += y0;
            y0 = y1;
            y1 = y2;
            y2 = y3;
            y3 = *s++;
            a0 = -a0 + y3;                      /* a0 = y3 - y2 - y0 + y1; */
            a1 = y0 - y1 - a0;
            a2 = y2 - y0;
#endif
         }
      }
      while (--i);
   }
}

void
_batch_resample_float_cpu(float32_ptr d, const_float32_ptr s, size_t dmin, size_t dmax, float smu, float fact)
{
   assert(fact > 0.0f);
   assert(d != s);

   if (fact < CUBIC_TRESHOLD) {
      _aaxBufResampleCubic_float_cpu(d, s, dmin, dmax, smu, fact);
   }
   else if (fact < 1.0f) {
      _aaxBufResampleLinear_float_cpu(d, s, dmin, dmax, smu, fact);
   }
   else if (fact >= 1.0f) {
      _aaxBufResampleDecimate_float_cpu(d, s, dmin, dmax, smu, fact);
   } else {
//    _aaxBufResampleNearest_float_cpu(d, s, dmin, dmax, smu, fact);
      memcpy(d+dmin, s, (dmax-dmin)*sizeof(float));
   }
}

/*
 * Copyright 2005-2017 by Erik Hofman.
 * Copyright 2009-2017 by Adalin B.V.
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Adalin B.V.;
 * the contents of this file may not be disclosed to third parties, copied or
 * duplicated in any form, in whole or in part, without the prior written
 * permission of Adalin B.V.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>	/* for floorf */


#include "software/rbuf_int.h"
#include "arch2d_simd.h"

#ifdef __AVX__

# define CACHE_ADVANCE_FMADD	 32
# define CACHE_ADVANCE_CPY	 32
# define CACHE_ADVANCE_CVT	 64
# define CACHE_ADVANCE_IMADD     32
# define CACHE_ADVANCE_INTL      32


void
_batch_cvt24_ps_avx(void_ptr dst, const_void_ptr src, size_t num)
{
   int32_t *d = (int32_t*)dst;
   float *s = (float*)src;

   if (((size_t)d & MEMMASK) != 0 || ((size_t)s & MEMMASK) != 0)
   {
      if (((size_t)d & MEMMASK16) == 0 || ((size_t)s & MEMMASK16) == 0)
      {
         _mm256_zeroall();
         return _batch_cvt24_ps_sse2(dst, src, num);
      }
      else
      {
         float mul = (float)(1<<23);
         size_t i = num;
         do {
            *d++ = (int32_t)(*s++ * mul);
         } while (--i);
         return;
      }
   }

   assert(((size_t)d & MEMMASK) == 0);
   assert(((size_t)s & MEMMASK) == 0);

   if (num)
   {
      __m256i *dptr = (__m256i*)d;
      __m256* sptr = (__m256*)s;
      size_t i, step;

      step = 4*sizeof(__m256)/sizeof(float);

      i = num/step;
      num -= i*step;
      if (i)
      {
         __m256i ymm4i, ymm5i, ymm6i, ymm7i;
         __m256 ymm0, ymm1, ymm2, ymm3;
         __m256 mul = _mm256_set1_ps((float)(1<<23));
         do
         {
            _mm_prefetch(((char *)s)+CACHE_ADVANCE_CVT, _MM_HINT_NTA);

            ymm0 = _mm256_load_ps((const float*)sptr++);
            ymm1 = _mm256_load_ps((const float*)sptr++);
            ymm2 = _mm256_load_ps((const float*)sptr++);
            ymm3 = _mm256_load_ps((const float*)sptr++);

            ymm0 = _mm256_mul_ps(ymm0, mul);
            ymm1 = _mm256_mul_ps(ymm1, mul);
            ymm2 = _mm256_mul_ps(ymm2, mul);
            ymm3 = _mm256_mul_ps(ymm3, mul);

            ymm4i = _mm256_cvtps_epi32(ymm0);
            ymm5i = _mm256_cvtps_epi32(ymm1);
            ymm6i = _mm256_cvtps_epi32(ymm2);
            ymm7i = _mm256_cvtps_epi32(ymm3);

            d += step;
            s += step;

            _mm256_store_si256(dptr++, ymm4i);
            _mm256_store_si256(dptr++, ymm5i);
            _mm256_store_si256(dptr++, ymm6i);
            _mm256_store_si256(dptr++, ymm7i);
         }
         while(--i);
      }

      if (num)
      {
         float mul = (float)(1<<23);
         i = num;
         do {
            *d++ = (int32_t)(*s++ * mul);
         } while (--i);
      }
   }
}

void
_batch_cvt24_ps24_avx(void_ptr dst, const_void_ptr src, size_t num)
{
   int32_t *d = (int32_t*)dst;
   float *s = (float*)src;
   size_t i, step;
   size_t dtmp, stmp;

   if (!num) return;

   dtmp = (size_t)d & MEMMASK;
   stmp = (size_t)s & MEMMASK;
   if ((dtmp || stmp) && dtmp != stmp)  /* improperly aligned,            */
   {                                    /* let the compiler figure it out */
      i = num;
      do {
         *d++ += (int32_t)*s++;
      }
      while (--i);
      return;
   }

   /* work towards a 16-byte aligned d (and hence 16-byte aligned sptr) */
   if (dtmp && num)
   {
      i = (0x20 - dtmp)/sizeof(int32_t);
      if (i <= num)
      {
         num -= i;
         do {
            *d++ += (int32_t)*s++;
         } while(--i);
      }
   }

   if (num)
   {
      __m256i *dptr = (__m256i*)d;
      __m256* sptr = (__m256*)s;

      step = 4*sizeof(__m256)/sizeof(float);

      i = num/step;
      num -= i*step;
      if (i)
      {
         __m256i ymm4i, ymm5i, ymm6i, ymm7i;
         __m256 ymm0, ymm1, ymm2, ymm3;
         do
         {
            _mm_prefetch(((char *)s)+CACHE_ADVANCE_CVT, _MM_HINT_NTA);

            ymm0 = _mm256_load_ps((const float*)sptr++);
            ymm1 = _mm256_load_ps((const float*)sptr++);
            ymm2 = _mm256_load_ps((const float*)sptr++);
            ymm3 = _mm256_load_ps((const float*)sptr++);

            ymm4i = _mm256_cvtps_epi32(ymm0);
            ymm5i = _mm256_cvtps_epi32(ymm1);
            ymm6i = _mm256_cvtps_epi32(ymm2);
            ymm7i = _mm256_cvtps_epi32(ymm3);

            d += step;
            s += step;

            _mm256_store_si256(dptr++, ymm4i);
            _mm256_store_si256(dptr++, ymm5i);
            _mm256_store_si256(dptr++, ymm6i);
            _mm256_store_si256(dptr++, ymm7i);
         }
         while(--i);
      }

      if (num)
      {
         i = num;
         do {
            *d++ = (int32_t)*s++;
         } while (--i);
      }
   }
}

void
_batch_cvtps_24_avx(void_ptr dst, const_void_ptr src, size_t num)
{
   int32_t *s = (int32_t*)src;
   float *d = (float*)dst;

   assert(((size_t)d & MEMMASK) == 0);
   assert(((size_t)s & MEMMASK) == 0);

   if (num)
   {
      __m256i* sptr = (__m256i*)s;
      __m256 *dptr = (__m256*)d;
      size_t i, step;

      step = 4*sizeof(__m256i)/sizeof(int32_t);

      i = num/step;
      num -= i*step;
      if (i)
      {
         __m256i ymm0i, ymm1i, ymm2i, ymm3i;
         __m256 ymm4, ymm5, ymm6, ymm7;
         __m256 mul = _mm256_set1_ps(1.0f/(float)(1<<23));
         do
         {
            _mm_prefetch(((char *)s)+CACHE_ADVANCE_CVT, _MM_HINT_NTA);

            ymm0i = _mm256_load_si256(sptr++);
            ymm1i = _mm256_load_si256(sptr++);
            ymm2i = _mm256_load_si256(sptr++);
            ymm3i = _mm256_load_si256(sptr++);

            ymm4 = _mm256_cvtepi32_ps(ymm0i);
            ymm5 = _mm256_cvtepi32_ps(ymm1i);
            ymm6 = _mm256_cvtepi32_ps(ymm2i);
            ymm7 = _mm256_cvtepi32_ps(ymm3i);

            ymm4 = _mm256_mul_ps(ymm4, mul);
            ymm5 = _mm256_mul_ps(ymm5, mul);
            ymm6 = _mm256_mul_ps(ymm6, mul);
            ymm7 = _mm256_mul_ps(ymm7, mul);

            s += step;
            d += step;

            _mm256_store_ps((float*)dptr++, ymm4);
            _mm256_store_ps((float*)dptr++, ymm5);
            _mm256_store_ps((float*)dptr++, ymm6);
            _mm256_store_ps((float*)dptr++, ymm7);;
         }
         while(--i);
      }

      if (num)
      {
         float mul = 1.0f/(float)(1<<23);
         i = num;
         do {
            *d++ = (float)(*s++) * mul;
         } while (--i);
      }
   }
}

void
_batch_cvtps24_24_avx(void_ptr dst, const_void_ptr src, size_t num)
{
   int32_t *s = (int32_t*)src;
   float *d = (float*)dst;
   size_t i, step;
   size_t dtmp, stmp;

   assert(s != 0);
   assert(d != 0);

   if (!num) return;

   dtmp = (size_t)d & MEMMASK;
   stmp = (size_t)s & MEMMASK;
   if ((dtmp || stmp) && dtmp != stmp)  /* improperly aligned,            */
   {                                    /* let the compiler figure it out */
      i = num;
      do {
         *d++ += (float)*s++;
      }
      while (--i);
      return;
   }

   /* work towards a 16-byte aligned d (and hence 16-byte aligned sptr) */
   if (dtmp && num)
   {
      i = (0x20 - dtmp)/sizeof(int32_t);
      if (i <= num)
      {
         num -= i;
         do {
            *d++ += (float)*s++;
         } while(--i);
      }
   }

   if (num)
   {
      __m256i* sptr = (__m256i*)s;
      __m256 *dptr = (__m256*)d;

      step = 4*sizeof(__m256i)/sizeof(int32_t);

      i = num/step;
      num -= i*step;
      if (i)
      {
         __m256i ymm0i, ymm1i, ymm2i, ymm3i;
         __m256 ymm4, ymm5, ymm6, ymm7;
         do
         {
            _mm_prefetch(((char *)s)+CACHE_ADVANCE_CVT, _MM_HINT_NTA);

            ymm0i = _mm256_load_si256(sptr++);
            ymm1i = _mm256_load_si256(sptr++);
            ymm2i = _mm256_load_si256(sptr++);
            ymm3i = _mm256_load_si256(sptr++);

            ymm4 = _mm256_cvtepi32_ps(ymm0i);
            ymm5 = _mm256_cvtepi32_ps(ymm1i);
            ymm6 = _mm256_cvtepi32_ps(ymm2i);
            ymm7 = _mm256_cvtepi32_ps(ymm3i);

            s += step;
            d += step;

            _mm256_store_ps((float*)dptr++, ymm4);
            _mm256_store_ps((float*)dptr++, ymm5);
            _mm256_store_ps((float*)dptr++, ymm6);
            _mm256_store_ps((float*)dptr++, ymm7);
         }
         while(--i);
      }

      if (num)
      {
         i = num;
         do {
            *d++ = (float)*s++;
         } while (--i);
      }
   }
}

static void
_batch_iadd_avx(int32_ptr dst, const_int32_ptr src, size_t num)
{
   int32_ptr d = (int32_ptr)dst;
   int32_ptr s = (int32_ptr)src;
   size_t i, step, dtmp, stmp;

   dtmp = (size_t)d & MEMMASK;
   stmp = (size_t)s & MEMMASK;
   if ((dtmp || stmp) && dtmp != stmp)  /* improperly aligned,            */
   {                                    /* let the compiler figure it out */
      i = num;
      do {
         *d++ += *s++;
      } while (--i);
      return;
   }

   /* work towards a 16-byte aligned d (and hence 16-byte aligned sptr) */
   if (dtmp && num)
   {
      i = (0x10 - dtmp)/sizeof(int32_t);
      if (i <= num)
      {
         num -= i;
         do
         {
            *d++ += *s++;
         } while(--i);
      }
   }

   step = 2*sizeof(__m128i)/sizeof(int32_t);

   i = num/step;
   num -= i*step;
   if (i)
   {
      _mm256_zeroall();
      __m128i *sptr = (__m128i *)s;
      __m128i *dptr = (__m128i *)d;
      __m128i xmm0i, xmm3i, xmm4i, xmm7i;

      do
      {
         _mm_prefetch(((char *)sptr)+CACHE_ADVANCE_IMADD, _MM_HINT_NTA);;
         xmm0i = _mm_load_si128(sptr++);
         xmm4i = _mm_load_si128(sptr++);

         s += step;

         xmm3i = _mm_load_si128(dptr);
         xmm7i = _mm_load_si128(dptr+1);

         d += step;

         xmm0i = _mm_add_epi32(xmm0i, xmm3i);
         xmm4i = _mm_add_epi32(xmm4i, xmm7i);

         _mm_store_si128(dptr++, xmm0i);
         _mm_store_si128(dptr++, xmm4i);
      }
      while(--i);
   }

   if (num)
   {
      i = num;
      do {
         *d++ += *s++;
      } while(--i);
   }
}

void
_batch_imadd_avx(int32_ptr dst, const_int32_ptr src, size_t num, float v, float vstep)
{
   int32_ptr d = (int32_ptr)dst;
   int32_ptr s = (int32_ptr)src;
   size_t i, step, dtmp, stmp;

   if (!num || (v == 0.0f && vstep == 0.0f)) return;
   if (fabsf(v - 1.0f) < GMATH_128DB && vstep == 0.0f) {
      _batch_iadd_avx(dst, src, num);
      return;
   }

   dtmp = (size_t)d & MEMMASK;
   stmp = (size_t)s & MEMMASK;
   if ((dtmp || stmp) && dtmp != stmp)  /* improperly aligned,            */
   {                                    /* let the compiler figure it out */
      i = num;
      do
      {
         *d++ += (int32_t)((float)*s++ * v);
         v += vstep;
      }
      while (--i);
      return;
   }

   /* work towards a 16-byte aligned d (and hence 16-byte aligned sptr) */
   if (dtmp && num)
   {
      i = (0x10 - dtmp)/sizeof(int32_t);
      if (i <= num)
      {
         num -= i;
         do
         {
            *d++ += (int32_t)((float)*s++ * v);
            v += vstep;
         } while(--i);
      }
   }

   step = 2*sizeof(__m128i)/sizeof(int32_t);

   vstep *= step;                               /* 8 samples at a time */
   i = num/step;
   num -= i*step;
   if (i)
   {
      _mm256_zeroall();
      __m128i *sptr = (__m128i *)s;
      __m128i *dptr = (__m128i *)d;
      __m128 tv = _mm_set1_ps(v);
      __m128i xmm0i, xmm3i, xmm4i, xmm7i;
      __m128 xmm1, xmm5;

      do
      {
         _mm_prefetch(((char *)sptr)+CACHE_ADVANCE_IMADD, _MM_HINT_NTA);
         xmm0i = _mm_load_si128(sptr++);
         xmm4i = _mm_load_si128(sptr++);

         xmm1 = _mm_cvtepi32_ps(xmm0i);
         xmm5 = _mm_cvtepi32_ps(xmm4i);

         s += step;

         xmm1 = _mm_mul_ps(xmm1, tv);
         xmm5 = _mm_mul_ps(xmm5, tv);

         xmm0i = _mm_load_si128(dptr);
         xmm4i = _mm_load_si128(dptr+1);

         xmm3i = _mm_cvtps_epi32(xmm1);
         xmm7i = _mm_cvtps_epi32(xmm5);

         d += step;

         xmm0i = _mm_add_epi32(xmm0i, xmm3i);
         xmm4i = _mm_add_epi32(xmm4i, xmm7i);

         v += vstep;

         _mm_store_si128(dptr++, xmm0i);
         _mm_store_si128(dptr++, xmm4i);

         tv = _mm_set1_ps(v);
      }
      while(--i);
   }

   if (num)
   {
      vstep /= step;
      i = num;
      do {
         *d++ += (int32_t)((float)*s++ * v);
         v += vstep;
      } while(--i);
   }
}

static void
_batch_fadd_avx(float32_ptr dst, const_float32_ptr src, size_t num)
{
   float32_ptr s = (float32_ptr)src;
   float32_ptr d = (float32_ptr)dst;
   size_t i, step, dtmp, stmp;

   dtmp = (size_t)d & MEMMASK;
   stmp = (size_t)s & MEMMASK;
   if ((dtmp || stmp) && dtmp != stmp)
   {
      i = num;                          /* improperly aligned,            */
      do                                /* let the compiler figure it out */
      {
         *d++ += *s++;
      }
      while (--i);
      return;
   }

   /* work towards a 16-byte aligned d (and hence 16-byte aligned s) */
   if (dtmp && num)
   {
      i = (0x20 - dtmp)/sizeof(int32_t);
      if (i <= num)
      {
         num -= i;
         do {
            *d++ += *s++;
         } while(--i);
      }
   }

   step = 4*sizeof(__m256)/sizeof(float);

   i = num/step;
   num -= i*step;
   if (i)
   {
      __m256* sptr = (__m256*)s;
      __m256 *dptr = (__m256*)d;
      __m256 ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7;

      do
      {
         _mm_prefetch(((char *)s)+CACHE_ADVANCE_FMADD, _MM_HINT_NTA);
         _mm_prefetch(((char *)d)+CACHE_ADVANCE_FMADD, _MM_HINT_NTA);

         ymm0 = _mm256_load_ps((const float*)sptr++);
         ymm1 = _mm256_load_ps((const float*)sptr++);
         ymm4 = _mm256_load_ps((const float*)sptr++);
         ymm5 = _mm256_load_ps((const float*)sptr++);

         s += step;
         d += step;

         ymm2 = _mm256_load_ps((const float*)dptr);
         ymm3 = _mm256_load_ps((const float*)(dptr+1));
         ymm6 = _mm256_load_ps((const float*)(dptr+2));
         ymm7 = _mm256_load_ps((const float*)(dptr+3));

         ymm2 = _mm256_add_ps(ymm2, ymm0);
         ymm3 = _mm256_add_ps(ymm3, ymm1);
         ymm6 = _mm256_add_ps(ymm6, ymm4);
         ymm7 = _mm256_add_ps(ymm7, ymm5);

         _mm256_store_ps((float*)dptr++, ymm2);
         _mm256_store_ps((float*)dptr++, ymm3);
         _mm256_store_ps((float*)dptr++, ymm6);
         _mm256_store_ps((float*)dptr++, ymm7);
      }
      while(--i);
   }

   if (num)
   {
      i = num;
      do {
         *d++ += *s++;
      } while(--i);
   }
}


void
_batch_fmadd_avx(float32_ptr dst, const_float32_ptr src, size_t num, float v, float vstep)
{
   float32_ptr s = (float32_ptr)src;
   float32_ptr d = (float32_ptr)dst;
   size_t i, step, dtmp, stmp;

   if (!num || (v == 0.0f && vstep == 0.0f)) return;
   if (fabsf(v - 1.0f) < GMATH_128DB && vstep == 0.0f) {
      _batch_fadd_avx(dst, src, num);
      return;
   }

   dtmp = (size_t)d & MEMMASK;
   stmp = (size_t)s & MEMMASK;
   if ((dtmp || stmp) && dtmp != stmp)
   {
      i = num;				/* improperly aligned,            */
      do				/* let the compiler figure it out */
      {
         *d++ += *s++ * v;
         v += vstep;
      }
      while (--i);
      return;
   }

   /* work towards a 16-byte aligned d (and hence 16-byte aligned s) */
   if (dtmp && num)
   {
      i = (0x20 - dtmp)/sizeof(int32_t);
      if (i <= num)
      {
         num -= i;
         do {
            *d++ += *s++ * v;
            v += vstep;
         } while(--i);
      }
   }

   step = 4*sizeof(__m256)/sizeof(float);

   vstep *= step;				/* 8 samples at a time */
   i = num/step;
   num -= i*step;
   if (i)
   {
      __m256* sptr = (__m256*)s;
      __m256 *dptr = (__m256*)d;
      __m256 ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7;
      __m256 tv = _mm256_set1_ps(v);

      do
      {
         _mm_prefetch(((char *)s)+CACHE_ADVANCE_FMADD, _MM_HINT_NTA);
         _mm_prefetch(((char *)d)+CACHE_ADVANCE_FMADD, _MM_HINT_NTA);

         ymm0 = _mm256_load_ps((const float*)sptr++);
         ymm1 = _mm256_load_ps((const float*)sptr++);
         ymm4 = _mm256_load_ps((const float*)sptr++);
         ymm5 = _mm256_load_ps((const float*)sptr++);

         ymm0 = _mm256_mul_ps(ymm0, tv);
         ymm1 = _mm256_mul_ps(ymm1, tv);
         ymm4 = _mm256_mul_ps(ymm4, tv);
         ymm5 = _mm256_mul_ps(ymm5, tv);

         s += step;
         d += step;

         ymm2 = _mm256_load_ps((const float*)dptr);
         ymm3 = _mm256_load_ps((const float*)(dptr+1));
         ymm6 = _mm256_load_ps((const float*)(dptr+2));
         ymm7 = _mm256_load_ps((const float*)(dptr+3));

         ymm2 = _mm256_add_ps(ymm2, ymm0);
         ymm3 = _mm256_add_ps(ymm3, ymm1);
         ymm6 = _mm256_add_ps(ymm6, ymm4);
         ymm7 = _mm256_add_ps(ymm7, ymm5);

         v += vstep;

         _mm256_store_ps((float*)dptr++, ymm2);
         _mm256_store_ps((float*)dptr++, ymm3);
         _mm256_store_ps((float*)dptr++, ymm6);
         _mm256_store_ps((float*)dptr++, ymm7);

         tv = _mm256_set1_ps(v);
      }
      while(--i);
   }

   if (num)
   {
      vstep /= step;
      i = num;
      do {
         *d++ += *s++ * v;
         v += vstep;
      } while(--i);
   }
}

void
_batch_cvt24_16_avx(void_ptr dst, const_void_ptr src, size_t num)
{
   int16_t *s = (int16_t *)src;
   int32_t *d = (int32_t*)dst;
   size_t i, step;
   size_t tmp;

   if (!num) return;

   /*
    * work towards 16-byte aligned d
    */
   tmp = (size_t)d & MEMMASK;
   if (tmp && num)
   {
      i = (0x10 - tmp)/sizeof(int32_t);
      if (i <= num)
      {
         num -= i;
         do {
            *d++ = *s++ << 8;
         } while(--i);
      }
   }

   step = 2*sizeof(__m128i)/sizeof(int16_t);

   tmp = (size_t)s & MEMMASK;
   i = num/step;
   num -= i*step;
   if (i)
   {
      _mm256_zeroall();
      __m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;
      __m128i zero = _mm_setzero_si128();
      __m128i *dptr = (__m128i *)d;
      __m128i *sptr = (__m128i *)s;

      do {
         _mm_prefetch(((char *)s)+CACHE_ADVANCE_CVT, _MM_HINT_NTA);

         if (tmp)
         {
            xmm0 = _mm_loadu_si128(sptr++);
            xmm4 = _mm_loadu_si128(sptr++);

            xmm1 = _mm_unpacklo_epi16(zero, xmm0);;
            xmm3 = _mm_unpackhi_epi16(zero, xmm0);
            xmm5 = _mm_unpacklo_epi16(zero, xmm4);
            xmm7 = _mm_unpackhi_epi16(zero, xmm4);
         }
         else
         {
            xmm0 = _mm_load_si128(sptr++);
            xmm4 = _mm_load_si128(sptr++);

            xmm1 = _mm_unpacklo_epi16(zero, xmm0);
            xmm3 = _mm_unpackhi_epi16(zero, xmm0);
            xmm5 = _mm_unpacklo_epi16(zero, xmm4);
            xmm7 = _mm_unpackhi_epi16(zero, xmm4);
         }

         s += step;
         d += step;

         xmm0 = _mm_srai_epi32(xmm1, 8);
         xmm2 = _mm_srai_epi32(xmm3, 8);
         xmm4 = _mm_srai_epi32(xmm5, 8);
         xmm6 = _mm_srai_epi32(xmm7, 8);

         _mm_store_si128(dptr++, xmm0);
         _mm_store_si128(dptr++, xmm2);
         _mm_store_si128(dptr++, xmm4);
         _mm_store_si128(dptr++, xmm6);
      }
      while (--i);
   }

   if (num)
   {
      i = num;
      do {
         *d++ = *s++ << 8;
      } while (--i);
   }
}

void
_batch_cvt16_24_avx(void_ptr dst, const_void_ptr src, size_t num)
{
   size_t i, step;
   int32_t* s = (int32_t*)src;
   int16_t* d = (int16_t*)dst;
   size_t tmp;

   /*
    * work towards 16-byte aligned sptr
    */
   tmp = (size_t)s & MEMMASK;
   if (tmp && num)
   {
      i = (0x10 - tmp)/sizeof(int32_t);
      if (i <= num)
      {
         num -= i;
         do {
            *d++ = *s++ >> 8;
         } while(--i);
      }
   }

   assert(((size_t)s & MEMMASK) == 0);
   tmp = (size_t)d & MEMMASK;

   step = 4*sizeof(__m128i)/sizeof(int32_t);

   i = num/step;
   num -= i*step;
   if (i)
   {
      _mm256_zeroall();
      __m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;
      __m128i *dptr, *sptr;

      dptr = (__m128i *)d;
      sptr = (__m128i *)s;
      do
      {
         _mm_prefetch(((char *)s)+CACHE_ADVANCE_CVT, _MM_HINT_NTA);

         xmm0 = _mm_load_si128(sptr++);
         xmm1 = _mm_load_si128(sptr++);
         xmm4 = _mm_load_si128(sptr++);
         xmm5 = _mm_load_si128(sptr++);

         xmm2 = _mm_srai_epi32(xmm0, 8);
         xmm3 = _mm_srai_epi32(xmm1, 8);
         xmm6 = _mm_srai_epi32(xmm4, 8);
         xmm7 = _mm_srai_epi32(xmm5, 8);

         s += step;

         xmm4 = _mm_packs_epi32(xmm2, xmm3);
         xmm6 = _mm_packs_epi32(xmm6, xmm7);

         d += step;

         if (tmp) {
            _mm_storeu_si128(dptr++, xmm4);
            _mm_storeu_si128(dptr++, xmm6);
         } else {
            _mm_store_si128(dptr++, xmm4);
            _mm_store_si128(dptr++, xmm6);
         }
      }
      while (--i);
   }

   if (num)
   {
      i = num;
      do {
         *d++ = *s++ >> 8;
      } while (--i);
   }
}

void
_batch_cvt16_intl_24_avx(void_ptr dst, const_int32_ptrptr src,
                                size_t offset, unsigned int tracks,
                                size_t num)
{
   size_t i, step;
   int16_t *d = (int16_t*)dst;
   int32_t *s1, *s2;
   size_t tmp;

   if (!num) return;

   if (tracks != 2)
   {
      size_t t;
      for (t=0; t<tracks; t++)
      {
         int32_t *s = (int32_t *)src[t] + offset;
         int16_t *d = (int16_t *)dst + t;
         size_t i = num;

         do
         {
            *d = *s++ >> 8;
            d += tracks;
         }
         while (--i);
      }
      return;
   }

   s1 = (int32_t *)src[0] + offset;
   s2 = (int32_t *)src[1] + offset;

   step = 2*sizeof(__m128i)/sizeof(int32_t);

   /*
    * work towards 16-byte aligned sptr
    */
   tmp = (size_t)s1 & MEMMASK;
   assert(tmp == ((size_t)s2 & MEMMASK));

   i = num/step;
   if (tmp && i)
   {
      i = (0x10 - tmp)/sizeof(int32_t);
      num -= i;
      do
      {
         *d++ = *s1++ >> 8;
         *d++ = *s2++ >> 8;
      }
      while (--i);
   }

   tmp = (size_t)d & MEMMASK;
   i = num/step;
   num -= i*step;
   if (i)
   {
      _mm256_zeroall();
      __m128i mask, xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7;
      __m128i *dptr, *sptr1, *sptr2;

      sptr1 = (__m128i*)s1;
      sptr2 = (__m128i*)s2;

      mask = _mm_set_epi32(0x00FFFF00, 0x00FFFF00, 0x00FFFF00, 0x00FFFF00);
      dptr = (__m128i *)d;
      do
      {
         _mm_prefetch(((char *)s1)+CACHE_ADVANCE_INTL, _MM_HINT_NTA);
         _mm_prefetch(((char *)s2)+CACHE_ADVANCE_INTL, _MM_HINT_NTA);

         xmm0 = _mm_load_si128(sptr1++);
         xmm4 = _mm_load_si128(sptr1++);
         xmm1 = _mm_load_si128(sptr2++);
         xmm5 = _mm_load_si128(sptr2++);

         xmm2 = _mm_and_si128(xmm0, mask);
         xmm3 = _mm_and_si128(xmm1, mask);
         xmm6 = _mm_and_si128(xmm4, mask);
         xmm7 = _mm_and_si128(xmm5, mask);

         s1 += step;
         s2 += step;

         xmm0 = _mm_srli_epi32(xmm2, 8);
         xmm1 = _mm_slli_epi32(xmm3, 8);
         xmm4 = _mm_srli_epi32(xmm6, 8);
         xmm5 = _mm_slli_epi32(xmm7, 8);

         xmm0 = _mm_or_si128(xmm1, xmm0);
         xmm4 = _mm_or_si128(xmm5, xmm4);;

         d += 2*step;

         if (tmp) {
            _mm_storeu_si128(dptr++, xmm0);
            _mm_storeu_si128(dptr++, xmm4);
         } else {
            _mm_store_si128(dptr++, xmm0);
            _mm_store_si128(dptr++, xmm4);
         }
      } while (--i);
   }

   if (num)
   {
      i = num;
      do
      {
         *d++ = *s1++ >> 8;
         *d++ = *s2++ >> 8;
      }
      while (--i);
   }
}


void
_batch_freqfilter_avx(int32_ptr dptr, const_int32_ptr sptr, int t, size_t num, void *flt)
{
   _aaxRingBufferFreqFilterData *filter = (_aaxRingBufferFreqFilterData*)flt;
   const_int32_ptr s = sptr;

   if (num)
   {
      _mm256_zeroall();
      __m128 c, h, mk;
      float *cptr, *hist;
      int stages;

      cptr = filter->coeff;
      hist = filter->freqfilter_history[t];
      stages = filter->no_stages;
      if (!stages) stages++;

      if (filter->state) {
         mk = _mm_set_ss(filter->k * (filter->high_gain - filter->low_gain));
      } else {
         mk = _mm_set_ss(filter->k * filter->high_gain);
      }

      do
      {
         int32_ptr d = dptr;
         size_t i = num;

//       c = _mm_set_ps(cptr[3], cptr[1], cptr[2], cptr[0]);
         c = _mm_load_ps(cptr);
         c = _mm_shuffle_ps(c, c, _MM_SHUFFLE(3,1,2,0));

//       h = _mm_set_ps(hist[1], hist[1], hist[0], hist[0]);
         h = _mm_loadl_pi(_mm_setzero_ps(), (__m64*)hist);
         h = _mm_shuffle_ps(h, h, _MM_SHUFFLE(1,1,0,0));

         do
         {
            __m128 pz, smp, nsmp, tmp;

            smp = _mm_cvtepi32_ps((__m128i)_mm_load_ss((const float*)s));

            // pz = { c[3]*h1, -c[1]*h1, c[2]*h0, -c[0]*h0 };
            pz = _mm_mul_ps(c, h); // poles and zeros

            // smp = *s++ * k;;
            smp = _mm_mul_ss(smp, mk);

            // tmp[0] = -c[0]*h0 + -c[1]*h1;
            tmp = _mm_add_ps(pz, _mm_shuffle_ps(pz, pz, _MM_SHUFFLE(1,3,0,2)));
            s++;

            // nsmp = smp - h0*c[0] - h1*c[1];
            nsmp = _mm_add_ss(smp, tmp);

            // h1 = h0, h0 = smp: h = { h0, h0, smp, smp };
            h = _mm_shuffle_ps(nsmp, h, _MM_SHUFFLE(0,0,0,0));

            // tmp[0] = -c[0]*h0 + -c[1]*h1 + c[2]*h0 + c[3]*h1;
            tmp = _mm_add_ps(tmp, _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(0,1,2,3)));

            // smp = smp - h0*c[0] - h1*c[1] + h0*c[2] + h1*c[3];
            smp = _mm_add_ss(smp, tmp);
            _mm_store_ss((float*)d++, (__m128)_mm_cvtps_epi32(smp));
         }
         while (--i);

         _mm_storel_pi((__m64*)hist, h);

         hist += 2;
         cptr += 4;
         mk = _mm_set_ss(1.0f);
         s = dptr;
      }
      while (--stages);
   }
}


void
_batch_freqfilter_float_avx(float32_ptr dptr, const_float32_ptr sptr, int t, size_t num, void *flt)
{
   _aaxRingBufferFreqFilterData *filter = (_aaxRingBufferFreqFilterData*)flt;
   const_float32_ptr s = sptr;

   if (num)
   {
      _mm256_zeroall();
      __m128 c, h, mk;
      float *cptr, *hist;
      int stages;;

      cptr = filter->coeff;
      hist = filter->freqfilter_history[t];
      stages = filter->no_stages;
      if (!stages) stages++;

      if (filter->state) {
         mk = _mm_set_ss(filter->k * (filter->high_gain - filter->low_gain));
      } else {
         mk = _mm_set_ss(filter->k * filter->high_gain);
      }

      do
      {
         float32_ptr d = dptr;
         size_t i = num;

//       c = _mm_set_ps(cptr[3], cptr[1], cptr[2], cptr[0]);
         if (((size_t)cptr & MEMMASK) == 0) {
            c = _mm_load_ps(cptr);
         } else {
            c = _mm_loadu_ps(cptr);
         }
         c = _mm_shuffle_ps(c, c, _MM_SHUFFLE(3,1,2,0));

//       h = _mm_set_ps(hist[1], hist[1], hist[0], hist[0]);
         h = _mm_loadl_pi(_mm_setzero_ps(), (__m64*)hist);
         h = _mm_shuffle_ps(h, h, _MM_SHUFFLE(1,1,0,0));

         do
         {
            __m128 pz, smp, nsmp, tmp;

            smp = _mm_load_ss(s);

            // pz = { c[3]*h1, -c[1]*h1, c[2]*h0, -c[0]*h0 };
            pz = _mm_mul_ps(c, h); // poles and zeros

            // smp = *s++ * k;
            smp = _mm_mul_ss(smp, mk);

            // tmp[0] = -c[0]*h0 + -c[1]*h1;
            tmp = _mm_add_ps(pz, _mm_shuffle_ps(pz, pz, _MM_SHUFFLE(1,3,0,2)));
            s++;

            // nsmp = smp - h0*c[0] - h1*c[1];
            nsmp = _mm_add_ss(smp, tmp);

            // h1 = h0, h0 = smp: h = { h0, h0, smp, smp };
            h = _mm_shuffle_ps(nsmp, h, _MM_SHUFFLE(0,0,0,0));

            // tmp[0] = -c[0]*h0 + -c[1]*h1 + c[2]*h0 + c[3]*h1;
            tmp = _mm_add_ps(tmp, _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(0,1,2,3)));

            // smp = smp - h0*c[0] - h1*c[1] + h0*c[2] + h1*c[3];
            smp = _mm_add_ss(smp, tmp);
            _mm_store_ss(d++, smp);
         }
         while (--i);

         h = _mm_shuffle_ps(h, h, _MM_SHUFFLE(3,1,2,0));
         _mm_storel_pi((__m64*)hist, h);

         hist += 2;
         cptr += 4;
         mk = _mm_set_ss(1.0f);
         s = dptr;
      }
      while (--stages);
   }
}


/*
 * optimized memcpy for 16-byte aligned destination buffer
 * fall back tobuildt-in  memcpy otherwise.
 */
void *
_aax_memcpy_avx(void_ptr dst, const_void_ptr src, size_t num)
{
   size_t i, step;
   char *d = (char*)dst;
   char *s = (char*)src;
   size_t tmp;

   if (!num) return dst;

   /*
    * work towards a 16-byte aligned dptr and possibly sptr
    */
   tmp = (size_t)d & MEMMASK;
   if (tmp)
   {
      i = (0x20 - tmp);
      num -= i;

      memcpy(d, s, i);
      d += i;
      s += i;
   }

   tmp = (size_t)s & MEMMASK;
   step = 8*sizeof(__m256i)/sizeof(int8_t);

   i = num/step;
   num -= i*step;
   if (i)
   {
      __m256i ymm0, ymm1, ymm2, ymm3, ymm4, ymm5, ymm6, ymm7;
      __m256i *sptr = (__m256i*)s;
      __m256i *dptr = (__m256i*)d;

      s += i*step;
      d += i*step;

      if (tmp)
      {
         do
         {
            _mm_prefetch(((char *)s)+CACHE_ADVANCE_CPY, _MM_HINT_NTA);

            ymm0 = _mm256_loadu_si256(sptr++);
            ymm1 = _mm256_loadu_si256(sptr++);
            ymm2 = _mm256_loadu_si256(sptr++);
            ymm3 = _mm256_loadu_si256(sptr++);
            ymm4 = _mm256_loadu_si256(sptr++);
            ymm5 = _mm256_loadu_si256(sptr++);
            ymm6 = _mm256_loadu_si256(sptr++);
            ymm7 = _mm256_loadu_si256(sptr++);

            _mm256_store_si256(dptr++, ymm0);
            _mm256_store_si256(dptr++, ymm1);
            _mm256_store_si256(dptr++, ymm2);
            _mm256_store_si256(dptr++, ymm3);
            _mm256_store_si256(dptr++, ymm4);
            _mm256_store_si256(dptr++, ymm5);
            _mm256_store_si256(dptr++, ymm6);
            _mm256_store_si256(dptr++, ymm7);
         }
         while(--i);
      }
      else	/* both buffers are 16-byte aligned */
      {
         do
         {
            _mm_prefetch(((char *)s)+CACHE_ADVANCE_CPY, _MM_HINT_NTA);

            ymm0 = _mm256_load_si256(sptr++);
            ymm1 = _mm256_load_si256(sptr++);
            ymm2 = _mm256_load_si256(sptr++);
            ymm3 = _mm256_load_si256(sptr++);
            ymm4 = _mm256_load_si256(sptr++);
            ymm5 = _mm256_load_si256(sptr++);
            ymm6 = _mm256_load_si256(sptr++);
            ymm7 = _mm256_load_si256(sptr++);

            _mm256_store_si256(dptr++, ymm0);
            _mm256_store_si256(dptr++, ymm1);
            _mm256_store_si256(dptr++, ymm2);
            _mm256_store_si256(dptr++, ymm3);
            _mm256_store_si256(dptr++, ymm4);
            _mm256_store_si256(dptr++, ymm5);
            _mm256_store_si256(dptr++, ymm6);
            _mm256_store_si256(dptr++, ymm7);
         }
         while(--i);
      }
   }

   if (num) {
      memcpy(d, s, num);
   }

   return dst;
}

#if !RB_FLOAT_DATA
static inline void
_aaxBufResampleSkip_avx(int32_ptr d, const_int32_ptr s, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   int32_ptr sptr = (int32_ptr)s;
   int32_ptr dptr = d;
   int32_t samp, dsamp;
   size_t i;

   assert(s != 0);
   assert(d != 0);
   assert(dmin < dmax);
   assert(freq_factor >= 1.0f);
   assert(0.0f <= smu && smu < 1.0f);

   dptr += dmin;

   samp = *sptr++;              // n+(step-1)
   dsamp = *sptr - samp;        // (n+1) - n


   i=dmax-dmin;
   if (i)
   {
      do
      {
         size_t step;

         *dptr++ = samp + (int32_t)(dsamp * smu);

         smu += freq_factor;
         step = (size_t)floorf(smu);

         smu -= step;
         sptr += step-1;
         samp = *sptr++;
         dsamp = *sptr - samp;
      }
      while (--i);
   }
}

#if 0
static inline void
_aaxBufResampleNearest_avx(int32_ptr d, const_int32_ptr s, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   if (freq_factor == 1.0f) {
      _aax_memcpy(d+dmin, s, (dmax-dmin)*sizeof(int32_t));
   }
   else
   {
      int32_ptr sptr = (int32_ptr)s;
      int32_ptr dptr = d;
      size_t i;

      assert(s != 0);
      assert(d != 0);
      assert(dmin < dmax);
      assert(0.95f <= freq_factor && freq_factor <= 1.05f);
      assert(0.0f <= smu && smu < 1.0f);

      dptr += dmin;

      i = dmax-dmin;
      if (i)
      {
         do
         {
            *dptr++ = *sptr;

            smu += freq_factor;
            if (smu > 0.5f)
            {
               sptr++;
               smu -= 1.0f;
            }
         }
         while (--i);
      }
   }
}
#endif

static inline void
_aaxBufResampleLinear_avx(int32_ptr d, const_int32_ptr s, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   int32_ptr sptr = (int32_ptr)s;
   int32_ptr dptr = d;
   int32_t samp, dsamp;
   size_t i;

   assert(s != 0);
   assert(d != 0);
   assert(dmin < dmax);
   assert(freq_factor < 1.0f);
   assert(0.0f <= smu && smu < 1.0f);

   dptr += dmin;

   samp = *sptr++;		// n
   dsamp = *sptr - samp;	// (n+1) - n

   i = dmax-dmin;
   if (i)
   {
      do
      {
         *dptr++ = samp + (int32_t)(dsamp * smu);

         smu += freq_factor;
         if (smu >= 1.0)
         {
            smu -= 1.0;
            samp = *sptr++;
            dsamp = *sptr - samp;
         }
      }
      while (--i);
   }
}

static inline void
_aaxBufResampleCubic_avx(int32_ptr d, const_int32_ptr s, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   float y0, y1, y2, y3, a0, a1, a2;
   int32_ptr sptr = (int32_ptr)s;
   int32_ptr dptr = d;
   size_t i;

   assert(s != 0);
   assert(d != 0);
   assert(dmin < dmax);
   assert(0.0f <= smu && smu < 1.0f);
   assert(0.0f < freq_factor && freq_factor <= 1.0f);

   dptr += dmin;

   y0 = (float)*sptr++;
   y1 = (float)*sptr++;
   y2 = (float)*sptr++;
   y3 = (float)*sptr++;

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
         *dptr++ = (int32_t)ftmp;

         smu += freq_factor;
         if (smu >= 1.0)
         {
            smu--;
            a0 += y0;
            y0 = y1;
            y1 = y2;
            y2 = y3;
            y3 = (float)*sptr++;
            a0 = -a0 + y3;			/* a0 = y3 - y2 - y0 + y1; */
            a1 = y0 - y1 - a0;
            a2 = y2 - y0;
         }
      }
      while (--i);
   }
}

void
_batch_resample_avx(int32_ptr d, const_int32_ptr s, size_t dmin, size_t dmax, float smu, float fact)
{
   assert(fact > 0.0f);

   if (fact < CUBIC_TRESHOLD) {
      _aaxBufResampleCubic_avx(d, s, dmin, dmax, smu, fact);
   }
   else if (fact < 1.0f) {
      _aaxBufResampleLinear_avx(d, s, dmin, dmax, smu, fact);
   }
   else if (fact > 1.0f) {
      _aaxBufResampleSkip_avx(d, s, dmin, dmax, smu, fact);
   } else {
//    _aaxBufResampleNearest_avx(d, s, dmin, dmax, smu, fact);
      _aax_memcpy(d+dmin, s, (dmax-dmin)*sizeof(MIX_T));
   }
}
#else

static inline void
_aaxBufResampleSkip_float_avx(float32_ptr dptr, const_float32_ptr sptr, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   float32_ptr s = (float32_ptr)sptr;
   float32_ptr d = dptr;
   float samp, dsamp;
   size_t i;

   assert(s != 0);
   assert(d != 0);
   assert(dmin < dmax);
   assert(freq_factor >= 1.0f);
   assert(0.0f <= smu && smu < 1.0f);

   d += dmin;

   samp = *s++;			// n+(step-1)
   dsamp = *s - samp;		// (n+1) - n

   i = dmax-dmin;
   if (i)
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

#if 0
static inline void
_aaxBufResampleNearest_float_avx(float32_ptr d, const_float32_ptr s, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   if (freq_factor == 1.0f) {
      _aax_memcpy(d+dmin, s, (dmax-dmin)*sizeof(float));
   }
   else
   {
      float32_ptr sptr = (float32_ptr)s;
      float32_ptr dptr = d;
      size_t i;

      assert(s != 0);
      assert(d != 0);
      assert(dmin < dmax);
      assert(0.95f <= freq_factor && freq_factor <= 1.05f);
      assert(0.0f <= smu && smu < 1.0f);

      dptr += dmin;

      i = dmax-dmin;
      if (i)
      {
         do
         {
            *dptr++ = *sptr;

            smu += freq_factor;
            if (smu > 0.5f)
            {
               sptr++;
               smu -= 1.0f;
            }
         }
         while (--i);
      }
   }
}
#endif

static inline void
_aaxBufResampleLinear_float_avx(float32_ptr d, const_float32_ptr s, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   float32_ptr sptr = (float32_ptr)s;
   float32_ptr dptr = d;
   float samp, dsamp;
   size_t i;

   assert(s != 0);
   assert(d != 0);
   assert(dmin < dmax);
   assert(freq_factor < 1.0f);
   assert(0.0f <= smu && smu < 1.0f);

   dptr += dmin;

   samp = *sptr++;              // n
   dsamp = *sptr - samp;        // (n+1) - n

   i = dmax-dmin;
   if (i)
   {
      do
      {
         *dptr++ = samp + (dsamp * smu);

         smu += freq_factor;
         if (smu >= 1.0)
         {
            smu -= 1.0;
            samp = *sptr++;
            dsamp = *sptr - samp;
         }
      }
      while (--i);
   }
}

static inline void
_aaxBufResampleCubic_float_avx(float32_ptr d, const_float32_ptr s, size_t dmin, size_t dmax, float smu, float freq_factor)
{
   float y0, y1, y2, y3, a0, a1, a2;
   float32_ptr sptr = (float32_ptr)s;
   float32_ptr dptr = d;
   size_t i;

   assert(s != 0);
   assert(d != 0);
   assert(dmin < dmax);
   assert(0.0f <= smu && smu < 1.0f);
   assert(0.0f < freq_factor && freq_factor <= 1.0f);

   dptr += dmin;

   y0 = *sptr++;
   y1 = *sptr++;
   y2 = *sptr++;
   y3 = *sptr++;

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
         *dptr++ = ftmp;

         smu += freq_factor;
         if (smu >= 1.0)
         {
            smu--;
            a0 += y0;
            y0 = y1;
            y1 = y2;
            y2 = y3;
            y3 = *sptr++;
            a0 = -a0 + y3;                      /* a0 = y3 - y2 - y0 + y1; */
            a1 = y0 - y1 - a0;
            a2 = y2 - y0;
         }
      }
      while (--i);
   }
}

void
_batch_resample_float_avx(float32_ptr d, const_float32_ptr s, size_t dmin, size_t dmax, float smu, float fact)
{
   assert(fact > 0.0f);

   if (fact < CUBIC_TRESHOLD) {
      _aaxBufResampleCubic_float_avx(d, s, dmin, dmax, smu, fact);
   }
   else if (fact < 1.0f) {
      _aaxBufResampleLinear_float_avx(d, s, dmin, dmax, smu, fact);
   }
   else if (fact > 1.0f) {
      _aaxBufResampleSkip_float_avx(d, s, dmin, dmax, smu, fact);
   } else {
//    _aaxBufResampleNearest_float_avx(d, s, dmin, dmax, smu, fact);
      _aax_memcpy(d+dmin, s, (dmax-dmin)*sizeof(MIX_T));
   }
}
#endif // RB_FLOAT_DATA

#endif /* AVX */


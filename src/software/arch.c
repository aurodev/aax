/*
 * Copyright 2005-2014 by Erik Hofman.
 * Copyright 2009-2014 by Adalin B.V.
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

#include <stdio.h>	/* fopen, fclose */
#ifdef HAVE_RMALLOC_H
# include <rmalloc.h>
#else
# include <string.h>	/* strstr, strchr */
#endif

#include <api.h>
#include <arch.h>
#include <ringbuffer.h>

#include "rbuf_int.h"
#include "cpu/arch2d_simd.h"
#include "cpu/arch3d_simd.h"
#include "audio.h"

_aax_memcpy_proc _aax_memcpy = (_aax_memcpy_proc)memcpy;
_aax_free_proc _aax_free = (_aax_free_proc)_aax_free_align16;
_aax_calloc_proc _aax_calloc = (_aax_calloc_proc)_aax_calloc_align16;
_aax_malloc_proc _aax_malloc = (_aax_malloc_proc)_aax_malloc_align16;
_aax_memcpy_proc _batch_cvt24_24 = (_aax_memcpy_proc)_batch_cvt24_24_cpu;

_batch_cvt_from_proc _batch_cvt24_8 = _batch_cvt24_8_cpu;
_batch_cvt_from_proc _batch_cvt24_16 = _batch_cvt24_16_cpu;
_batch_cvt_from_proc _batch_cvt24_24_3 = _batch_cvt24_24_3_cpu;
_batch_cvt_from_proc _batch_cvt24_32 = _batch_cvt24_32_cpu;
_batch_cvt_from_proc _batch_cvt24_ps = _batch_cvt24_ps_cpu;
_batch_cvt_from_proc _batch_cvt24_pd = _batch_cvt24_pd_cpu;
_batch_cvt_from_intl_proc _batch_cvt24_8_intl = _batch_cvt24_8_intl_cpu;
_batch_cvt_from_intl_proc _batch_cvt24_16_intl = _batch_cvt24_16_intl_cpu;
_batch_cvt_from_intl_proc _batch_cvt24_24_3intl = _batch_cvt24_24_3intl_cpu;
_batch_cvt_from_intl_proc _batch_cvt24_24_intl = _batch_cvt24_24_intl_cpu;
_batch_cvt_from_intl_proc _batch_cvt24_32_intl = _batch_cvt24_32_intl_cpu;
_batch_cvt_from_intl_proc _batch_cvt24_ps_intl = _batch_cvt24_ps_intl_cpu;
_batch_cvt_from_intl_proc _batch_cvt24_pd_intl = _batch_cvt24_pd_intl_cpu;

_batch_cvt_proc _batch_saturate24 = _batch_saturate24_cpu;

_batch_cvt_proc _batch_cvt8u_8s = _batch_cvt8u_8s_cpu;
_batch_cvt_proc _batch_cvt8s_8u = _batch_cvt8s_8u_cpu;
_batch_cvt_proc _batch_cvt16u_16s = _batch_cvt16u_16s_cpu;
_batch_cvt_proc _batch_cvt16s_16u = _batch_cvt16s_16u_cpu;
_batch_cvt_proc _batch_cvt24u_24s = _batch_cvt24u_24s_cpu;
_batch_cvt_proc _batch_cvt24s_24u = _batch_cvt24s_24u_cpu;
_batch_cvt_proc _batch_cvt32u_32s = _batch_cvt32u_32s_cpu;
_batch_cvt_proc _batch_cvt32s_32u = _batch_cvt32s_32u_cpu;

_batch_cvt_proc _batch_endianswap16 = _batch_endianswap16_cpu;
_batch_cvt_proc _batch_endianswap32 = _batch_endianswap32_cpu;
_batch_cvt_proc _batch_endianswap64 = _batch_endianswap64_cpu;

_batch_cvt_to_proc _batch_cvt8_24 = _batch_cvt8_24_cpu;
_batch_cvt_to_proc _batch_cvt16_24 = _batch_cvt16_24_cpu;
_batch_cvt_to_proc _batch_cvt24_3_24 = _batch_cvt24_3_24_cpu;
_batch_cvt_to_proc _batch_cvt32_24 = _batch_cvt32_24_cpu;
_batch_cvt_to_proc _batch_cvtps_24 = _batch_cvtps_24_cpu;
_batch_cvt_to_proc _batch_cvtpd_24 = _batch_cvtpd_24_cpu;
_batch_cvt_to_intl_proc _batch_cvt8_intl_24 = _batch_cvt8_intl_24_cpu;
_batch_cvt_to_intl_proc _batch_cvt16_intl_24 = _batch_cvt16_intl_24_cpu;
_batch_cvt_to_intl_proc _batch_cvt24_3intl_24 = _batch_cvt24_3intl_24_cpu;
_batch_cvt_to_intl_proc _batch_cvt24_intl_24 = _batch_cvt24_intl_24_cpu;
_batch_cvt_to_intl_proc _batch_cvt32_intl_24 = _batch_cvt32_intl_24_cpu;
_batch_cvt_to_intl_proc _batch_cvtps_intl_24 = _batch_cvtps_intl_24_cpu;
_batch_cvt_to_intl_proc _batch_cvtpd_intl_24 = _batch_cvtpd_intl_24_cpu;

_batch_imadd_proc _batch_imadd = _batch_imadd_cpu;
_batch_fmadd_proc _batch_fmadd = _batch_fmadd_cpu;
_batch_mul_value_proc _batch_imul_value = _batch_imul_value_cpu;
_batch_mul_value_proc _batch_fmul_value = _batch_fmul_value_cpu;
_batch_freqfilter_proc _batch_freqfilter = _batch_freqfilter_cpu;
_batch_freqfilter_float_proc _batch_freqfilter_float = _batch_freqfilter_float_cpu;

#if RB_FLOAT_DATA
_batch_cvt_from_proc _batch_cvt24_ps24 = _batch_cvt24_ps24_cpu;
_batch_cvt_to_proc _batch_cvtps24_24 = _batch_cvtps24_24_cpu;
_batch_resample_float_proc _batch_resample_float = _batch_resample_float_cpu;
#else
_batch_resample_proc _batch_resample = _batch_resample_cpu;
#endif


/* -------------------------------------------------------------------------- */

void *
_aax_aligned_alloc16(size_t size)
{
   void *rv = NULL;

   size = SIZETO16(size);

#if __MINGW32__
   rv = _mm_malloc(size, MEMALIGN);
#elif ISOC11_SOURCE 
   rv = aligned_alloc(MEMALIGN, size);
#elif  _POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600
   if (posix_memalign(&rv, MEMALIGN, size) != 0) {
      rv = NULL;
   }
#elif _MSC_VER
   rv = _aligned_malloc(size, MEMALIGN);
#else
   assert(1 == 0);
#endif
   return rv;
}

#if defined(__MINGW32__)
void _xmm_free(void *ptr) { _mm_free(ptr); }
_aax_aligned_free_proc _aax_aligned_free = (_aax_aligned_free_proc)_xmm_free;
#elif ISOC11_SOURCE 
_aax_aligned_free_proc _aax_aligned_free = (_aax_aligned_free_proc)free;
#elif  _POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600
_aax_aligned_free_proc _aax_aligned_free = (_aax_aligned_free_proc)free;
#elif _MSC_VER
_aax_aligned_free_proc _aax_aligned_free= (_aax_aligned_free_proc)_aligned_free;
#else
# pragma warnig _aax_aligned_alloc16 needs implementing
_aax_aligned_free_proc _aax_aligned_free = (_aax_aligned_free_proc)free;
#endif

char *
_aax_malloc_align16(char **start, size_t size)
{
   int ctr = 3;
   char *ptr;

   assert((size_t)*start < size);

   size = SIZETO16(size + MEMALIGN);
   do
   {
      ptr = (char *)(int64_t *)malloc(size);
      if (ptr)
      {
         char *s = ptr + (size_t)*start;
         size_t tmp;

         tmp = (size_t)s & MEMMASK;
         if (tmp)
         {
            tmp = MEMALIGN - tmp;
            s += tmp;
         }
         *start = s;
      }
   }
   while (!ptr && --ctr);

   if (!ptr) {
      _AAX_SYSLOG("Unable to allocate enough memory, giving up after 3 tries");
   }

   return ptr;
}

char *
_aax_calloc_align16(char **start, size_t num, size_t size)
{
   int ctr = 3;
   char *ptr;

   assert((size_t)*start < num*size);

   size = SIZETO16(size + MEMALIGN);
   do
   {
      ptr = (char *)malloc(num*size);
      if (ptr)
      {
         char *s = ptr + (size_t)*start;
         size_t tmp;

         tmp = (size_t)s & MEMMASK;
         if (tmp)
         {
            tmp = MEMALIGN - tmp;
            s += tmp;
         }
         *start = s;

         memset(ptr, 0, num*size);
      }
   }
   while (!ptr && --ctr);

   if (!ptr) {
      _AAX_SYSLOG("Unable to allocate enough memory, giving up after 3 tries");
   }

   return ptr;
}

void
_aax_free_align16(void *ptr)
{
   free(ptr);
}

char *
_aax_strdup(const_char_ptr s)
{
   char *ret = 0;
   if (s) {
      size_t len = strlen(s);
      ret = malloc(len+1);
      if (ret) {
         memcpy(ret, s, len+1);
      }
   }
   return ret;
}


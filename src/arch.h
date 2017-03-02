/*
 * Copyright 2007-2017 by Erik Hofman.
 * Copyright 2009-2017 by Adalin B.V.
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Adalin B.V.;
 * the contents of this file may not be disclosed to third parties, copied or
 * duplicated in any form, in whole or in part, without the prior written
 * permission of Adalin B.V.
 */

#ifndef _AAX_ARCH_SUPPORT
#define _AAX_ARCH_SUPPORT 1

#if defined(__cplusplus)
extern "C" {
#endif

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <base/types.h>

#ifdef _MSC_VER
# define ASM    __asm
#else
# define ASM    __asm __volatile
#endif

#define SIZE_ALIGNED(a)	((a) & MEMMASK) ? ((a)|MEMMASK)+1 : (a)
#if RB_FLOAT_DATA
# define SIMD_PREFIX	"FP "
#else
# define SIMD_PREFIX
#endif

typedef void* (*_aax_memcpy_proc)(void_ptr, const void*, size_t);
typedef char* (*_aax_calloc_proc)(char**, size_t, size_t);
typedef char* (*_aax_malloc_proc)(char**, size_t);
typedef void (*_aax_free_proc)(void*);

typedef void (*_batch_cvt_proc)(void*, size_t);
typedef void (*_batch_cvt_from_proc)(void_ptr, const_void_ptr, size_t);
typedef void (*_batch_cvt_from_intl_proc)(int32_ptrptr, const_void_ptr, size_t, unsigned int, size_t);
typedef void (*_batch_cvt_to_proc)(void_ptr, const_void_ptr, size_t);
typedef void (*_batch_cvt_to_intl_proc)(void_ptr, const_int32_ptrptr, size_t, unsigned int, size_t);
typedef void (*_batch_codec_proc)(void_ptr, const_void_ptr, size_t);


typedef void (*_batch_fmadd_proc)(float32_ptr, const_float32_ptr, size_t, float, float);
typedef void (*_batch_imadd_proc)(int32_ptr, const_int32_ptr, size_t, float, float);
typedef void (*_batch_mul_value_proc)(void*, unsigned, size_t, float);
typedef void (*_batch_ema_proc)(int32_ptr, const_int32_ptr, size_t, float*, float);
typedef void (*_batch_freqfilter_proc)(int32_ptr, const_int32_ptr, int, size_t, void*);
typedef void (*_batch_ema_float_proc)(float32_ptr, const_float32_ptr, size_t, float*, float);
typedef void (*_batch_freqfilter_float_proc)(float32_ptr, const_float32_ptr, int, size_t, void*);
typedef void (*_batch_resample_float_proc)(float32_ptr, const_float32_ptr, size_t, size_t, float, float);
typedef void (*_batch_resample_proc)(int32_ptr, const_int32_ptr, size_t, size_t, float, float);


typedef void (*_aax_aligned_free_proc)(void*);
void* _aax_aligned_alloc(size_t);
extern _aax_aligned_free_proc _aax_aligned_free;

extern _aax_calloc_proc _aax_calloc;
extern _aax_malloc_proc _aax_malloc;
extern _aax_free_proc _aax_free;
extern _aax_memcpy_proc _aax_memcpy;
extern _aax_memcpy_proc _batch_cvt24_24;
extern char* _aax_strdup(const_char_ptr);

extern _batch_mul_value_proc _batch_imul_value;
extern _batch_mul_value_proc _batch_fmul_value;
extern _batch_imadd_proc _batch_imadd;
extern _batch_fmadd_proc _batch_fmadd;
extern _batch_fmadd_proc _batch_fma3;
extern _batch_fmadd_proc _batch_fma4;
extern _batch_ema_proc _batch_movingaverage;
extern _batch_freqfilter_proc _batch_freqfilter;
extern _batch_ema_float_proc _batch_movingaverage_float;
extern _batch_freqfilter_float_proc _batch_freqfilter_float;
extern _batch_resample_proc _batch_resample;
extern _batch_resample_float_proc _batch_resample_float;

extern _batch_cvt_proc _batch_saturate24;

extern _batch_cvt_proc _batch_cvt8u_8s;
extern _batch_cvt_proc _batch_cvt8s_8u;
extern _batch_cvt_proc _batch_cvt16u_16s;
extern _batch_cvt_proc _batch_cvt16s_16u;
extern _batch_cvt_proc _batch_cvt24u_24s;
extern _batch_cvt_proc _batch_cvt24s_24u;
extern _batch_cvt_proc _batch_cvt32u_32s;
extern _batch_cvt_proc _batch_cvt32s_32u;

extern _batch_cvt_proc _batch_endianswap16;
extern _batch_cvt_proc _batch_endianswap32;
extern _batch_cvt_proc _batch_endianswap64;

extern _batch_cvt_from_proc _batch_cvt24_8;
extern _batch_cvt_from_proc _batch_cvt24_16;
extern _batch_cvt_from_proc _batch_cvt24_24_3;
extern _batch_cvt_from_proc _batch_cvt24_32;
extern _batch_cvt_from_proc _batch_cvt24_ps;
extern _batch_cvt_from_proc _batch_cvt24_ps24;
extern _batch_cvt_from_proc _batch_cvt24_pd;
extern _batch_cvt_from_intl_proc _batch_cvt24_8_intl;
extern _batch_cvt_from_intl_proc _batch_cvt24_16_intl;
extern _batch_cvt_from_intl_proc _batch_cvt24_24_3intl;
extern _batch_cvt_from_intl_proc _batch_cvt24_24_intl;
extern _batch_cvt_from_intl_proc _batch_cvt24_32_intl;
extern _batch_cvt_from_intl_proc _batch_cvt24_ps_intl;
extern _batch_cvt_from_intl_proc _batch_cvt24_pd_intl;

extern _batch_cvt_to_proc _batch_cvt8_24;
extern _batch_cvt_to_proc _batch_cvt16_24;
extern _batch_cvt_to_proc _batch_cvt24_3_24;
extern _batch_cvt_to_proc _batch_cvt32_24;
extern _batch_cvt_to_proc _batch_cvtps_24;
extern _batch_cvt_to_proc _batch_cvtps24_24;
extern _batch_cvt_to_proc _batch_cvtpd_24;
extern _batch_cvt_to_intl_proc _batch_cvt8_intl_24;
extern _batch_cvt_to_intl_proc _batch_cvt16_intl_24;
extern _batch_cvt_to_intl_proc _batch_cvt24_3intl_24;
extern _batch_cvt_to_intl_proc _batch_cvt24_intl_24;
extern _batch_cvt_to_intl_proc _batch_cvt24_intl_ps;
extern _batch_cvt_to_intl_proc _batch_cvt32_intl_24;
extern _batch_cvt_to_intl_proc _batch_cvtps_intl_24;
extern _batch_cvt_to_intl_proc _batch_cvtpd_intl_24;

unsigned int _aaxGetNoCores();
const char* _aaxGetSIMDSupportString();

#if defined(__cplusplus)
}  /* extern "C" */
#endif

#endif /* !_AAX_ARCH_SUPPORT */


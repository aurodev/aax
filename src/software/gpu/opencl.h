/*
 * Copyright 2017 by Erik Hofman.
 * Copyright 2017 by Adalin B.V.
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

#ifndef _AAX_OPENCL_H
#define _AAX_OPENCL_H 1

#if defined(__cplusplus)
extern "C" {
#endif

#if HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef __APPLE__
# include "OpenCL/opencl.h"
#else
# include "CL/cl.h"
#endif

#include <ringbuffer.h>


typedef struct
{
   cl_context context;
   cl_command_queue queue;

} _aax_opencl_t;

_aax_opencl_t* _aaxOpenCLCreate(); 
void _aaxOpenCLDestroy(_aax_opencl_t*);

enum {
   SPTR = 0,
   CPTR,
   HCPTR
};

typedef struct {
   unsigned int snum, cnum, step;
   float v, threshold;
   float *ptr[3];
} _aax_convolution_t;

void _aaxOpenCLRunConvolution(_aax_opencl_t*, _aaxRingBufferConvolutionData*, float*, unsigned int, unsigned int);

#if defined(__cplusplus)
}  /* extern "C" */
#endif

#endif /* !_AAX_OPENCL_H */


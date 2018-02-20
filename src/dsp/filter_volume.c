/*
 * Copyright 2007-2018 by Erik Hofman.
 * Copyright 2009-2018 by Adalin B.V.
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

#include <assert.h>
#ifdef HAVE_RMALLOC_H
# include <rmalloc.h>
#else
# include <stdlib.h>
# include <malloc.h>
#endif

#include <aax/aax.h>

#include <base/types.h>		/* for rintf */
#include <base/gmath.h>

#include <software/rbuf_int.h>
#include "common.h"
#include "filters.h"
#include "effects.h"
#include "arch.h"
#include "api.h"

void _occlusion_prepare(_aaxEmitter*, _aax3dProps*, float);
void _occlusion_run(void*, MIX_PTR_T, CONST_MIX_PTR_T, MIX_PTR_T, size_t, unsigned int, const void*);
void _freqfilter_run(void*, MIX_PTR_T, CONST_MIX_PTR_T, size_t, size_t, size_t, unsigned int, void*, void*, unsigned char);

static aaxFilter
_aaxVolumeFilterCreate(_aaxMixerInfo *info, enum aaxFilterType type)
{
   _filter_t* flt = _aaxFilterCreateHandle(info, type, 2);
   aaxFilter rv = NULL;

   if (flt)
   {
      _aaxSetDefaultFilter3d(flt->slot[0], flt->pos, 0);
      _aaxSetDefaultFilter3d(flt->slot[1], flt->pos, 1);
      flt->slot[0]->destroy = aligned_destroy;
      rv = (aaxFilter)flt;
   }
   return rv;
}

static int
_aaxVolumeFilterDestroy(_filter_t* filter)
{
   filter->slot[0]->destroy(filter->slot[0]->data);
   free(filter);

   return AAX_TRUE;
}

static aaxFilter
_aaxVolumeFilterSetState(_filter_t* filter, int state)
{
   _aaxRingBufferOcclusionData *occlusion = filter->slot[0]->data;

   if (state != AAX_FALSE &&
       filter->slot[1]->param[0] > 0.1f &&
       filter->slot[1]->param[1] > 0.1f &&
       filter->slot[1]->param[2] > 0.1f &&
       filter->slot[1]->param[3] > LEVEL_64DB)
   {

      if (!occlusion)
      {
         occlusion = _aax_aligned_alloc(sizeof(_aaxRingBufferOcclusionData));
         filter->slot[0]->data = occlusion;
      }

      if (occlusion)
      {
         float  fs = 48000.0f;

         if (filter->info) {
            fs = filter->info->frequency;
         }

         occlusion->prepare = _occlusion_prepare;
         occlusion->run = _occlusion_run;

         occlusion->occlusion.v4[0] = 0.5f*filter->slot[1]->param[0];
         occlusion->occlusion.v4[1] = 0.5f*filter->slot[1]->param[1];
         occlusion->occlusion.v4[2] = 0.5f*filter->slot[1]->param[2];
         occlusion->occlusion.v4[3] = filter->slot[1]->param[3];
         occlusion->magnitude_sq = vec3fMagnitudeSquared(&occlusion->occlusion.v3);
         occlusion->fc = 22000.0f;

         occlusion->level = 1.0f;
         occlusion->olevel = 0.0f;
         occlusion->inverse = (state & AAX_INVERSE) ? AAX_TRUE : AAX_FALSE;

         memset(&occlusion->freq_filter, 0, sizeof(_aaxRingBufferFreqFilterData));
         occlusion->freq_filter.run = _freqfilter_run;
         occlusion->freq_filter.lfo = 0;
         occlusion->freq_filter.fs = fs;
         occlusion->freq_filter.Q = 0.6f;
         occlusion->freq_filter.low_gain = 1.0f;

         // 6db/Oct low-pass Bessel filter
         occlusion->freq_filter.type = LOWPASS;
         occlusion->freq_filter.no_stages = 0;
         occlusion->freq_filter.state = AAX_FALSE;
         _aax_bessel_compute(occlusion->fc, &occlusion->freq_filter);
      }
   }
   else
   {
      filter->slot[0]->destroy(filter->slot[0]->data);
      filter->slot[0]->data = NULL;
   }

   return filter;
}

static _filter_t*
_aaxNewVolumeFilterHandle(const aaxConfig config, enum aaxFilterType type, UNUSED(_aax2dProps* p2d), _aax3dProps* p3d)
{
   _handle_t *handle = get_driver_handle(config);
   _aaxMixerInfo* info = handle ? handle->info : _info;
   _filter_t* rv = _aaxFilterCreateHandle(info, type, 2);

   if (rv)
   {
      unsigned int size = sizeof(_aaxFilterInfo);

      memcpy(rv->slot[0], &p2d->filter[rv->pos], size);
      memcpy(rv->slot[1], &p3d->filter[rv->pos], size);
      rv->slot[0]->destroy = aligned_destroy;
      rv->slot[0]->data = NULL;

      rv->state = p3d->filter[rv->pos].state;
   }
   return rv;
}

static float
_aaxVolumeFilterSet(float val, int ptype, UNUSED(unsigned char param))
{
   float rv = val;
   if (ptype == AAX_LOGARITHMIC) {
      rv = _lin2db(val);
   }
   return rv;
}

static float
_aaxVolumeFilterGet(float val, int ptype, UNUSED(unsigned char param))
{
   float rv = val;
   if (param < 3 && ptype == AAX_LOGARITHMIC) {
      rv = _db2lin(val);
   }
   return rv;
}

static float
_aaxVolumeFilterMinMax(float val, int slot, unsigned char param)
{
  static const _flt_minmax_tbl_t _aaxVolumeRange[_MAX_FE_SLOTS] =
   {    /* min[4] */                  /* max[4] */
    { { 0.0f, 0.0f, 0.0f, 0.0f }, {   10.0f,    1.0f,    10.0f, 10.0f } },
    { { 0.0f, 0.0f, 0.0f, 0.0f }, { FLT_MAX, FLT_MAX,  FLT_MAX,  1.0f } }, 
    { { 0.0f, 0.0f, 0.0f, 0.0f }, {    0.0f,    0.0f,     0.0f,  0.0f } },
    { { 0.0f, 0.0f, 0.0f, 0.0f }, {    0.0f,    0.0f,     0.0f,  0.0f } }
   };
   
   assert(slot < _MAX_FE_SLOTS);
   assert(param < 4);
   
   return _MINMAX(val, _aaxVolumeRange[slot].min[param],
                       _aaxVolumeRange[slot].max[param]);
}

/* -------------------------------------------------------------------------- */

_flt_function_tbl _aaxVolumeFilter =
{
   AAX_TRUE,
   "AAX_volume_filter_1.1", 1.1f,
   (_aaxFilterCreate*)&_aaxVolumeFilterCreate,
   (_aaxFilterDestroy*)&_aaxVolumeFilterDestroy,
   (_aaxFilterSetState*)&_aaxVolumeFilterSetState,
   (_aaxNewFilterHandle*)&_aaxNewVolumeFilterHandle,
   (_aaxFilterConvert*)&_aaxVolumeFilterSet,
   (_aaxFilterConvert*)&_aaxVolumeFilterGet,
   (_aaxFilterConvert*)&_aaxVolumeFilterMinMax
};


#ifdef ARCH32
# define FLOAT			float
# define VEC3_T			vec3f_t
# define VEC3SUB(a,b,c)		vec3fSub(a,b,c)
# define VEC3ADD(a,b,c)		vec3fAdd(a,b,c)
# define VEC3NEGATE(a,b)	vec3fNegate(a,b)
# define VEC3NORMALIZE(a,b)	vec3fNormalize(a,b)
# define VEC3DOTPRODUCT(a,b)	vec3fDotProduct(a,b)
# define VEC3SCALARMUL(a,b,c)	vec3fScalarMul(a,b,c)
# define VEC3SUBFILL(a,b,c,d)	vec3fSub(&a,c,d)
#else
# define FLOAT			double
# define VEC3_T			vec3d_t
# define VEC3SUB(a,b,c)		vec3dSub(a,b,c)
# define VEC3ADD(a,b,c)		vec3dAdd(a,b,c)
# define VEC3NEGATE(a,b)	vec3dNegate(a,b)
# define VEC3NORMALIZE(a,b)	vec3dNormalize(a,b)
# define VEC3DOTPRODUCT(a,b)	vec3dDotProduct(a,b)
# define VEC3SCALARMUL(a,b,c)	vec3dScalarMul(a,b,c)
# define VEC3SUBFILL(a,b,c,d)	vec3dSub(&b,c,d); vec3fFilld(a.v3,b.v3)
#endif

void
_occlusion_prepare(_aaxEmitter *src, _aax3dProps *fp3d, float vs)
{
   _aaxRingBufferOcclusionData *occlusion;
   _aaxDelayed3dProps *pdp3d_m, *fdp3d_m;
   _aaxDelayed3dProps *edp3d_m;
   _aax3dProps *ep3d;

   ep3d = src->props3d;
   edp3d_m = ep3d->m_dprops3d;
   fdp3d_m = fp3d->m_dprops3d;
   pdp3d_m = fp3d->parent ? fp3d->parent->m_dprops3d : NULL;
   
   if (pdp3d_m)
   {
      occlusion = _EFFECT_GET_DATA(fp3d, REVERB_EFFECT);
      if (!occlusion) occlusion = _FILTER_GET_DATA(fp3d, VOLUME_FILTER);
      if (occlusion)
      {
         VEC3_T fpepos, fpevec, pevec, fevec;
         vec3f_t vres;
         _aaxRingBufferOcclusionData *path = occlusion;
         _aaxDelayed3dProps *ndp3d_m;
         _aax3dProps *nfp3d;
         FLOAT mag_pev;
         int less;

         nfp3d = fp3d->parent;
         assert(nfp3d->m_dprops3d == pdp3d_m);

         // Calculate the frame-to-emitter vector.
         // fdp3d_m specifies the absolute position of the frame
         // where, by definition, the sound obstruction is located.
         VEC3SUB(&fevec, &fdp3d_m->matrix.v34[LOCATION],
                         &edp3d_m->matrix.v34[LOCATION]);
         less = 0;
         occlusion->level = 0.0f;
         do
         {
            if (path)
            {
               ndp3d_m = nfp3d->m_dprops3d;

               // Calculate the parent_frame-to-emitter unit vector.
               // ndp3d_m specifies the absolute position of the parent.
               // edp3d_m specifies the absolute position of the emitter.

               if (nfp3d == nfp3d->root) {
                  VEC3NEGATE(&pevec, &edp3d_m->matrix.v34[LOCATION]);
               }
               else
               {
                  VEC3SUB(&pevec, &ndp3d_m->matrix.v34[LOCATION],
                                  &edp3d_m->matrix.v34[LOCATION]);
               }
               VEC3NORMALIZE(&pevec, &pevec);

               // Get the projection length of the frame-to-emitter vector on
               // the parent_frame-to-emitter unit vector..
               mag_pev = VEC3DOTPRODUCT(&fevec, &pevec);

               // scale the parent_frame-to-emitter unit vector.
               VEC3SCALARMUL(&fpevec, &pevec, mag_pev);

               // Get the vector from the frame position which perpendicular
               // to the parent_frame-to-emitter vector.
               VEC3ADD(&fpepos, &edp3d_m->matrix.v34[LOCATION], &fpevec);
               VEC3SUBFILL(vres, fpevec, &fdp3d_m->matrix.v34[LOCATION], &fpepos);
               vec3fAbsolute(&vres, &vres);

               // Less is true means the direct path intersects with an
               // obstrruction.
               // In case path->inverse is true the real obstruction is
               // everything but the defined dimensions (meaning a hole).
               less = vec3fLessThan(&vres, &path->occlusion.v3);
               if (path->inverse) less = !less;
               if (less)
               {
                  float level, mag;

                  mag = path->magnitude_sq * 100.0f*100.0f/(vs*vs);
                  level = 1.0f - _MIN(vec3fMagnitudeSquared(&vres)/mag, 1.0f);

                  if (path->inverse) level = 1.0f / level;

                  level *= path->occlusion.v4[3];    // density
                  if (level > occlusion->level) {
                     occlusion->level = level;
                  }
#if 1
                  if (occlusion->level > (1.0f-LEVEL_64DB)) break;
               }
            }
#else
               }
 printf("obstruction dimensions:\t");
 PRINT_VEC3(path->occlusion.v3);
 printf("  parent_frame-emitter:\t");
 PRINT_VEC3(vres);
 printf("       hit obstruction: %s, level: %f, inverse?: %i\n", (less^path->inverse)?"yes":"no ", occlusion->level, path->inverse);
               if (occlusion->level > (1.0f-LEVEL_64DB)) break;
            }
#endif
            path = _EFFECT_GET_DATA(nfp3d, REVERB_EFFECT);
            if (!path) path = _FILTER_GET_DATA(nfp3d, VOLUME_FILTER);
            nfp3d = nfp3d->parent;
         }
         while (nfp3d);
      } /* occlusion != NULL */
   } /* pdp3d_m != NULL */
}

void
_occlusion_run(void *rb, MIX_PTR_T dptr, CONST_MIX_PTR_T sptr, MIX_PTR_T scratch, size_t no_samples, unsigned int track, const void *data)
{
   _aaxRingBufferOcclusionData *occlusion = (_aaxRingBufferOcclusionData*)data;
   _aaxRingBufferSample *rbd = (_aaxRingBufferSample*)rb;
   _aaxRingBufferFreqFilterData *freq_flt;

   /* add the direct path */
   assert(occlusion);

   freq_flt = &occlusion->freq_filter;

   // occlusion->level: 0.0 = free path, 1.0 = blocked
   if (occlusion->level != occlusion->olevel)
   {
      // level = 0.0f: 20kHz, level = 1.0f: 250Hz
      // log10(20000 - 1000) = 4.2787541
      occlusion->freq_filter.low_gain = 1.0f - occlusion->level;
      occlusion->fc = 20000.0f - _log2lin(4.278754f*occlusion->level);
      _aax_bessel_compute(occlusion->fc, freq_flt);

      occlusion->olevel = occlusion->level;
   }

   if (occlusion->freq_filter.low_gain > LEVEL_64DB)
   {
      if (occlusion->fc < 15000.0f)
      {
         freq_flt->run(rbd, scratch, sptr, 0, no_samples, 0, track, freq_flt,
                       NULL, 0);
         sptr = scratch;
      }
      rbd->add(dptr, sptr, no_samples, 1.0f, 0.0f);
   }
}

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

#include <assert.h>
#ifdef HAVE_RMALLOC_H
# include <rmalloc.h>
#else
# include <stdlib.h>
# include <malloc.h>
#endif

#include <aax/aax.h>

#include <base/types.h>		/*  for rintf */
#include <base/gmath.h>

#include <software/rbuf_int.h>
#include "effects.h"
#include "api.h"
#include "arch.h"

#define CHORUS_MIN	10e-3f
#define CHORUS_MAX	60e-3f

static void _chorus_destroy(void*);
static void _chorus_run(void*, MIX_PTR_T, CONST_MIX_PTR_T, MIX_PTR_T, size_t, size_t, size_t, size_t, void*, void*, unsigned int);

static aaxEffect
_aaxChorusEffectCreate(_aaxMixerInfo *info, enum aaxEffectType type)
{
   _effect_t* eff = _aaxEffectCreateHandle(info, type, 1);
   aaxEffect rv = NULL;

   if (eff)
   {
      _aaxSetDefaultEffect2d(eff->slot[0], eff->pos);
      eff->slot[0]->destroy = _chorus_destroy;
      rv = (aaxEffect)eff;
   }
   return rv;
}

static int
_aaxChorusEffectDestroy(_effect_t* effect)
{
   effect->slot[0]->destroy(effect->slot[0]->data);
   effect->slot[0]->data = NULL;
   free(effect);

   return AAX_TRUE;
}

static aaxEffect
_aaxChorusEffectSetState(_effect_t* effect, int state)
{
   void *handle = effect->handle;
   aaxEffect rv = AAX_FALSE;

   assert(effect->info);

   effect->state = state;
   switch (state & ~AAX_INVERSE)
   {
   case AAX_CONSTANT_VALUE:
   case AAX_TRIANGLE_WAVE:
   case AAX_SINE_WAVE:
   case AAX_SQUARE_WAVE:
   case AAX_SAWTOOTH_WAVE:
   case AAX_ENVELOPE_FOLLOW:
   {
      _aaxRingBufferDelayEffectData* data = effect->slot[0]->data;
      if (data == NULL)
      {
         int t;

         data  = malloc(sizeof(_aaxRingBufferDelayEffectData));
         effect->slot[0]->data = data;
         if (data)
         {
            data->history_ptr = 0;
            for (t=0; t<_AAX_MAX_SPEAKERS; t++)
            {
               data->lfo.value[t] = 0.0f;
               data->lfo.step[t] = 0.0f;
            }
         }
      }

      if (data)
      {
         int t, constant;

         data->run = _chorus_run;
         data->loopback = AAX_FALSE;

         if (data->history_ptr)
         {
            free(data->history_ptr);
            data->history_ptr = 0;
         }

         data->lfo.convert = _linear;
         data->lfo.state = effect->state;
         data->lfo.fs = effect->info->frequency;
         data->lfo.period_rate = effect->info->period_rate;

         data->lfo.min_sec = CHORUS_MIN;
         data->lfo.max_sec = CHORUS_MAX;
         data->lfo.depth = effect->slot[0]->param[AAX_LFO_DEPTH];
         data->lfo.offset = effect->slot[0]->param[AAX_LFO_OFFSET];
         data->lfo.f = effect->slot[0]->param[AAX_LFO_FREQUENCY];
         data->lfo.inv = (state & AAX_INVERSE) ? AAX_TRUE : AAX_FALSE;
         data->lfo.stereo_lnk = AAX_TRUE;

         if ((data->lfo.offset + data->lfo.depth) > 1.0f) {
            data->lfo.depth = 1.0f - data->lfo.offset;
         }

         constant = _lfo_set_timing(&data->lfo);

         data->delay.gain = effect->slot[0]->param[AAX_DELAY_GAIN];
         for (t=0; t<_AAX_MAX_SPEAKERS; t++) {
            data->delay.sample_offs[t] = (size_t)data->lfo.value[t];
         }

         if (!_lfo_set_function(&data->lfo, constant)) {
            _aaxErrorSet(AAX_INVALID_PARAMETER);
         }
      }
      else _aaxErrorSet(AAX_INSUFFICIENT_RESOURCES);
      break;
   }
   case AAX_FALSE:
   {
      effect->slot[0]->destroy(effect->slot[0]->data);
      effect->slot[0]->data = NULL;
      break;
   }
   default:
      _aaxErrorSet(AAX_INVALID_PARAMETER);
      break;
   }
   rv = effect;
   return rv;
}

static _effect_t*
_aaxNewChorusEffectHandle(const aaxConfig config, enum aaxEffectType type, _aax2dProps* p2d, UNUSED(_aax3dProps* p3d))
{
   _handle_t *handle = get_driver_handle(config);
   _aaxMixerInfo* info = handle ? handle->info : _info;
   _effect_t* rv = _aaxEffectCreateHandle(info, type, 1);

   if (rv)
   {
      unsigned int size = sizeof(_aaxEffectInfo);

      memcpy(rv->slot[0], &p2d->effect[rv->pos], size);
      rv->slot[0]->destroy = _chorus_destroy;
      rv->slot[0]->data = NULL;

      rv->state = p2d->effect[rv->pos].state;
   }
   return rv;
}

static float
_aaxChorusEffectSet(float val, int ptype, unsigned char param)
{  
   float rv = val;
   if ((param == 0) && (ptype == AAX_LOGARITHMIC)) {
      rv = _lin2db(val);
   }
   return rv;
}
   
static float
_aaxChorusEffectGet(float val, int ptype, unsigned char param)
{  
   float rv = val;
   if ((param == 0) && (ptype == AAX_LOGARITHMIC)) {
      rv = _db2lin(val);
   }
   return rv;
}

static float
_aaxChorusEffectMinMax(float val, int slot, unsigned char param)
{
   static const _eff_minmax_tbl_t _aaxChorusRange[_MAX_FE_SLOTS] =
   {    /* min[4] */                  /* max[4] */
    { { 0.0f, 0.01f, 0.0f, 0.0f }, { 1.0f, 10.0f, 1.0f, 1.0f } },
    { { 0.0f, 0.0f,  0.0f, 0.0f }, { 0.0f,  0.0f, 0.0f, 0.0f } },
    { { 0.0f, 0.0f,  0.0f, 0.0f }, { 0.0f,  0.0f, 0.0f, 0.0f } }
   };

   assert(slot < _MAX_FE_SLOTS);
   assert(param < 4);

   return _MINMAX(val, _aaxChorusRange[slot].min[param],
                       _aaxChorusRange[slot].max[param]);
}

/* -------------------------------------------------------------------------- */

_eff_function_tbl _aaxChorusEffect =
{
   AAX_FALSE,
   "AAX_chorus_effect", 1.0f,
   (_aaxEffectCreate*)&_aaxChorusEffectCreate,
   (_aaxEffectDestroy*)&_aaxChorusEffectDestroy,
   (_aaxEffectSetState*)&_aaxChorusEffectSetState,
   NULL,
   (_aaxNewEffectHandle*)&_aaxNewChorusEffectHandle,
   (_aaxEffectConvert*)&_aaxChorusEffectSet,
   (_aaxEffectConvert*)&_aaxChorusEffectGet,
   (_aaxEffectConvert*)&_aaxChorusEffectMinMax
};

static void
_chorus_destroy(void *ptr)
{
   _aaxRingBufferDelayEffectData *data = ptr;
   if (data)
   {
      data->lfo.envelope = AAX_FALSE;
      free(data);
   }
}

/**
 * - d and s point to a buffer containing the delay effects buffer prior to
 *   the pointer.
 * - start is the starting pointer
 * - end is the end pointer (end-start is the number of samples)
 * - no_samples is the number of samples to process this run
 * - dmax does not include ds
 */
static void
_chorus_run(void *rb, MIX_PTR_T d, CONST_MIX_PTR_T s, MIX_PTR_T scratch,
             size_t start, size_t end, size_t no_samples, size_t ds,
             void *data, void *env, unsigned int track)
{
   _aaxRingBufferSample *rbd = (_aaxRingBufferSample*)rb;
   _aaxRingBufferDelayEffectData* effect = data;
   size_t offs, noffs;
   MIX_T *sptr, *dptr;
   float pitch, volume;
   ssize_t doffs;

   _AAX_LOG(LOG_DEBUG, __func__);

   assert(s != 0);
   assert(d != 0);
   assert(start < end);
   assert(data != NULL);

   sptr = (MIX_T*)s + start;
   dptr = d + start;

   volume =  effect->delay.gain;
   offs = effect->delay.sample_offs[track];

   assert(start || (offs < ds));
   if (offs >= ds) offs = ds-1;

   if (start) {
      noffs = effect->curr_noffs[track];
   }
   else
   {
      noffs = (size_t)effect->lfo.get(&effect->lfo, env, s, track, end);
      effect->delay.sample_offs[track] = noffs;
      effect->curr_noffs[track] = noffs;
   }

   assert(s != d);

   doffs = noffs - offs;
   pitch = _MAX(((float)end-(float)doffs)/(float)(end), 0.001f);
   if (pitch == 1.0f) {
      rbd->add(dptr, sptr-offs, no_samples, volume, 0.0f);
   }
   else
   {
      DBG_MEMCLR(1, scratch-ds, ds+end, sizeof(MIX_T));
      rbd->resample(scratch-ds, sptr-offs, 0, no_samples, 0.0f, pitch);
      rbd->add(dptr, scratch-ds, no_samples, volume, 0.0f);
   }
}

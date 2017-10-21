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

#include "effects.h"
#include "api.h"
#include "arch.h"

static void _flanging_destroy(void*);

static aaxEffect
_aaxFlangingEffectCreate(_aaxMixerInfo *info, enum aaxEffectType type)
{
   _effect_t* eff = _aaxEffectCreateHandle(info, type, 1);
   aaxEffect rv = NULL;

   if (eff)
   {
      _aaxSetDefaultEffect2d(eff->slot[0], eff->pos);
      eff->slot[0]->destroy = _flanging_destroy;
      rv = (aaxEffect)eff;
   }
   return rv;
}

static int
_aaxFlangingEffectDestroy(_effect_t* effect)
{
   effect->slot[0]->destroy(effect->slot[0]->data);
   effect->slot[0]->data = NULL;
   free(effect);

   return AAX_TRUE;
}

static aaxEffect
_aaxFlangingEffectSetState(_effect_t* effect, int state)
{
   void *handle = effect->handle;
   aaxEffect rv = AAX_FALSE;

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
         data->history_ptr = 0;
         for (t=0; t<_AAX_MAX_SPEAKERS; t++)
         {
            data->lfo.value[t] = 0.0f;
            data->lfo.step[t] = 0.0f;
         }
      }

      if (data)
      {
         float depth = effect->slot[0]->param[AAX_LFO_DEPTH];
         float offset = effect->slot[0]->param[AAX_LFO_OFFSET];
         unsigned int tracks = effect->info->no_tracks;
         float sign, range, step;
         float fs = 48000.0f;
         size_t samples;

         if (effect->info) {
            fs = effect->info->frequency;
         }

         if ((offset + depth) > 1.0f) {
            depth = 1.0f - offset;
         }

         // AAX_FLANGING_EFFECT
         range = (60e-3f - 10e-3f);		// 10ms .. 60ms
         depth *= range * fs;		// convert to samples
         data->lfo.min = (range * offset + 10e-3f)*fs;
         data->loopback = AAX_TRUE;
         samples = TIME_TO_SAMPLES(fs, DELAY_EFFECTS_TIME);
         _aaxRingBufferCreateHistoryBuffer(&data->history_ptr,
                                           data->delay_history,
                                           samples, tracks);
         // AAX_FLANGING_EFFECT

         data->lfo.convert = _linear;
         data->lfo.max = data->lfo.min + depth;
         data->lfo.f = effect->slot[0]->param[AAX_LFO_FREQUENCY];
         data->delay.gain = effect->slot[0]->param[AAX_DELAY_GAIN];
         data->lfo.inv = (state & AAX_INVERSE) ? AAX_TRUE : AAX_FALSE;

         if (depth > 0.05f)
         {
            int t;
            for (t=0; t<_AAX_MAX_SPEAKERS; t++)
            {
               // slowly work towards the new settings
               step = data->lfo.step[t];
               sign = step ? (step/fabsf(step)) : 1.0f;

               data->lfo.step[t] = 2.0f*sign * data->lfo.f;
               data->lfo.step[t] *= (data->lfo.max - data->lfo.min);
               data->lfo.step[t] /= effect->info->period_rate;

               if ((data->lfo.value[t] == 0)
                   || (data->lfo.value[t] < data->lfo.min)) {
                  data->lfo.value[t] = data->lfo.min;
               } else if (data->lfo.value[t] > data->lfo.max) {
                  data->lfo.value[t] = data->lfo.max;
               }
               data->delay.sample_offs[t] = (size_t)data->lfo.value[t];

               switch (state & ~AAX_INVERSE)
               {
               case AAX_SAWTOOTH_WAVE:
                  data->lfo.step[t] *= 0.5f;
                  break;
               case AAX_ENVELOPE_FOLLOW:
               {
                  float fact = data->lfo.f;
                  data->lfo.value[t] /= data->lfo.max;
                  data->lfo.step[t] = ENVELOPE_FOLLOW_STEP_CVT(fact);
                  break;
               }
               default:
                  break;
               }
            }

            switch (state & ~AAX_INVERSE)
            {
            case AAX_CONSTANT_VALUE: /* equals to AAX_TRUE */
               data->lfo.get = _aaxRingBufferLFOGetFixedValue;
               break;
            case AAX_TRIANGLE_WAVE:
               data->lfo.get = _aaxRingBufferLFOGetTriangle;
               break;
            case AAX_SINE_WAVE:
               data->lfo.get = _aaxRingBufferLFOGetSine;
               break;
            case AAX_SQUARE_WAVE:
               data->lfo.get = _aaxRingBufferLFOGetSquare;
               break;
            case AAX_SAWTOOTH_WAVE:
               data->lfo.get = _aaxRingBufferLFOGetSawtooth;
               break;
            case AAX_ENVELOPE_FOLLOW:
                data->lfo.get = _aaxRingBufferLFOGetGainFollow;
                data->lfo.envelope = AAX_TRUE;
               break;
            default:
               _aaxErrorSet(AAX_INVALID_PARAMETER);
               break;
            }
         }
         else
         {
            int t;
            for (t=0; t<_AAX_MAX_SPEAKERS; t++)
            {
               data->lfo.value[t] = data->lfo.min;
               data->delay.sample_offs[t] = (size_t)data->lfo.value[t];
            }
            data->lfo.get = _aaxRingBufferLFOGetFixedValue;
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
_aaxNewFlangingEffectHandle(const aaxConfig config, enum aaxEffectType type, _aax2dProps* p2d, UNUSED(_aax3dProps* p3d))
{
   _handle_t *handle = get_driver_handle(config);
   _aaxMixerInfo* info = handle ? handle->info : _info;
   _effect_t* rv = _aaxEffectCreateHandle(info, type, 1);

   if (rv)
   {
      unsigned int size = sizeof(_aaxEffectInfo);

      memcpy(rv->slot[0], &p2d->effect[rv->pos], size);
      rv->slot[0]->destroy = _flanging_destroy;
      rv->slot[0]->data = NULL;

      rv->state = p2d->effect[rv->pos].state;
   }
   return rv;
}

static float
_aaxFlangingEffectSet(float val, int ptype, unsigned char param)
{  
   float rv = val;
   if ((param == 0) && (ptype == AAX_LOGARITHMIC)) {
      rv = _lin2db(val);
   }
   return rv;
}
   
static float
_aaxFlangingEffectGet(float val, int ptype, unsigned char param)
{  
   float rv = val;
   if ((param == 0) && (ptype == AAX_LOGARITHMIC)) {
      rv = _db2lin(val);
   }
   return rv;
}

static float
_aaxFlangingEffectMinMax(float val, int slot, unsigned char param)
{
   static const _eff_minmax_tbl_t _aaxFlangingRange[_MAX_FE_SLOTS] =
   {    /* min[4] */                  /* max[4] */
    { { 0.0f, 0.01f, 0.0f, 0.0f }, { 1.0f, 10.0f, 1.0f, 1.0f } },
    { { 0.0f, 0.0f,  0.0f, 0.0f }, { 0.0f,  0.0f, 0.0f, 0.0f } },
    { { 0.0f, 0.0f,  0.0f, 0.0f }, { 0.0f,  0.0f, 0.0f, 0.0f } }
   };
   
   assert(slot < _MAX_FE_SLOTS);
   assert(param < 4);
   
   return _MINMAX(val, _aaxFlangingRange[slot].min[param],
                       _aaxFlangingRange[slot].max[param]);
}

/* -------------------------------------------------------------------------- */

_eff_function_tbl _aaxFlangingEffect =
{
   AAX_FALSE,
   "AAX_flanging_effect", 1.0f,
   (_aaxEffectCreate*)&_aaxFlangingEffectCreate,
   (_aaxEffectDestroy*)&_aaxFlangingEffectDestroy,
   (_aaxEffectSetState*)&_aaxFlangingEffectSetState,
   NULL,
   (_aaxNewEffectHandle*)&_aaxNewFlangingEffectHandle,
   (_aaxEffectConvert*)&_aaxFlangingEffectSet,
   (_aaxEffectConvert*)&_aaxFlangingEffectGet,
   (_aaxEffectConvert*)&_aaxFlangingEffectMinMax
};

static void
_flanging_destroy(void *ptr)
{
   _aaxRingBufferDelayEffectData* data = ptr;
   if (data)
   {
      free(data->history_ptr);
      free(data);
   }
}


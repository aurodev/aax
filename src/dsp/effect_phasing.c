/*
 * Copyright 2007-2020 by Erik Hofman.
 * Copyright 2009-2020 by Adalin B.V.
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

#include <assert.h>

#include <aax/aax.h>

#include <base/types.h>		/*  for rintf */
#include <base/gmath.h>

#include <software/rbuf_int.h>
#include "effects.h"
#include "arch.h"
#include "dsp.h"
#include "api.h"

#define VERSION	1.02
#define DSIZE	sizeof(_aaxRingBufferDelayEffectData)

static aaxEffect
_aaxPhasingEffectCreate(_aaxMixerInfo *info, enum aaxEffectType type)
{
   _effect_t* eff = _aaxEffectCreateHandle(info, type, 2, DSIZE);
   aaxEffect rv = NULL;

   if (eff)
   {
      _aaxSetDefaultEffect2d(eff->slot[0], eff->pos, 0);
      eff->slot[0]->destroy = _delay_destroy;
      eff->slot[0]->swap = _delay_swap;
      rv = (aaxEffect)eff;
   }
   return rv;
}

static int
_aaxPhasingEffectDestroy(_effect_t* effect)
{
   if (effect->slot[0]->data)
   {
      effect->slot[0]->destroy(effect->slot[0]->data);
      effect->slot[0]->data = NULL;
   }
   free(effect);

   return AAX_TRUE;
}

static aaxEffect
_aaxPhasingEffectSetState(_effect_t* effect, int state)
{
   void *handle = effect->handle;
   aaxEffect rv = AAX_FALSE;
   int stereo;

   assert(effect->info);

   stereo = (state & AAX_LFO_STEREO) ? AAX_TRUE : AAX_FALSE;
   state &= ~AAX_LFO_STEREO;

   effect->state = state;
   switch (state & ~AAX_INVERSE)
   {
   case AAX_CONSTANT_VALUE:
   case AAX_TRIANGLE_WAVE:
   case AAX_SINE_WAVE:
   case AAX_SQUARE_WAVE:
   case AAX_IMPULSE_WAVE:
   case AAX_SAWTOOTH_WAVE:
   case AAX_RANDOMNESS:
   case AAX_TIMED_TRANSITION:
   case AAX_ENVELOPE_FOLLOW:
   case AAX_ENVELOPE_FOLLOW_LOG:
   {
      _aaxRingBufferDelayEffectData* data = effect->slot[0]->data;
      float feedback = effect->slot[1]->param[AAX_MAX_GAIN];
      char fbhist = feedback ? AAX_TRUE : AAX_FALSE;

      data = _delay_create(data, effect->info, AAX_TRUE, fbhist);
      effect->slot[0]->data = data;
      if (data)
      {
         _aaxRingBufferFreqFilterData *flt = data->freq_filter;
         float fc = effect->slot[1]->param[AAX_CUTOFF_FREQUENCY];
         float fmax = effect->slot[1]->param[AAX_LFO_FREQUENCY];
         float offset = effect->slot[0]->param[AAX_LFO_OFFSET];
         float depth = effect->slot[0]->param[AAX_LFO_DEPTH];
         float fs = 48000.0f;
         int t, constant;

         if (effect->info) {
            fs = effect->info->frequency;
         }

         if ((fc > MINIMUM_CUTOFF || fmax > MINIMUM_CUTOFF) && !flt)
         {
            flt = _aax_aligned_alloc(sizeof(_aaxRingBufferFreqFilterData));
            if (flt)
            {
               memset(flt, 0, sizeof(_aaxRingBufferFreqFilterData));
               flt->freqfilter = _aax_aligned_alloc(sizeof(_aaxRingBufferFreqFilterHistoryData));
               if (flt->freqfilter) {
                  memset(flt->freqfilter, 0, sizeof(_aaxRingBufferFreqFilterHistoryData));
               }
               else
               {
                  _aax_aligned_free(flt);
                  flt = NULL;
               }
            }
         }

         data->freq_filter = flt;
         data->prepare = _delay_prepare;
         data->run = _delay_run;
         data->flanger = AAX_FALSE;
         data->feedback = feedback;

         data->lfo.convert = _linear;
         data->lfo.state = effect->state;
         data->lfo.fs = fs;
         data->lfo.period_rate = effect->info->period_rate;

         data->lfo.min_sec = PHASING_MIN;
         data->lfo.max_sec = PHASING_MAX;
         data->lfo.depth = depth;
         data->lfo.offset = offset;
         data->lfo.f = effect->slot[0]->param[AAX_LFO_FREQUENCY];
         data->lfo.inv = (state & AAX_INVERSE) ? AAX_TRUE : AAX_FALSE;
         data->lfo.stereo_lnk = !stereo;

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
         else if (flt)
         {
            flt->run = _freqfilter_run;

            flt->fs = fs;
            flt->lfo = NULL;
            flt->no_stages = 0;
            flt->type = HIGHPASS;


            flt->low_gain = data->delay.gain;
            flt->high_gain = LEVEL_128DB;
            flt->Q = effect->slot[1]->param[AAX_RESONANCE];
            flt->k = flt->low_gain/flt->high_gain;

            _aax_butterworth_compute(fc, flt);

            if (data->lfo.f)
            {
               _aaxLFOData* lfo = flt->lfo;

               if (fmax > MINIMUM_CUTOFF)
               {
                  if (lfo == NULL) {
                     lfo = flt->lfo = _lfo_create();
                  }
               }
               else if (lfo)
               {
                  _lfo_destroy(flt->lfo);
                   lfo = flt->lfo = NULL;
               }

               if (lfo)
               {
                  memcpy(lfo, &data->lfo, sizeof(_aaxLFOData));

                  lfo->min = fc;
                  lfo->max = fmax;
                  if (lfo->max == 0.0f) {
                      lfo->max = 22050.0f;
                  }
                  if (fabsf(lfo->max - lfo->min) < 200.0f)
                  {
                     lfo->min = 0.5f*(lfo->min + lfo->max);
                     lfo->max = lfo->min;
                  }
                  else if (lfo->max < lfo->min)
                  {
                     float f = lfo->max;
                     lfo->max = lfo->min;
                     lfo->min = f;
                     state ^= AAX_INVERSE;
                  }

                  lfo->min_sec = lfo->min/lfo->fs;
                  lfo->max_sec = lfo->max/lfo->fs;
                  lfo->depth = 1.0f;
                  lfo->offset = 0.0f;
                  lfo->f = flt->Q;
                  lfo->inv = (state & AAX_INVERSE) ? AAX_TRUE : AAX_FALSE;
                  lfo->stereo_lnk = !stereo;

                  constant = _lfo_set_timing(lfo);
                  lfo->envelope = AAX_FALSE;

                  if (!_lfo_set_function(lfo, constant)) {
                     _aaxErrorSet(AAX_INVALID_PARAMETER);
                  }
               }
            }
         }
      }
      else _aaxErrorSet(AAX_INSUFFICIENT_RESOURCES);
      break;
   }
   case AAX_FALSE:
   {
      if (effect->slot[0]->data)
      {
         effect->slot[0]->destroy(effect->slot[0]->data);
         effect->slot[0]->data = NULL;
      }
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
_aaxNewPhasingEffectHandle(const aaxConfig config, enum aaxEffectType type, _aax2dProps* p2d, UNUSED(_aax3dProps* p3d))
{
   _handle_t *handle = get_driver_handle(config);
   _aaxMixerInfo* info = handle ? handle->info : _info;
   _effect_t* rv = _aaxEffectCreateHandle(info, type, 2, DSIZE);

   if (rv)
   {
      _aax_dsp_copy(rv->slot[0], &p2d->effect[rv->pos]);
      rv->slot[0]->destroy = _delay_destroy;
      rv->slot[0]->swap = _delay_swap;
      rv->slot[0]->data = NULL;

      rv->state = p2d->effect[rv->pos].state;
   }
   return rv;
}

static float
_aaxPhasingEffectSet(float val, int ptype, unsigned char param)
{
   float rv = val;
   if ((param == AAX_DELAY_GAIN) && (ptype == AAX_DECIBEL)) {
      rv = _lin2db(val);
   }
   else if ((param == AAX_LFO_DEPTH || param == AAX_LFO_OFFSET) &&
            (ptype == AAX_MICROSECONDS)) {
       rv = (val*1e-6f)/PHASING_MAX;
   }
   return rv;
}

static float
_aaxPhasingEffectGet(float val, int ptype, unsigned char param)
{
   float rv = val;
   if ((param == AAX_DELAY_GAIN) && (ptype == AAX_DECIBEL)) {
      rv = _db2lin(val);
   }
   else if ((param == AAX_LFO_DEPTH || param == AAX_LFO_OFFSET) &&
            (ptype == AAX_MICROSECONDS)) {
       rv = val*PHASING_MAX*1e6f;
   }
   return rv;
}

static float
_aaxPhasingEffectMinMax(float val, int slot, unsigned char param)
{
   static const _eff_minmax_tbl_t _aaxPhasingRange[_MAX_FE_SLOTS] =
   {    /* min[4] */                  /* max[4] */
    { {  0.001f,  0.01f, 0.0f, 0.0f  }, {     1.0f,    10.0f, 1.0f, 1.0f } },
    { {   20.0f, 20.0f,  0.0f, 0.01f }, { 22050.0f, 22050.0f, 1.0f, 1.0f } },
    { {    0.0f,  0.0f,  0.0f, 0.0f  }, {     0.0f,     0.0f, 0.0f, 0.0f } },
    { {    0.0f,  0.0f,  0.0f, 0.0f  }, {     0.0f,     0.0f, 0.0f, 0.0f } }
   };

   assert(slot < _MAX_FE_SLOTS);
   assert(param < 4);

   return _MINMAX(val, _aaxPhasingRange[slot].min[param],
                       _aaxPhasingRange[slot].max[param]);
}

/* -------------------------------------------------------------------------- */

_eff_function_tbl _aaxPhasingEffect =
{
   AAX_FALSE,
   "AAX_phasing_effect_"AAX_MKSTR(VERSION), VERSION,
   (_aaxEffectCreate*)&_aaxPhasingEffectCreate,
   (_aaxEffectDestroy*)&_aaxPhasingEffectDestroy,
   (_aaxEffectReset*)&_delay_reset,
   (_aaxEffectSetState*)&_aaxPhasingEffectSetState,
   NULL,
   (_aaxNewEffectHandle*)&_aaxNewPhasingEffectHandle,
   (_aaxEffectConvert*)&_aaxPhasingEffectSet,
   (_aaxEffectConvert*)&_aaxPhasingEffectGet,
   (_aaxEffectConvert*)&_aaxPhasingEffectMinMax
};

void*
_delay_create(void *d, void *i, char delay, char feedback)
{
   _aaxRingBufferDelayEffectData *data = d;
   _aaxMixerInfo *info = i;

   if (data == NULL)
   {
      data  = _aax_aligned_alloc(DSIZE);
      if (data) memset(data, 0, DSIZE);
   }

   if (data && data->offset == NULL)
   {
      data->offset = calloc(1, sizeof(_aaxRingBufferOffsetData));
      if (!data->offset)
      {
         _aax_aligned_free(data);
         data = NULL;
      }
   }

   if (data)
   {
      unsigned int tracks = info->no_tracks;
      float fs = info->frequency;

      data->history_samples = TIME_TO_SAMPLES(fs, DELAY_EFFECTS_TIME);

#if 1
      if (data->history == NULL) {
         _aaxRingBufferCreateHistoryBuffer(&data->history,
                                           data->history_samples, tracks);
      }
      if (data->feedback_history == NULL) {
         _aaxRingBufferCreateHistoryBuffer(&data->feedback_history,
                                           data->history_samples, tracks);
      }
#else
      if (delay && data->history == NULL)
      {
         if (!feedback && data->feedback_history)
         {
            data->history = data->feedback_history;
            data->feedback_history = NULL;
         }
         else {
            _aaxRingBufferCreateHistoryBuffer(&data->history,
                                              data->history_samples, tracks);
         }
      }

      if (feedback && data->feedback_history == NULL)
      {
         if (!delay && data->history)
         {
            data->feedback_history = data->history;
            data->history = NULL;
         }
         else {
            _aaxRingBufferCreateHistoryBuffer(&data->feedback_history,
                                              data->history_samples, tracks);
         }
      }
#endif

      if (!data->history || !data->feedback_history)
      {
         free(data->offset);
         data->offset = NULL;

         _aax_aligned_free(data);
         data = NULL;
      }
   }

   return data;
}

void
_delay_swap(void *d, void *s)
{
   _aaxFilterInfo *dst = d, *src = s;

   if (src->data && src->data_size)
   {
      if (!dst->data) {
          dst->data = _aaxAtomicPointerSwap(&src->data, dst->data);
          dst->data_size = src->data_size;
      }
      else
      {
         _aaxRingBufferDelayEffectData *ddef = dst->data;
         _aaxRingBufferDelayEffectData *sdef = src->data;

         assert(dst->data_size == src->data_size);

         _lfo_swap(&ddef->lfo, &sdef->lfo);
         ddef->delay = sdef->delay;
         ddef->history_samples = sdef->history_samples;
         ddef->feedback = sdef->feedback;
         ddef->flanger = sdef->flanger;
         ddef->run = sdef->run;
      }
   }
   dst->destroy = src->destroy;
   dst->swap = src->swap;
}

void
_delay_destroy(void *ptr)
{
   _aaxRingBufferDelayEffectData *data = ptr;
   if (data)
   {
      data->lfo.envelope = AAX_FALSE;
      if (data->offset)
      {
         free(data->offset);
         data->offset = NULL;
      }
      if (data->history)
      {
         free(data->history);
         data->history = NULL;
      }
      if (data->feedback_history)
      {
         free(data->feedback_history);
         data->feedback_history = NULL;
      }
      if (data->freq_filter)
      {
         _freqfilter_destroy(data->freq_filter);
         data->freq_filter = NULL;
      }
      _aax_aligned_free(data);
   }
}

void
_delay_reset(void *ptr)
{
   _aaxRingBufferDelayEffectData *data = ptr;
   if (data) _lfo_reset(&data->lfo);
}

size_t
_delay_prepare(MIX_PTR_T dst, MIX_PTR_T src, size_t no_samples, void *data, unsigned int track)
{
   static const size_t bps = sizeof(MIX_T);
   _aaxRingBufferDelayEffectData* delay = data;
   size_t ds;

   assert(delay);
   assert(delay->history);
   assert(delay->history->ptr);
   assert(bps <= sizeof(MIX_T));

   ds = delay->history_samples;

   // copy the delay effects history to src
//       DBG_MEMCLR(1, src-ds, ds, bps);
   _aax_memcpy(src-ds, delay->history->history[track], ds*bps);

   // copy the new delay effects history back
   _aax_memcpy(delay->history->history[track], src+no_samples-ds, ds*bps);

   return ds;
}

/**
 * - d and s point to a buffer containing the delay effects buffer prior to
 *   the pointer.
 * - start is the starting pointer
 * - end is the end pointer (end-start is the number of samples)
 * - no_samples is the number of samples to process this run
 * - dmax does not include ds
 */
int
_delay_run(void *rb, MIX_PTR_T d, MIX_PTR_T s, MIX_PTR_T scratch,
             size_t start, size_t end, size_t no_samples, size_t ds,
             void *data, void *env, unsigned int track)
{
   static const size_t bps = sizeof(MIX_T);
   _aaxRingBufferSample *rbd = (_aaxRingBufferSample*)rb;
   _aaxRingBufferDelayEffectData* effect = data;
   MIX_T *sptr = s + start;
   MIX_T *nsptr = sptr;
   ssize_t offs, noffs;
   int rv = AAX_FALSE;
   float volume;

   _AAX_LOG(LOG_DEBUG, __func__);

   assert(s != 0);
   assert(d != 0);
   assert(start < end);
   assert(data != NULL);

   offs = effect->delay.sample_offs[track];
   if (ds > effect->history_samples) {
      ds = effect->history_samples;
   }

   assert(start || (offs < (ssize_t)ds));
   if (offs >= (ssize_t)ds) offs = ds-1;

   if (start) {
      noffs = effect->offset->noffs[track];
   }
   else
   {
      noffs = (size_t)effect->lfo.get(&effect->lfo, env, s, track, end);
      effect->delay.sample_offs[track] = noffs;
      effect->offset->noffs[track] = noffs;
   }

   assert(s != d);

   volume = effect->feedback;
   if (offs && volume > LEVEL_96DB)
   {
//    float *hist = effect->lf_history.history[track];
//    float k = effect->lf_k;
      ssize_t coffs, doffs;
      int i, step, sign;

      sign = (noffs < offs) ? -1 : 1;
      doffs = labs(noffs - offs);
      i = no_samples;
      coffs = offs;
      step = end;

      if (start)
      {
         step = effect->offset->step[track];
         coffs = effect->offset->coffs[track];
      }
      else
      {
         if (doffs)
         {
            step = end/doffs;
            if (step < 2) step = end;
         }
      }
      effect->offset->step[track] = step;

      _aax_memcpy(nsptr-ds, effect->feedback_history->history[track], ds*bps);
      if (i >= step)
      {
         do
         {
            rbd->add(nsptr, nsptr-coffs, step, volume, 0.0f);

            nsptr += step;
            coffs += sign;
            i -= step;
         }
         while(i >= step);
      }
      if (i) {
         rbd->add(nsptr, nsptr-coffs, i, volume, 0.0f);
      }
      effect->offset->coffs[track] = coffs;

      _aax_memcpy(effect->feedback_history->history[track], sptr+no_samples-ds, ds*bps);

#if 0
      // low-pass to smoothen the result
      _batch_movingaverage_float(scratch, sptr, no_samples, hist, k);
      sptr = scratch;
#else
      nsptr = sptr;
#endif
   }

   volume =  effect->delay.gain;
   if (offs && volume > LEVEL_96DB)
   {
      _aaxRingBufferFreqFilterData *flt = effect->freq_filter;
      MIX_T *dptr = d + start;
      ssize_t doffs;

      doffs = noffs - offs;

      // first process the delayed (wet) signal
      // then add the original (dry) signal
      if (doffs == 0)
      {
         if (flt && ((flt->type == LOWPASS && flt->fc < MAXIMUM_CUTOFF) ||
                     (flt->type == HIGHPASS && flt->fc > MINIMUM_CUTOFF)))
         {
//          if ((flt->type == LOWPASS && flt->fc > MINIMUM_CUTOFF) ||
//              (flt->type == HIGHPASS && flt->fc < MAXIMUM_CUTOFF))
            {
               flt->run(rbd, dptr, nsptr-offs, 0, no_samples, 0, track, flt, env, 1.0f, 0);
               rbd->add(dptr, sptr, no_samples, 1.0f, 0.0f);
               rv = AAX_TRUE;
            }
         }
         else
         {
            rbd->multiply(dptr, nsptr-offs, bps, no_samples, volume);
            rbd->add(dptr, sptr, no_samples, 1.0f, 0.0f);
            rv = AAX_TRUE;
         }
      }
      else
      {
         float pitch = _MAX(((float)end-(float)doffs)/(float)(end), 0.001f);

         rbd->resample(dptr, nsptr-offs, 0, no_samples, 0.0f, pitch);
         if (flt && ((flt->type == LOWPASS && flt->fc < MAXIMUM_CUTOFF) ||
                     (flt->type == HIGHPASS && flt->fc > MINIMUM_CUTOFF)))
         {
            if ((flt->type == LOWPASS && flt->fc > MINIMUM_CUTOFF) ||
                (flt->type == HIGHPASS && flt->fc < MAXIMUM_CUTOFF))
            {
               flt->run(rbd, dptr, dptr, 0, no_samples, 0, track, flt, env, 1.0f, 0);
               rbd->add(dptr, sptr, no_samples, 1.0f, 0.0f);
               rv = AAX_TRUE;
            }
         }
         else
         {
            rbd->multiply(dptr, dptr, bps, no_samples, volume);
            rbd->add(dptr, sptr, no_samples, 1.0f, 0.0f);
            rv = AAX_TRUE;
         }
      }
   }

   return rv;
}


/*
 * Copyright 2007-2019 by Erik Hofman.
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

#include <assert.h>

#include <aax/aax.h>

#include <base/types.h>		/*  for rintf */
#include <base/gmath.h>

#include <software/rbuf_int.h>
#include "effects.h"
#include "arch.h"
#include "dsp.h"
#include "api.h"

#define VERSION		1.11
#define DSIZE		sizeof(_aaxRingBufferReverbData)
#define REFLECTIONSIZE	sizeof(_aaxRingBufferReflectionData)
#define LOOPBACKSIZE	sizeof(_aaxRingBufferLoopbackData)

#define NUM_LOOPBACKS	2
#define NUM_REFLECTIONS	4

static void _reverb_swap(void*,void*);
static void _reverb_destroy(void*);

static void _reverb_clear(void*, unsigned int);
static void _reverb_prepare(_aaxEmitter*, _aax3dProps*, void*);
static int _reverb_run(void*, MIX_PTR_T, CONST_MIX_PTR_T, MIX_PTR_T, size_t, size_t, unsigned int, const void*, _aaxMixerInfo*, unsigned char, int, void*, unsigned char);

static void _reflections_prepare(MIX_PTR_T, MIX_PTR_T, size_t, void*, unsigned int);
static void _reverb_add_reflections(_aaxRingBufferReverbData*, float, unsigned int, float, int, float);
static void _reverb_add_loopbacks(_aaxRingBufferReverbData*, float, unsigned int, float, float);
static void _loopbacks_destroy_delays(_aaxRingBufferReverbData*);


static aaxEffect
_aaxReverbEffectCreate(_aaxMixerInfo *info, enum aaxEffectType type)
{
   _effect_t* eff = _aaxEffectCreateHandle(info, type, 2, DSIZE);
   aaxEffect rv = NULL;

   if (eff)
   {
      _aaxSetDefaultEffect3d(eff->slot[0], eff->pos, 0);
      _aaxSetDefaultEffect3d(eff->slot[1], eff->pos, 1);
      eff->slot[0]->destroy = _reverb_destroy;
      eff->slot[0]->swap = _reverb_swap;
      rv = (aaxEffect)eff;
   }
   return rv;
}

static int
_aaxReverbEffectDestroy(_effect_t* effect)
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
_aaxReverbEffectSetState(_effect_t* effect, int state)
{
   void *handle = effect->handle;
   aaxEffect rv = AAX_FALSE;
   int rstate;

   rstate = state;
   if (rstate & ~AAX_ENVELOPE_FOLLOW) {
      rstate = (AAX_REVERB_REFLECTIONS | AAX_REVERB_LOOPBACKS);
   }

   switch (state & ~AAX_ENVELOPE_FOLLOW)
   {
   case AAX_TRUE:
   case AAX_INVERSE:
   case (AAX_INVERSE|AAX_TRUE):
   case AAX_REVERB_REFLECTIONS:
   case AAX_REVERB_LOOPBACKS:
   {
      char reflections=(rstate & AAX_REVERB_REFLECTIONS) ? AAX_TRUE : AAX_FALSE;
      char loopbacks = (rstate & AAX_REVERB_LOOPBACKS) ? AAX_TRUE : AAX_FALSE;
      _aaxRingBufferReverbData *reverb = effect->slot[0]->data;
      unsigned int tracks = effect->info->no_tracks;
      float lb_depth, rate = 23.0f;
      float depth, fs = 48000.0f;

      if (effect->info)
      {
         fs = effect->info->frequency;
         rate = effect->info->period_rate;
      }

      if (reverb == NULL)
      {
          reverb = malloc(DSIZE);
          effect->slot[0]->data = reverb;
          if (reverb)  memset(reverb, 0, DSIZE);
      }

      if (reverb)
      {
         _aaxRingBufferFreqFilterData *flt = reverb->freq_filter;
         float decay_level;

         reverb->reflections_prepare = _reflections_prepare;
         reverb->prepare = _reverb_prepare;
         reverb->clear = _reverb_clear;
         reverb->run = _reverb_run;

         if (reverb->direct_path == 0)
         {
            reverb->no_samples = TIME_TO_SAMPLES(fs, 1.0f/rate);
            _aaxRingBufferCreateHistoryBuffer(&reverb->direct_path,
                                              reverb->no_samples, tracks);
         }

         if (!flt)
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
            reverb->freq_filter = flt;
         }

         /* calculate initial and loopback samples                      */
         decay_level = effect->slot[0]->param[AAX_DECAY_LEVEL];
         depth = effect->slot[0]->param[AAX_DELAY_DEPTH]/0.07f;
         lb_depth = effect->slot[0]->param[AAX_DECAY_DEPTH]/0.7f;

         if (reflections) {
            _reverb_add_reflections(reverb, fs, tracks, depth, state, decay_level);
         }
         if (loopbacks) {
            _reverb_add_loopbacks(reverb, fs, tracks, lb_depth, 1.0f);
         }

         if (flt)
         {
            float dfact;

            /* set up a frequency filter between 100Hz and 15000Hz
             * for the reflections. The lower the cut-off frequency,
             * the more the low frequencies get exaggerated.
             *
             * low: 100Hz/1.75*gain .. 15000Hz/1.0*gain
             * high: 100Hz/0.0*gain .. 15000Hz/0.33*gain
             *
             * Q is set to 0.6 to overly dampen the frequency response to
             * provide a bit smoother frequency response  around the
             * cut-off frequency.
             */
            reverb->fc = effect->slot[0]->param[AAX_CUTOFF_FREQUENCY];

            flt->run = _freqfilter_run;
            flt->fs = fs;
            flt->Q = 0.6f;
            flt->low_gain = 0.0f;
            flt->no_stages = 1;

            dfact = powf(reverb->fc*0.00005f, 0.2f);
            flt->low_gain = _MIN(1.75f-0.75f*dfact, 1.0f);
            flt->high_gain = 1.0f - 0.33f*dfact;
            flt->k = flt->low_gain/flt->high_gain;
            flt->type = (flt->high_gain >= flt->low_gain) ? LOWPASS : HIGHPASS;

            _aax_butterworth_compute(reverb->fc, flt);

            if (state & AAX_ENVELOPE_FOLLOW)
            {
               _aaxLFOData* lfo = flt->lfo;

               if (lfo == NULL) {
                  lfo = flt->lfo = _lfo_create();
               }

               if (lfo)
               {
                  int constant;

                  /* sweeprate */
                  lfo->convert = _linear;
                  lfo->state = AAX_ENVELOPE_FOLLOW;
                  lfo->fs = effect->info->frequency;
                  lfo->period_rate = effect->info->period_rate;

                  lfo->min = effect->slot[0]->param[AAX_CUTOFF_FREQUENCY];
                  lfo->max = 22000.0f;
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
                  lfo->f = 1.0f;
                  lfo->inv = (state & AAX_INVERSE) ? AAX_TRUE : AAX_FALSE;
                  lfo->stereo_lnk = !(state & AAX_LFO_STEREO);

                  constant = _lfo_set_timing(lfo);
                  lfo->envelope = AAX_FALSE;

                  if (!_lfo_set_function(lfo, constant)) {
                     _aaxErrorSet(AAX_INVALID_PARAMETER);
                  }
               } /* flt->lfo */
            }
         }

         reverb->state = rstate;
         reverb->info = effect->info;
         reverb->freq_filter = flt;
         reverb->occlusion = _occlusion_create(reverb->occlusion, effect->slot[1], state, fs);
      }

      break;
   }
   case AAX_FALSE:
      if (effect->slot[0]->data)
      {
         effect->slot[0]->destroy(effect->slot[0]->data);
         effect->slot[0]->data = NULL;
      }
      break;
   default:
      _aaxErrorSet(AAX_INVALID_PARAMETER);
      break;
   }
   rv = effect;
   return rv;
}

_effect_t*
_aaxNewReverbEffectHandle(const aaxConfig config, enum aaxEffectType type, UNUSED(_aax2dProps* p2d), _aax3dProps* p3d)
{
   _handle_t *handle = get_driver_handle(config);
   _aaxMixerInfo* info = handle ? handle->info : _info;
   _effect_t* rv = _aaxEffectCreateHandle(info, type, 2, DSIZE);

   if (rv)
   {
      _aax_dsp_copy(rv->slot[1], &p2d->effect[rv->pos]);
      _aax_dsp_copy(rv->slot[0], &p2d->effect[rv->pos]);
      rv->slot[0]->destroy = _reverb_destroy;
      rv->slot[0]->swap = _reverb_swap;
      rv->slot[0]->data = NULL;

      rv->state = p3d->effect[rv->pos].state;
   }
   return rv;
}

static float
_aaxReverbEffectSet(float val, UNUSED(int ptype), UNUSED(unsigned char param))
{
   float rv = val;
   return rv;
}

static float
_aaxReverbEffectGet(float val, UNUSED(int ptype), UNUSED(unsigned char param))
{
   float rv = val;
   return rv;
}

static float
_aaxReverbEffectMinMax(float val, int slot, unsigned char param)
{
   static const _eff_minmax_tbl_t _aaxReverbRange[_MAX_FE_SLOTS] =
   {    /* min[4] */                  /* max[4] */
    { {50.0f, 0.001f, 0.0f, 0.001f }, { 22000.0f,   0.07f, FLT_MAX, 0.7f } },
    { { 0.0f,   0.0f, 0.0f,   0.0f }, {  FLT_MAX, FLT_MAX, FLT_MAX, 1.0f } },
    { { 0.0f,   0.0f, 0.0f,   0.0f }, {     0.0f,    0.0f,    0.0f, 0.0f } },
    { { 0.0f,   0.0f, 0.0f,   0.0f }, {     0.0f,    0.0f,    0.0f, 0.0f } }
   };

   assert(slot < _MAX_FE_SLOTS);
   assert(param < 4);

   return _MINMAX(val, _aaxReverbRange[slot].min[param],
                       _aaxReverbRange[slot].max[param]);
}

/* -------------------------------------------------------------------------- */

_eff_function_tbl _aaxReverbEffect =
{
   AAX_TRUE,
   "AAX_reverb_effect_"AAX_MKSTR(VERSION), VERSION,
   (_aaxEffectCreate*)&_aaxReverbEffectCreate,
   (_aaxEffectDestroy*)&_aaxReverbEffectDestroy,
   NULL,
   (_aaxEffectSetState*)&_aaxReverbEffectSetState,
   NULL,
   (_aaxNewEffectHandle*)&_aaxNewReverbEffectHandle,
   (_aaxEffectConvert*)&_aaxReverbEffectSet,
   (_aaxEffectConvert*)&_aaxReverbEffectGet,
   (_aaxEffectConvert*)&_aaxReverbEffectMinMax
};

void
_reverb_swap(void *d, void *s)
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
         _aaxRingBufferReverbData *drev = dst->data;
         _aaxRingBufferReverbData *srev = src->data;

         assert(dst->data_size == src->data_size);

         drev->run = srev->run;
      }
   }
   dst->destroy = src->destroy;
   dst->swap = src->swap;
}

static void
_reverb_destroy(void *ptr)
{
   _aaxRingBufferReverbData *reverb = (_aaxRingBufferReverbData*)ptr;
   if (reverb)
   {
      _occlusion_destroy(reverb->occlusion);
      _freqfilter_destroy(reverb->freq_filter);
      if (reverb->reflections)
      {
         reverb->reflections->no_delays = 0;
         reverb->reflections->delay[0].gain = 1.0f;
         _aax_aligned_free(reverb->reflections);
         reverb->reflections = NULL;
      }
      if (reverb->loopbacks)
      {
         _loopbacks_destroy_delays(reverb);
         reverb->loopbacks = NULL;
      }
      free(reverb);
   }
}

static void
_reverb_prepare(_aaxEmitter *src, _aax3dProps *fp3d, void *data)
{
   _aaxRingBufferReverbData *reverb = data;
   _aaxRingBufferOcclusionData *occlusion;
   _aaxRingBufferFreqFilterData *filter;

   filter = reverb->freq_filter;
   occlusion = reverb->occlusion;
   if (occlusion)
   {
      float l;

      _occlusion_prepare(src, fp3d, occlusion);

      l = 1.0f - occlusion->level;
      reverb->fc = _MINMAX(l*22000.0f, 100.0f, reverb->fc);
      if (reverb->fc > 100.0f) {
          _aax_butterworth_compute(reverb->fc, filter);
      }
   }
}

static void
_reverb_clear(void *data, unsigned int tracks)
{
   static const size_t bps = sizeof(MIX_T);
   _aaxRingBufferReverbData *reverb = data;
   unsigned int i;

   for (i=0; i<tracks; ++i) {
      memset(reverb->direct_path->history[i], 0, bps*reverb->no_samples);
   }
}

void
_reflections_prepare(MIX_PTR_T dst, MIX_PTR_T src, size_t no_samples, void *data, unsigned int track)
{
   static const size_t bps = sizeof(MIX_T);
   _aaxRingBufferReverbData *reverb = data;
   _aaxRingBufferReflectionData *reflections;
   size_t ds;

   assert(reverb);
   assert(reverb->reflections);
   assert(bps <= sizeof(MIX_T));

   reflections = reverb->reflections;

   assert(reflections->history);
   assert(reflections->history->ptr);

   ds = reflections->history_samples;

   // copy the delay effects history to src
// DBG_MEMCLR(1, src-ds, ds, bps);
   _aax_memcpy(src-ds, reflections->history->history[track], ds*bps);

   // copy the new delay effects history back
   _aax_memcpy(reflections->history->history[track], src+no_samples-ds, ds*bps);
}

// Calculate the 1st order reflections
static void
_reverb_add_reflections(_aaxRingBufferReverbData *reverb, float fs, unsigned int tracks, float depth, int state, float decay_level)
{
   _aaxRingBufferReflectionData *reflections = reverb->reflections;
   unsigned int num = NUM_REFLECTIONS;

   assert(reverb != 0);
   assert(num < _AAX_MAX_DELAYS);

   if (reflections == NULL)
   {
      reflections = _aax_aligned_alloc(REFLECTIONSIZE);
      reverb->reflections = reflections;
      if (reflections) memset(reflections, 0, REFLECTIONSIZE);
   }

   if (reflections)
   {
      static const float max_depth = _MIN(REVERB_EFFECTS_TIME, 0.15f);
      float idepth, igain, idepth_offs, lb_gain;
      float delays[8], gains[8];
      size_t i;

      reflections->history_samples = TIME_TO_SAMPLES(fs, max_depth);
      if (reflections->history == 0)
      {
         _aaxRingBufferCreateHistoryBuffer(&reflections->history,
                                          reflections->history_samples, tracks);
      }

      lb_gain = 0.01f + 0.99f*_MIN(decay_level, 1.0f);

      /*
       https://christianfloisand.wordpress.com/2012/09/04/digital-reverberation/
       * gain = 0.001f * tau / RVT, where
       *   tau = the delay time of the comb filter
       *   RVT =  loopbacks time desired, which is defined as the time it takes
       *          for the delayed signal to reach -60dB (considered silence).
       */

      /* initial delay in seconds (should be between 10ms en 70 ms) */
      /* initial gains, defining a direct path is not necessary     */
      /* sound Attenuation coeff. in dB/m (α) = 4.343 µ (m-1)       */
      // http://www.sae.edu/reference_material/pages/Coefficient%20Chart.htm
      igain = 2.0f*lb_gain;
      gains[0] = igain*0.9484f;      // conrete/brick = 0.95
      gains[1] = igain*0.8935f;      // wood floor    = 0.90
      gains[2] = igain*0.8254f;      // carpet        = 0.853
      gains[3] = igain*0.8997f;
      gains[4] = igain*0.8346f;
      gains[5] = igain*0.7718f;
      gains[6] = igain*0.7946f;

      // depth definies the initial delay of the first reflections
      idepth = 0.005f+0.045f*depth;
      idepth_offs = (max_depth-idepth)*depth;
      idepth_offs = _MINMAX(idepth_offs, 0.01f, max_depth-0.05f);
      assert(idepth_offs+idepth*0.9876543f <= REVERB_EFFECTS_TIME);

      idepth *= fs;
      idepth_offs *= fs;
      if (state & AAX_INVERSE)
      {
         delays[0] = idepth_offs + idepth/1.0f;
         delays[2] = idepth_offs + idepth/2.0f;
         delays[1] = idepth_offs + idepth/3.0f;
         delays[6] = idepth_offs + idepth/5.0f;
         delays[4] = idepth_offs + idepth/7.0f;
         delays[5] = idepth_offs + idepth/11.0f;
         delays[3] = idepth_offs + idepth/13.0f;
      }
      else
      {
         delays[5] = idepth_offs + idepth/1.0f;
         delays[3] = idepth_offs + idepth/2.0f;
         delays[1] = idepth_offs + idepth/3.0f;
         delays[6] = idepth_offs + idepth/5.0f;
         delays[4] = idepth_offs + idepth/7.0f;
         delays[2] = idepth_offs + idepth/11.0f;
         delays[0] = idepth_offs + idepth/13.0f;
      }

      reflections->gain = 1.0f/_MAX(decay_level, 1.0f);
      reflections->no_delays = num;
      for (i=0; i<num; ++i)
      {
         if ((gains[i] > 0.001f) || (gains[i] < -0.001f))
         {
            unsigned int track;
            for (track=0; track<tracks; ++track) {
               reflections->delay[i].sample_offs[track] = (ssize_t)delays[i];
            }
            reflections->delay[i].gain = gains[i];
#if 0
 printf("reflection delay[%zi]: %zi\n", i, reflections->delay[i].sample_offs[0]);
#endif
         }
         else {
            reflections->delay[i].gain = 0.0f;
         }
      }
   }
}

// Calculate the 2nd order reflections
static void
_reverb_add_loopbacks(_aaxRingBufferReverbData *reverb, float fs, unsigned int tracks, float lb_depth, float lb_gain)
{
   _aaxRingBufferLoopbackData *loopbacks = reverb->loopbacks;
   unsigned int num;

   if (loopbacks == NULL)
   {
      loopbacks = _aax_aligned_alloc(LOOPBACKSIZE);
      reverb->loopbacks = loopbacks;
      if (loopbacks) memset(loopbacks, 0, LOOPBACKSIZE);
   }

   if (loopbacks)
   {
      if (loopbacks->reverb == 0)
      {
         size_t samples = TIME_TO_SAMPLES(fs, REVERB_EFFECTS_TIME);
         _aaxRingBufferCreateHistoryBuffer(&loopbacks->reverb,
                                           samples, tracks);
      }

      // http://www.sae.edu/reference_material/pages/Coefficient%20Chart.htm
      // https://web.archive.org/web/20150416071915/http://www.sae.edu/reference_material/pages/Coefficient%20Chart.htm
      if ((lb_depth != 0) && (lb_gain != 0))
      {
         static const float max_depth = REVERB_EFFECTS_TIME; // *0.6877777f;
         unsigned int track;
         float dlb, dlbp;

         num = NUM_LOOPBACKS;
         loopbacks->loopback[0].gain = lb_gain*0.95015f;   // conrete/brick = 0.95
         loopbacks->loopback[1].gain = lb_gain*0.87075f;
         loopbacks->loopback[2].gain = lb_gain*0.91917f;
         loopbacks->loopback[3].gain = lb_gain*0.72317f;   // carpet     = 0.853
         loopbacks->loopback[4].gain = lb_gain*0.80317f;
         loopbacks->loopback[5].gain = lb_gain*0.73317f;
         loopbacks->loopback[6].gain = lb_gain*0.88317f;
#if 0
 for (int i=0; i<7; ++i)
 printf(" loopback[%i].gain: %f\n", i,  loopbacks->loopback[i].gain);
#endif

         dlb = 0.01f+lb_depth*max_depth;
         dlbp = (REVERB_EFFECTS_TIME-dlb)*lb_depth;
         dlbp = _MINMAX(dlbp, 0.01f, REVERB_EFFECTS_TIME-0.01f);

         dlb *= fs;
         dlbp *= fs;
         loopbacks->no_loopbacks = num;
         for (track=0; track<tracks; ++track)
         {
            loopbacks->loopback[0].sample_offs[track] = dlb/1.0f;
            loopbacks->loopback[1].sample_offs[track] = dlb/2.0f;
            loopbacks->loopback[2].sample_offs[track] = dlb/3.0f;
            loopbacks->loopback[3].sample_offs[track] = dlb/5.0f;
            loopbacks->loopback[4].sample_offs[track] = dlb/7.0f;
            loopbacks->loopback[5].sample_offs[track] = dlb/11.0f;
            loopbacks->loopback[6].sample_offs[track] = dlb/13.0f;
         }
#if 0
 for (int i=0; i<7; ++i)
 printf(" loopback offset[%i]: %zi\n", i, loopbacks->loopback[i].sample_offs[0]);
#endif
      }
   }
}

static void
_loopbacks_destroy_delays(_aaxRingBufferReverbData *reverb)
{
   _aaxRingBufferLoopbackData *loopbacks = reverb->loopbacks;
   assert(reverb != 0);
   if (loopbacks)
   {
      loopbacks->no_loopbacks = 0;
#if BYTE_ALIGN
      if (loopbacks->reverb) _aax_free(loopbacks->reverb);
#else
      if (loopbacks->reverb) free(loopbacks->reverb);
#endif
      loopbacks->reverb = NULL;
      _aax_aligned_free(loopbacks);
   }
}

static int
_reflections_run(const _aaxRingBufferReflectionData *reflections,
                _aaxRingBufferSample *rbd, MIX_PTR_T dptr, CONST_MIX_PTR_T sptr,
                 size_t no_samples, size_t ds, unsigned int track, float gain,
                 float dst, unsigned char mono, int state)
{
   unsigned int snum, tracks;
   int rv = AAX_FALSE;
   float volume;

   _AAX_LOG(LOG_DEBUG, __func__);

   assert(dptr != 0);
   assert(sptr != 0);
   assert(track < _AAX_MAX_SPEAKERS);

   tracks = rbd->no_tracks;

   /* reflections (1st order reflections) */
   /* skip if the caller is mono  */
   snum = reflections->no_delays;
   if (!mono && (snum > 0))
   {
      unsigned int q = 0;

#if 0
      memset(dptr, 0, no_samples*sizeof(MIX_T));
#else
      do
      {
         volume = gain*reflections->delay[q].gain;
         volume /= (1 + (track+q) % tracks);
         if ((volume > 0.001f) || (volume < -0.001f))
         {
            ssize_t offs = reflections->delay[q].sample_offs[track] + dst;
            if (offs && offs < (ssize_t)ds) {
               rbd->add(dptr, sptr-offs, no_samples, volume, 0.0f);
            }
         }
         q++;
      }
      while ((volume < 0.001f) && (volume > -0.001f));
#endif

      for(; q<snum; ++q)
      {
         volume = gain*reflections->delay[q].gain;
         volume /= (1 + (track+q) % tracks);
         if ((volume > 0.001f) || (volume < -0.001f))
         {
            ssize_t offs = reflections->delay[q].sample_offs[track] + dst;
            if (offs && offs < (ssize_t)ds) {
               rbd->add(dptr, sptr-offs, no_samples, volume, 0.0f);
            }
         }
      }

      rv = AAX_TRUE;
   }
   else if (gain > LEVEL_64DB)
   {
      memcpy(dptr, sptr, no_samples*sizeof(MIX_T));
      if (gain < (1.0f-LEVEL_64DB)) {
         rbd->multiply(dptr, dptr, sizeof(MIX_T), no_samples, gain);
      }

      rv = AAX_TRUE;
   }

   return rv;
}

static int
_loopbacks_run(_aaxRingBufferLoopbackData *loopbacks, void *rb, MIX_PTR_T dptr, MIX_PTR_T scratch, size_t no_samples, size_t ds, unsigned int track, float gain, float dst, int state)
{
   int snum, rv = AAX_FALSE;

   /* loopbacks (2nd order reflections) */
   snum = loopbacks->no_loopbacks;
   if ((snum > 0) && (state & AAX_REVERB_LOOPBACKS))
   {
      _aaxRingBufferSample *rbd = (_aaxRingBufferSample*)rb;
      size_t bytes = ds*sizeof(MIX_T);
      int q;

      memcpy(scratch, dptr, no_samples*sizeof(MIX_T));
      memcpy(dptr-ds, loopbacks->reverb->history[track], bytes);

      for(q=0; q<snum; ++q)
      {
         float volume = gain * loopbacks->loopback[q].gain / (snum+1);
         if ((volume > 0.001f) || (volume < -0.001f))
         {
            ssize_t offs = loopbacks->loopback[q].sample_offs[track] + dst;
            if (offs && offs < (ssize_t)ds)
            {
               // comb filter
               rbd->add(dptr, dptr-offs, no_samples, volume, 0.0f);
               // make it all-pass
               rbd->add(dptr, scratch, no_samples, -volume, 0.0f);
            }
         }
      }
      memcpy(loopbacks->reverb->history[track], dptr+no_samples-ds, bytes);
   }

   return rv;
}

static int
_reverb_run(void *rb, MIX_PTR_T dptr, CONST_MIX_PTR_T sptr, MIX_PTR_T scratch,
            size_t no_samples, size_t ds, unsigned int track, const void *data,
            _aaxMixerInfo *info, unsigned char mono, int state, void *env,
            unsigned char ctr)
{
   float dst = info ? _MAX(info->speaker[track].v4[0]*info->frequency*track/343.0,0.0f) : 0;
   _aaxRingBufferSample *rbd = (_aaxRingBufferSample*)rb;
   const _aaxRingBufferReverbData *reverb = data;
   _aaxRingBufferFreqFilterData *filter = reverb->freq_filter;
   _aaxRingBufferOcclusionData *occlusion;
   int rv = AAX_FALSE;
   float gain = 1.0f;

   _AAX_LOG(LOG_DEBUG, __func__);

   assert(dptr != 0);
   assert(sptr != 0);
   assert(scratch != 0);
   assert(track < _AAX_MAX_SPEAKERS);

   occlusion = reverb->occlusion;

   if (gain > LEVEL_64DB)
   {
      MIX_T *dpath = (MIX_T*)reverb->direct_path->history[track];

      if (reverb->reflections)
      {
         float l, fc;

         if (occlusion)
         {
            l = 1.0f - occlusion->level;
            fc = _MINMAX(l*22000.0f, 100.0f, reverb->fc);
            if (fc > 100.0f) {
               _aax_butterworth_compute(fc, filter);
            }
            occlusion->run(rbd, dpath, sptr, scratch, no_samples, track,
                           occlusion);
         }
         else {
            rbd->add(dpath, sptr, no_samples, reverb->reflections->gain, 0.0f);
         }

         if (filter->lfo && !ctr)
         {
            fc = filter->lfo->get(filter->lfo, env, sptr, track, no_samples);
            fc = _MAX(fc, 1);
            _aax_butterworth_compute(fc, filter);
         }

         rv = _reflections_run(reverb->reflections, rb, scratch, sptr,
                               no_samples, ds, track, gain, dst, mono, state);
         filter->run(rbd, dptr, scratch, 0, no_samples, 0, track, filter, NULL, 1.0f, 0);
      }

      if (reverb->loopbacks)
      {
         _loopbacks_run(reverb->loopbacks, rb, dptr, scratch, no_samples, ds,
                        track, gain, dst, state);
         filter->run(rbd, dptr, dptr, 0, no_samples, 0, track, filter, NULL, 1.0f, 0);
         rbd->add(dptr, dpath, no_samples, 1.0f, 0.0f);

      }
   }

   return rv;
}

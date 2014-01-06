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

#include <assert.h>
#include <math.h>	/* tan */

#include <base/logging.h>

#include <api.h>
#include <arch.h>
#include <ringbuffer.h>

#include "rbuf2d_effects.h"


static void szxform(float *, float *, float *, float *, float *, float *,
                    float, float, float *, float *);

/**
 * - dst and scratch point to the beginning of a buffer containing room for
 *   the delay effects prior to the pointer.
 * - start is the starting pointer
 * - end is the end pointer (end-start is the number of smaples)
 * - dmax does not include ds
 */
#define BUFSWAP(a, b) do { void* t = (a); (a) = (b); (b) = t; } while (0);
void
_aaxRingBufferEffectsApply(int32_ptr dst, int32_ptr src,
          int32_ptr scratch, unsigned int start, unsigned int end,
          unsigned int no_samples, unsigned int ddesamps, unsigned int track,
          unsigned char ctr, void *freq, void *delay, void *distort)
{
   static const unsigned int bps = sizeof(int32_t);
   _aaxRingBufferDelayEffectData* effect = delay;
   unsigned int ds = effect ? ddesamps : 0;	/* 0 for frequency filtering */
   int32_t *psrc = src;
   int32_t *pdst = dst;

   if (effect && !effect->loopback && effect->history_ptr)	/* streaming */
   {
      DBG_MEMCLR(1, src-ds, ds+start, bps);
      _aax_memcpy(src+start-ds, effect->delay_history[track], ds*bps);

//    DBG_MEMCLR(1, effect->delay_history[track], ds, bps);
      _aax_memcpy(effect->delay_history[track], src+start+no_samples-ds,ds*bps);
   }

   /* Apply frequency filter first */
   if (freq)
   {
      bufFilterFrequency(pdst, psrc, start, end, ds, track, freq, ctr);
      BUFSWAP(pdst, psrc);
   }

   if (distort)
   {
      bufEffectDistort(pdst, psrc, start, end, ds, track, distort);
      BUFSWAP(pdst, psrc);
   }

#if !ENABLE_LITE
   if (delay)
   {
      /* Apply delay effects */
      if (effect->loopback) {		/*    flanging     */
         bufEffectDelay(psrc, psrc, scratch, start, end, no_samples, ds,
                        delay, track);
      }
      else				/* phasing, chorus */
      {
         _aax_memcpy(pdst+start, psrc+start, no_samples*bps);
         bufEffectDelay(pdst, psrc, scratch, start, end, no_samples, ds,
                        delay, track);
         BUFSWAP(pdst, psrc);
      }
   }
#endif

   if (dst == pdst)	/* copy the data back to the dst buffer */
   {
      DBG_MEMCLR(1, dst-ds, ds+end, bps);
      _aax_memcpy(dst+start, src+start, no_samples*bps);
   }
}

#if !ENABLE_LITE
void
bufEffectReflections(int32_t* s, const int32_ptr sbuf, const int32_ptr sbuf2,
                        unsigned int dmin, unsigned int dmax, unsigned int ds,
                        unsigned int track, const void *data)
{
   const _aaxRingBufferReverbData *reverb = data;
   int snum;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(s != 0);
   assert(sbuf != 0);
   assert(sbuf2 != 0);
   assert(dmin < dmax);
   assert(track < _AAX_MAX_SPEAKERS);

   /* reverb (1st order reflections) */
   snum = reverb->no_delays;
   if (snum > 0)
   {
      _aaxRingBufferFreqFilterData* filter = reverb->freq_filter;
      int32_t *scratch = sbuf + dmin;
      const int32_ptr sptr = s + dmin;
      int q;

      dmax -= dmin;
      _aax_memcpy(scratch, sptr, dmax*sizeof(int32_t));
      for(q=0; q<snum; q++)
      {
         float volume = reverb->delay[q].gain / (snum+1);
         if ((volume > 0.001f) || (volume < -0.001f))
         {
            unsigned int offs = reverb->delay[q].sample_offs[track];

            assert(offs < ds);
//          if (samples >= ds) samples = ds-1;

            _batch_imadd(scratch, sptr-offs, dmax, volume, 0.0f);
         }
      }

      bufFilterFrequency(sbuf2, scratch, 0, dmax, 0, track, filter, 0);
      _batch_imadd(sptr, sbuf2, dmax, 0.5f, 0.0f);
   }
}

void
bufEffectReverb(int32_t *s,
                   unsigned int dmin, unsigned int dmax, unsigned int ds,
                   unsigned int track, const void *data)
{
   const _aaxRingBufferReverbData *reverb = data;
   int snum;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(s != 0);
   assert(dmin < dmax);
   assert(track < _AAX_MAX_SPEAKERS);

   /* loopback for reverb (2nd order reflections) */
   snum = reverb->no_loopbacks;
   if (snum > 0)
   {
      unsigned int bytes = ds*sizeof(int32_t);
      int32_t *sptr = s + dmin;
      int q;

      _aax_memcpy(sptr-ds, reverb->reverb_history[track], bytes);
      for(q=0; q<snum; q++)
      {
         unsigned int samples = reverb->loopback[q].sample_offs[track];
         float volume = reverb->loopback[q].gain / (snum+1);

         assert(samples < ds);
         if (samples >= ds) samples = ds-1;

         _batch_imadd(sptr, sptr-samples, dmax-dmin, volume, 0.0f);
      }
      _aax_memcpy(reverb->reverb_history[track], sptr+dmax-ds, bytes);
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
void
bufEffectDelay(int32_ptr d, const int32_ptr s, int32_ptr scratch,
               unsigned int start, unsigned int end, unsigned int no_samples,
               unsigned int ds, void *data, unsigned int track)
{
   static const unsigned int bps = sizeof(int32_t);
   _aaxRingBufferDelayEffectData* effect = data;
   float volume;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(s != 0);
   assert(d != 0);
   assert(start < end);
   assert(data != NULL);

   volume =  effect->delay.gain;
   do
   {
      unsigned int offs, noffs;
      int32_t *sptr = s + start;
      int32_t *dptr = d + start;

      offs = effect->delay.sample_offs[track];
      assert(offs < ds);
      if (offs >= ds) offs = ds-1;

      if (start) {
         noffs = effect->curr_noffs[track];
      }
      else
      {
         noffs = (unsigned int)effect->lfo.get(&effect->lfo, s, track, end);
         effect->delay.sample_offs[track] = noffs;
         effect->curr_noffs[track] = noffs;
      }

      if (s == d)	/*  flanging */
      {
         unsigned int sign = (noffs < offs) ? -1 : 1;
         unsigned int doffs = abs(noffs - offs);
         unsigned int i = no_samples;
         unsigned int coffs = offs;
         unsigned int step = end;
         int32_t *ptr = dptr;

         if (start)
         {
            step = effect->curr_step[track];
            coffs = effect->curr_coffs[track];
         }
         else
         {
            if (doffs)
            {
               step = end/doffs;
               if (step < 2) step = end;
            }
         }
         effect->curr_step[track] = step;

         DBG_MEMCLR(1, s-ds, ds+start, bps);
         _aax_memcpy(sptr-ds, effect->delay_history[track], ds*bps);
         if (i >= step)
         {
//          float diff;
            do
            {
               _batch_imadd(ptr, ptr-coffs, step, volume, 0.0f);
#if 0
               /* gradually fade to the new value */
               diff = (float)*ptr - (float)*(ptr-1);
               *(ptr-1) += (6.0f/7.0f)*diff;
               *(ptr-2) += (5.0f/7.0f)*diff;
               *(ptr-3) += (4.0f/7.0f)*diff;
               *(ptr-4) += (3.0f/7.0f)*diff;
               *(ptr-5) += (2.0f/7.0f)*diff;
               *(ptr-6) += (1.0f/7.0f)*diff;
#endif
               ptr += step;
               coffs += sign;
               i -= step;
            }
            while(i >= step);
         }
         if (i) {
            _batch_imadd(ptr, ptr-coffs, i, volume, 0.0f);
         }

//       DBG_MEMCLR(1, effect->delay_history[track], ds, bps);
         _aax_memcpy(effect->delay_history[track], dptr+no_samples-ds, ds*bps);
         effect->curr_coffs[track] = coffs;
      }
      else	/* chorus, phasing */
      {
         int doffs = noffs - offs;
         float fact;

         fact = _MAX((float)((int)end-doffs)/(float)(end), 0.0f);
         if (fact == 1.0f) {
            _batch_imadd(dptr, sptr-offs, no_samples, volume, 0.0f);
         }
         else
         {
            DBG_MEMCLR(1, scratch-ds, ds+end, bps);
            _batch_resample(scratch-ds, sptr-offs, 0, no_samples, 0.0f, fact);
            _batch_imadd(dptr, scratch-ds, no_samples, volume, 0.0f);
         }
      }
   }
   while (0);
}

void
bufEffectDistort(int32_ptr d, const int32_ptr s,
                   unsigned int dmin, unsigned int dmax, unsigned int ds,
                   unsigned int track, void *data)
{
   _aaxFilterInfo *dist_effect = (_aaxFilterInfo*)data;
   _aaxRingBufferLFOData* lfo = dist_effect->data;
   float *params = dist_effect->param;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(s != 0);
   assert(d != 0);
   assert(data != 0);
   assert(dmin < dmax);
   assert(data != NULL);
   assert(track < _AAX_MAX_SPEAKERS);

   do
   {
      static const unsigned int bps = sizeof(int32_t);
      const int32_ptr sptr = s - ds + dmin;
      int32_t *dptr = d - ds + dmin;
      float clip, asym, fact, mix;
      unsigned int no_samples;
      float lfo_fact = 1.0;

      no_samples = dmax+ds-dmin;
      DBG_MEMCLR(1, d-ds, ds+dmax, bps);

      if (lfo) {
         lfo_fact = lfo->get(lfo, sptr, track, no_samples);
      }
      fact = params[AAX_DISTORTION_FACTOR]*lfo_fact;
      clip = params[AAX_CLIPPING_FACTOR];
      mix  = params[AAX_MIX_FACTOR]*lfo_fact;
      asym = params[AAX_ASYMMETRY];

      _aax_memcpy(dptr, sptr, no_samples*bps);
      if (mix > 0.01f)
      {
         float mix_factor;

         /* make dptr the wet signal */
         if (fact > 0.0013f) {
            _batch_imul_value(dptr, bps, no_samples, 1.0f+64.0f*fact);
         }

         if ((fact > 0.01f) || (asym > 0.01f))
         {
            unsigned int average = 0;
            unsigned int peak = no_samples;
            bufCompress(dptr, &average, &peak, clip, 4*asym);
         }

         /* mix with the dry signal */
         mix_factor = mix/(0.5f+powf(fact, 0.25f));
         _batch_imul_value(dptr, bps, no_samples, mix_factor);
         if (mix < 0.99f) {
            _batch_imadd(dptr, sptr, no_samples, 1.0f-mix, 0.0f);
         }
      }
   }
   while(0);
}

#endif /* !ENABLE_LITE */

void
bufFilterFrequency(int32_ptr d, const int32_ptr s,
                   unsigned int dmin, unsigned int dmax, unsigned int ds,
                   unsigned int track, void *data, unsigned char ctr)
{
   _aaxRingBufferFreqFilterData *filter = data;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(s != 0);
   assert(d != 0);
   assert(data != 0);
   assert(dmin < dmax);
   assert(data != NULL);
   assert(track < _AAX_MAX_SPEAKERS);

   if (filter->k)
   {
      const int32_ptr sptr = s - ds + dmin;
      int32_t *dptr = d - ds + dmin;
      float *hist = filter->freqfilter_history[track];
      float *cptr = filter->coeff;
      float lf = filter->lf_gain;
      float hf = filter->hf_gain;
      float k = filter->k;

      if (filter->lfo && !ctr)
      {
         float fc = _MAX(filter->lfo->get(filter->lfo, s, track, dmax), 1.0f);
         float Q = filter->Q;

         k = 1.0f;
         iir_compute_coefs(fc, filter->fs, cptr, &k, Q);
         filter->k = k;
      }

      _batch_freqfilter(dptr, sptr, dmax+ds-dmin, hist, lf, hf, k, cptr);
   }
}

void
iir_compute_coefs(float fc, float fs, float *coef, float *gain, float Q)
{
   float k = 1.0f;

   float a0 = 1.0f;
   float a1 = 0.0f;
   float a2 = 0.0f;
   float b0 = 1.0f;
   float b1 = 1.4142f / Q;
   float b2 = 1.0f;

   szxform(&a0, &a1, &a2, &b0, &b1, &b2, fc, fs, &k, coef);

   *gain = k;
}

/* -------------------------------------------------------------------------- */

/*
 * From: http://www.gamedev.net/reference/articles/article845.asp
 *       http://www.gamedev.net/reference/articles/article846.asp
 */
static void
bilinear(float a0, float a1, float a2, float b0, float b1, float b2,
             float *k, float fs, float *coef)
{
   float ad, bd;

   a2 *= (4.0f * fs*fs);
   b2 *= (4.0f * fs*fs);
   a1 *= (2.0f * fs);
   b1 *= (2.0f * fs);

   ad = a2 + a1 + a0;
   bd = b2 + b1 + b0;

   *k *= ad/bd;

   *coef++ = (-2.0f*b2 + 2.0f*b0) / bd;
   *coef++ = (b2 - b1 + b0) / bd;
   *coef++ = (-2.0f*a2 + 2.0f*a0) / ad;
   *coef   = (a2 - a1 + a0) / ad;
}

static void
szxform(float *a0, float *a1, float *a2, float *b0, float *b1, float *b2,
        float fc, float fs, float *k, float *coef)
{
   float wp;

   wp = 2.0f*fs * tanf(GMATH_PI * fc/fs);
   *a2 /= wp*wp;
   *b2 /= wp*wp;
   *a1 /= wp;
   *b1 /= wp;

   bilinear(*a0, *a1, *a2, *b0, *b1, *b2, k, fs, coef);
}


void
_aaxRingBufferDelaysAdd(void **data, float fs, unsigned int tracks, const float *delays, const float *gains, unsigned int num, float igain, float lb_depth, float lb_gain)
{
   _aaxRingBufferReverbData **ptr = (_aaxRingBufferReverbData**)data;
   _aaxRingBufferReverbData *reverb;

   assert(ptr != 0);
   assert(delays != 0);
   assert(gains != 0);

   if (*ptr == NULL) {
      *ptr = calloc(1, sizeof(_aaxRingBufferReverbData));
   }

   reverb = *ptr;
   if (reverb)
   {
      unsigned int j, snum = _AAX_MAX_SPEAKERS;

      if (reverb->history_ptr == 0) {
         _aaxRingBufferCreateHistoryBuffer(&reverb->history_ptr,
                                           reverb->reverb_history,
                                           fs, tracks, REVERB_EFFECTS_TIME);
      }

      if (num < _AAX_MAX_DELAYS)
      {
         unsigned int i;

         reverb->gain = igain;
         reverb->no_delays = num;
         for (i=0; i<num; i++)
         {
            if ((gains[i] > 0.001f) || (gains[i] < -0.001f))
            {
               for (j=0; j<snum; j++) {
                  reverb->delay[i].sample_offs[j] = (int)(delays[i] * fs);
               }
               reverb->delay[i].gain = gains[i];
            }
            else {
               reverb->no_delays--;
            }
         }
      }

      // http://www.sae.edu/reference_material/pages/Coefficient%20Chart.htm
      if ((num > 0) && (lb_depth != 0) && (lb_gain != 0))
      {
         static const float max_depth = REVERB_EFFECTS_TIME*0.6877777f;
         float dlb, dlbp;

         num = 5;
         reverb->loopback[0].gain = lb_gain*0.95015f;	// conrete/brick = 0.95
         reverb->loopback[1].gain = lb_gain*0.87075f;
         reverb->loopback[2].gain = lb_gain*0.91917f;
         reverb->loopback[3].gain = lb_gain*0.72317f;	// carpet     = 0.853
         reverb->loopback[4].gain = lb_gain*0.80317f;
         reverb->loopback[5].gain = lb_gain*0.73317f;
         reverb->loopback[6].gain = lb_gain*0.88317f;

         dlb = 0.01f+lb_depth*max_depth;
         dlbp = (REVERB_EFFECTS_TIME-dlb)*lb_depth;
         dlbp = _MINMAX(dlbp, 0.01f, REVERB_EFFECTS_TIME-0.01f);
//       dlbp = 0;

         dlb *= fs;
         dlbp *= fs;
         reverb->no_loopbacks = num;
         for (j=0; j<num; j++)
         {
            reverb->loopback[0].sample_offs[j] = (dlbp + dlb*0.9876543f);
            reverb->loopback[1].sample_offs[j] = (dlbp + dlb*0.4901861f);
            reverb->loopback[2].sample_offs[j] = (dlbp + dlb*0.3333333f);
            reverb->loopback[3].sample_offs[j] = (dlbp + dlb*0.2001743f);
            reverb->loopback[4].sample_offs[j] = (dlbp + dlb*0.1428571f);
            reverb->loopback[5].sample_offs[j] = (dlbp + dlb*0.0909091f);
            reverb->loopback[6].sample_offs[j] = (dlbp + dlb*0.0769231f);
         }
      }
      *data = reverb;
   }
}

void
_aaxRingBufferDelaysRemove(void **data)
{
   _aaxRingBufferReverbData *reverb;

   assert(data != 0);

   reverb = *data;
   if (reverb)
   {
      reverb->no_delays = 0;
      reverb->no_loopbacks = 0;
      reverb->delay[0].gain = 1.0f;
      free(reverb->history_ptr);
      free(reverb->freq_filter);
      reverb->freq_filter = 0;
      reverb->history_ptr = 0;
   }
}

#if 0
void
_aaxRingBufferDelayRemoveNum(_aaxRingBuffer *rb, unsigned int n)
{
   unsigned int size;

   assert(rb);

   if (rb->reverb)
   {
      assert(n < rb->reverb->no_delays);

      rb->reverb->no_delays--;
      size = rb->reverb->no_delays - n;
      size *= sizeof(_aaxRingBufferDelayInfo);
      memcpy(&rb->reverb->delay[n], &rb->reverb->delay[n+1], size);
   }
}
#endif

void
_aaxRingBufferCreateHistoryBuffer(void **hptr, int32_t *history[_AAX_MAX_SPEAKERS], float frequency, int tracks, float dde)
{
   unsigned int bps, size;
   char *ptr, *p;
   int i;

   bps = sizeof(int32_t);
   size = (unsigned int)ceilf(dde * frequency);
   size *= bps;
#if BYTE_ALIGN
   if (size & 0xF)
   {
      size |= 0xF;
      size++;
   }
   p = 0;
   ptr = _aax_calloc(&p, tracks, size);
#else
   ptr = p = calloc(tracks, size);
#endif
   if (ptr)
   {
      *hptr = ptr;
      for (i=0; i<tracks; i++)
      {
         history[i] = (int32_t*)p;
         p += size;
      }
   }
}


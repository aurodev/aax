/*
 * Copyright 2005-2011 by Erik Hofman.
 * Copyright 2009-2011 by Adalin B.V.
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
#if HAVE_STRINGS_H
# include <strings.h>
#endif
#include <math.h>

#include <api.h>
#include <arch.h>
#include <ringbuffer.h>
#include <base/types.h>
#include <base/logging.h>

#include "audio.h"

#ifndef _DEBUG
# define _DEBUG		0
#endif

static unsigned int _oalGetSetMonoSources(unsigned int, int);
static void _oalRingBufferIMA4ToPCM16(int32_t **__restrict,const void *__restrict,int,int,unsigned int);

int
_oalRingBufferIsValid(_oalRingBuffer *rb)
{
   int rv = 0;
   if (rb && rb->sample && rb->sample->track) {
      rv = -1;
   }
   return rv;
}


_oalRingBuffer *
_oalRingBufferCreate(float dde)
{
   _oalRingBuffer *rb;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   rb = (_oalRingBuffer *)calloc(1, sizeof(_oalRingBuffer));
   if (rb)
   {
      _oalRingBufferSample *rbd;

      rbd = (_oalRingBufferSample *)calloc(1, sizeof(_oalRingBufferSample));
      if (rbd)
      {
         float ddesamps;
         int format;
         /*
          * fill in the defaults
          */
         rb->sample = rbd;
         rbd->ref_counter = 1;

         rb->playing = 0;
         rb->stopped = 1;
         rb->streaming = 0;
         rb->dde_sec = dde;
         rb->pitch_norm = 1.0f;
         rb->format = AAX_PCM16S;

         format = rb->format;
         rbd->no_tracks = 1;
         rbd->frequency_hz = 44100.0f;
         rbd->codec = _oalRingBufferCodecs[format];
         rbd->bytes_sample = _oalRingBufferFormat[format].bits/8;

         ddesamps = ceilf(dde * rbd->frequency_hz);
         rbd->dde_samples = (unsigned int)ddesamps;
         rbd->scratch = NULL;
      }
      else
      {
         free(rb);
         rb = NULL;
      }
   }

   return rb;
}

static void
_oalRingBufferInitTracks(_oalRingBuffer *rb)
{
   _oalRingBufferSample *rbd;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != NULL);

   rbd = rb->sample;

   if (!rbd->track)
   {
      unsigned int tracks, no_samples, tracksize, dde_bytes;
      char *ptr, *ptr2;
      char bps;

      bps = rbd->bytes_sample;
      no_samples = rbd->no_samples_avail;
      dde_bytes = TEST_FOR_TRUE(rb->dde_sec) ? (rbd->dde_samples * bps) : 0;
      tracksize = dde_bytes + (no_samples + 0xF) * bps;

      /*
       * Create one buffer that can hold the data for all channels.
       * The first bytes are reserved for the track pointers
       */
#if BYTE_ALIGN
      /* 16-byte align every buffer */
      if (tracksize & 0xF)
      {
         tracksize |= 0xF;
         tracksize++;
      }

      tracks = rbd->no_tracks;
      ptr2 = (char*)(tracks * sizeof(void*));
      ptr = _aax_calloc(&ptr2, tracks, sizeof(void*) + tracksize);
#else
      ptr = ptr2 = calloc(tracks, sizeof(void*) + tracksize);
#endif
      if (ptr)
      {
         unsigned int i;

         rbd->track_len_bytes = no_samples * bps;
         rbd->duration_sec = (float)no_samples / rbd->frequency_hz;

         rbd->loop_start_sec = 0.0f;
         rbd->loop_end_sec = rbd->duration_sec;

         free(rbd->track);
         rbd->track = (void **)ptr;
         for (i=0; i<rbd->no_tracks; i++)
         {
            rbd->track[i] = ptr2 + dde_bytes;
            ptr2 += tracksize;
         }
      }
   }
}

void
_oalRingBufferInit(_oalRingBuffer *rb, char add_scratchbuf)
{
   _oalRingBufferSample *rbd;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != NULL);

   rbd = rb->sample;
   _oalRingBufferInitTracks(rb);
   rbd->no_samples = rbd->no_samples_avail;

   if (add_scratchbuf && rbd->scratch == NULL)
   {
      unsigned int i, tracks, no_samples, tracksize, dde_bytes;
      char *ptr, *ptr2;
      char bps;

      bps = rbd->bytes_sample;
      no_samples = rbd->no_samples_avail;
      dde_bytes = TEST_FOR_TRUE(rb->dde_sec) ? (rbd->dde_samples*bps) : 0;
      tracksize = 2*dde_bytes + no_samples*bps;
      tracks = MAX_SCRATCH_BUFFERS;

#if BYTE_ALIGN
      /* 16-byte align every buffer */
      if (tracksize & 0xF)
      {
         tracksize |= 0xF;
         tracksize++;
      }
      ptr2 = (char*)(tracks * sizeof(void*));
      ptr = _aax_calloc(&ptr2, tracks, sizeof(void*) + tracksize);
#else
      ptr = ptr2 = calloc(tracks, sizeof(void*) + tracksize);
#endif
      if (ptr)
      {
         rbd->scratch = (void **)ptr;
         for (i=0; i<tracks; i++)
         {
            rbd->scratch[i] = ptr2 + dde_bytes;
            ptr2 += tracksize;
         }
      }
   }
}


_oalRingBuffer *
_oalRingBufferReference(_oalRingBuffer *ringbuffer)
{
   _oalRingBuffer *rb;

   assert(ringbuffer != 0);

   rb = malloc(sizeof(_oalRingBuffer));
   if (rb)
   {
      memcpy(rb, ringbuffer, sizeof(_oalRingBuffer));
      rb->sample->ref_counter++;
      // rb->looping = 0;
      rb->playing = 0;
      rb->stopped = 1;
      rb->streaming = 0;
      rb->elapsed_sec = 0.0f;
      rb->curr_pos_sec = 0.0f;
      rb->curr_sample = 0;
// TODO: Is this really what we want? Looks suspiceous to me
      rb->sample->scratch = NULL;
   }

   return rb;
}

_oalRingBuffer *
_oalRingBufferDuplicate(_oalRingBuffer *ringbuffer, char copy, char dde)
{
   _oalRingBuffer *rb;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(ringbuffer != 0);

   rb = _oalRingBufferCreate(ringbuffer->dde_sec);
   if (rb)
   {
      _oalRingBufferSample *rbsd, *rbdd;
      char add_scratchbuf;

      rbdd = rb->sample;
      rbsd = ringbuffer->sample;

      /* set format */
      rb->format = ringbuffer->format;
      rbdd->codec = rbsd->codec;
      rbdd->bytes_sample = rbsd->bytes_sample;

      _oalRingBufferSetNoTracks(rb, rbsd->no_tracks);
      _oalRingBufferSetFrequency(rb, rbsd->frequency_hz);
      _oalRingBufferSetNoSamples(rb, rbsd->no_samples_avail);

      /*set looping */
      rbdd->loop_start_sec = rbsd->loop_start_sec;
      rbdd->loop_end_sec = rbsd->loop_end_sec;

      add_scratchbuf = (copy && rbsd->scratch) ? AAX_TRUE : AAX_FALSE;
      _oalRingBufferInit(rb, add_scratchbuf);
      _oalRingBufferSetNoSamples(rb, rbsd->no_samples);
      if (copy) {
         _oalRingBufferFillNonInterleaved(rb, rbsd->track, 1, ringbuffer->looping);
      }
      else
      {
         rbdd->scratch = rbsd->scratch;
         rbsd->scratch = NULL;
      }
      if (dde) {
         _oalRingBufferCopyDelyEffectsData(rb, ringbuffer);
      }
   }

   return rb;
}

void
_oalRingBufferFillNonInterleaved(_oalRingBuffer *rb, const void *data, unsigned blocksize, char looping)
{
   unsigned int t, tracksize;
   _oalRingBufferSample *rbd;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   rbd = rb->sample;
   rb->looping = looping;

   tracksize = rbd->no_samples * rbd->bytes_sample;
   switch (rb->format)
   {
   case AAX_IMA4_ADPCM:
   {
#if 1
      printf("WARNING: AAX_IMA4_ADPCM not implemented for _oalRingBufferFillNonInterleaved\n");
      exit(-1);
#else
      int t;
      for (t=0; t<no_tracks; t++)
      {
         int32_t **track = &tracks[t];
         _oalRingBufferIMA4ToPCM16(track, data, 1, blocksize, no_samples);
      }
      rb->format = AAX_PCM16S;
#endif
      break;
   }
   default:
      for (t=0; t<rbd->no_tracks; t++)
      {
         char *s, *d;

         s = (char *)data + t*tracksize;
         d = (char *)rbd->track[t];
         _aax_memcpy(d, s, tracksize);
      }
   }

#if 0
   if (looping) {
      _oalRingBufferAddLooping(rb);
   }
#endif

   rb->playing = 0;
   rb->stopped = 1;
   rb->streaming = 0;
   rb->elapsed_sec = 0.0f;
   rb->curr_pos_sec = 0.0f;
   rb->curr_sample = 0;
   rb->looping = looping;
}

void
_oalRingBufferFillInterleaved(_oalRingBuffer *rb, const void *data, unsigned blocksize, char looping)
{
   unsigned int bps, no_samples, no_tracks, tracksize;
   _oalRingBufferSample *rbd;
   int32_t **tracks;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);
   assert(data != 0);

   rbd = rb->sample;
   rb->looping = looping;

   bps = rbd->bytes_sample;
   no_tracks = rbd->no_tracks;
   no_samples = rbd->no_samples;
   tracksize = no_samples * bps;
   tracks = (int32_t **)rbd->track;

   switch (rb->format)
   {
   case AAX_IMA4_ADPCM:
      _oalRingBufferIMA4ToPCM16(tracks, data, no_tracks, blocksize, no_samples);
      break;
   case AAX_PCM32S:
      _batch_cvt24_32_intl(tracks, data, 0, no_tracks, no_samples);
      
      break;
   case AAX_FLOAT:
      _batch_cvt24_ps_intl(tracks, data, 0, no_tracks, no_samples);
      break;
   case AAX_DOUBLE:
      _batch_cvt24_pd_intl(tracks, data, 0, no_tracks, no_samples);
      break;
   default:
      if (rbd->no_tracks == 1) {
         _aax_memcpy(rbd->track[0], data, tracksize);
      }
      else /* stereo */
      {
         unsigned int frame_size = no_tracks * bps;
         unsigned int t;

         for (t=0; t<no_tracks; t++)
         {
            char *sptr, *dptr;
            unsigned int i;

            sptr = (char *)data + t*bps;
            dptr = (char *)rbd->track[t];
            i = no_samples;
            do
            {
               memcpy(dptr, sptr, bps);
               sptr += frame_size;
               dptr += bps;
            }
            while (--i);
         }
      }
   } /* switch */

#if 0
   if (looping) {
      _oalRingBufferAddLooping(rb);
   }
#endif

   rb->playing = 0;
   rb->stopped = 1;
   rb->streaming = 0;
   rb->elapsed_sec = 0.0f;
   rb->curr_pos_sec = 0.0f;
   rb->curr_sample = 0;
   rb->looping = looping;
}


void
_oalRingBufferGetDataInterleaved(_oalRingBuffer *rb, void* data, unsigned int samples, float fact)
{
   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   if (data)
   {
      _oalRingBufferSample *rbd = rbd = rb->sample;
      unsigned int t, no_tracks = rbd->no_tracks;
      unsigned int no_samples = rbd->no_samples;
      unsigned char bps = rbd->bytes_sample;
      void **ptr, **track = rbd->track;

      assert(samples >= (unsigned int)(fact*no_samples));

      fact = 1.0f/fact;
      ptr = track;
      if (fact != 1.0f)
      {
         unsigned int size = samples*bps;
         char *p;
         
         if (bps == sizeof(int32_t))
         {
            p = (char*)(no_tracks*sizeof(void*));
            track = (void**)_aax_malloc(&p, no_tracks*(sizeof(void*) + size));
            for (t=0; t<no_tracks; t++)
            {
               track[t] = p;
               _aaxProcessResample(track[t], ptr[t], 0, samples, 0, fact);
               p += size;
            }
         }
         else
         {
            unsigned int scratch_size;
            int32_t **scratch;
            char *sptr;

            scratch_size = 2*sizeof(int32_t*);
            sptr = (char*)scratch_size;
           
            scratch_size += (no_samples+samples)*sizeof(int32_t);
            scratch = (int32_t**)_aax_malloc(&sptr, scratch_size);
            scratch[0] = (int32_t*)sptr;
            scratch[1] = (int32_t*)(sptr + no_samples*sizeof(int32_t));

            p = (char*)(no_tracks*sizeof(void*));
            track = (void**)_aax_malloc(&p, no_tracks*(sizeof(void*) + size));
            for (t=0; t<no_tracks; t++)
            {
               track[t] = p;
               bufConvertDataToPCM24S(scratch[0], ptr[t], no_samples,
                                     rb->format);
               _aaxProcessResample(scratch[1], scratch[0], 0, samples, 0, fact);
               bufConvertDataFromPCM24S(track[t], scratch[1], 1, samples, 
                                        rb->format, 1);
               p += size;
            }
            free(scratch);
         }
      }

      if (no_tracks == 1) {
         _aax_memcpy(data, track[0], samples*bps);
      }
      else
      {
         for (t=0; t<no_tracks; t++)
         {
            uint8_t *s = (uint8_t*)track[t];
            uint8_t *d = (uint8_t *)data + t*bps;
            unsigned int i =  samples;
            do
            {
               memcpy(d, s, bps);
               d += no_tracks*bps;
               s += bps;
            }
            while (--i);
         }
      }

      if (ptr != track) free(track);
   }
}

void*
_oalRingBufferGetDataInterleavedMalloc(_oalRingBuffer *rb, float fact)
{
   unsigned int samples;
   void *data;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);
  
   samples = (unsigned int)(fact*rb->sample->no_samples);
   data = malloc(rb->sample->no_tracks * samples*rb->sample->bytes_sample);
   if (data) {
      _oalRingBufferGetDataInterleaved(rb, data, samples, fact);
   }

   return data;
}

void
_oalRingBufferGetDataNonInterleaved(_oalRingBuffer *rb, void *data, unsigned int samples, float fact)
{
   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   if (data)
   {
      _oalRingBufferSample *rbd = rbd = rb->sample;
      unsigned int t, no_tracks = rbd->no_tracks;
      unsigned int no_samples = rbd->no_samples;
      unsigned char bps = rbd->bytes_sample;
      void **ptr, **track = rbd->track;
      char *p = data;

      assert(samples >= (unsigned int)(fact*no_samples));

      fact = 1.0f/fact;
      ptr = track;
      if (fact != 1.0f)
      {
         unsigned int size = samples*bps;
         char *p;

         if (bps == sizeof(int32_t))
         {
            p = (char*)(no_tracks*sizeof(void*));
            track = (void**)_aax_malloc(&p, no_tracks*(sizeof(void*) + size));
            for (t=0; t<no_tracks; t++)
            {
               track[t] = p;
               _aaxProcessResample(track[t], ptr[t], 0, samples, 0, fact);
               p += size;
            }
         }
         else
         {
            unsigned int scratch_size;
            int32_t **scratch;
            char *sptr;

            scratch_size = 2*sizeof(int32_t*);
            sptr = (char*)scratch_size;

            scratch_size += (no_samples+samples)*sizeof(int32_t);
            scratch = (int32_t**)_aax_malloc(&sptr, scratch_size);
            scratch[0] = (int32_t*)sptr;
            scratch[1] = (int32_t*)(sptr + no_samples*sizeof(int32_t));

            p = (char*)(no_tracks*sizeof(void*));
            track = (void**)_aax_malloc(&p, no_tracks*(sizeof(void*) + size));
            for (t=0; t<no_tracks; t++)
            {
               track[t] = p;
               bufConvertDataToPCM24S(scratch[0], ptr[t], no_samples,
                                     rb->format);
               _aaxProcessResample(scratch[1], scratch[0], 0, samples, 0, fact);
               bufConvertDataFromPCM24S(track[t], scratch[1], 1, samples,
                                        rb->format, 1);
               p += size;
            }
            free(scratch);
         }
      }

      p = data;
      for (t=0; t<no_tracks; t++)
      {
         _aax_memcpy(p, rbd->track[t], rbd->track_len_bytes);
         p += rbd->track_len_bytes;
      }

      if (ptr != track) free(track);
   }
}

void*
_oalRingBufferGetDataNonInterleavedMalloc(_oalRingBuffer *rb, float fact)
{
   unsigned int samples;
   void *data;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   samples = (unsigned int)(fact*rb->sample->no_samples);
   data = malloc(rb->sample->no_tracks * samples*rb->sample->bytes_sample);
   if (data) {
      _oalRingBufferGetDataNonInterleaved(rb, data, samples, fact);
   }

   return data;
}

void
_oalRingBufferClear(_oalRingBuffer *rb)
{
   _oalRingBufferSample *rbd;
   unsigned int i;

   /* _AAX_LOG(LOG_DEBUG, __FUNCTION__); */

   assert(rb != 0);

   rbd = rb->sample;
   assert(rbd != 0);
   assert(rbd->track);

   for (i=0; i<rbd->no_tracks; i++) {
      memset((void *)rbd->track[i], 0, rbd->track_len_bytes);
   }

// rb->sample = rbd;
// rb->reverb = reverb;

   rb->elapsed_sec = 0.0f;
   rb->pitch_norm = 1.0f;
   rb->volume_norm = 1.0f;

   rb->curr_pos_sec = 0.0f;
   rb->curr_sample = 0;

// rb->format = fmt;
   rb->playing = 0;
   rb->stopped = 1;
   rb->looping = 0;
   rb->streaming = 0;
// rb->dde_sec = dde;
}

_oalRingBuffer *
_oalRingBufferDelete(_oalRingBuffer *rb)
{
   _oalRingBufferSample *rbd;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   rbd = rb->sample;
   if (rbd && rbd->ref_counter > 0)
   {
      if (--rbd->ref_counter == 0)
      {
         free(rbd->track);
         rbd->track = NULL;

         free(rbd->scratch);
         rbd->scratch = NULL;

         free(rb->sample);
         rb->sample = NULL;
      }
      free(rb);
   }
   return NULL;
}

void
_oalRingBufferRewind(_oalRingBuffer *rb)
{
   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);

   rb->curr_pos_sec = 0.0f;
   rb->curr_sample = 0;
}


void
_oalRingBufferForward(_oalRingBuffer *rb)
{
   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);

   rb->curr_pos_sec = rb->sample->duration_sec;
   rb->curr_sample = rb->sample->no_samples;
}


void
_oalRingBufferStart(_oalRingBuffer *rb)
{
   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);

// rb->playing = 1;
   rb->stopped = 0;
   rb->streaming = 0;
}


void
_oalRingBufferStop(_oalRingBuffer *rb)
{
   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);

// rb->playing = 0;
   rb->stopped = 1;
   rb->streaming = 0;
}


void
_oalRingBufferStartStreaming(_oalRingBuffer *rb)
{
   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);

// rb->playing = 1;
   rb->stopped = 0;
   rb->streaming = 1;
}

void
_oalRingBufferCopyDelyEffectsData(_oalRingBuffer *d, const _oalRingBuffer *s)
{
   _oalRingBufferSample *srbd, *drbd;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(s);
   assert(d);

   srbd = s->sample;
   drbd = d->sample;

   if (srbd->bytes_sample == drbd->bytes_sample)
   {
      unsigned int t, tracks, ds, bps;

      ds = srbd->dde_samples;
      if (ds > drbd->dde_samples) {
         ds = drbd->dde_samples;
      }

      tracks = srbd->no_tracks;
      if (tracks > drbd->no_tracks) {
         tracks = drbd->no_tracks;
      }

      bps = srbd->bytes_sample;
      for (t=0; t<tracks; t++)
      {
         int32_t *sptr = srbd->track[t];
         int32_t *dptr = drbd->track[t];
         _aax_memcpy(dptr-ds, sptr-ds, ds*bps);
      }
   }
}


void
_oalRingBufferSetFrequency(_oalRingBuffer *rb, float freq)
{
   _oalRingBufferSample *rbd;
   float ddesamps;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   rbd = rb->sample;
   rbd->duration_sec = rbd->no_samples / freq;
   rbd->loop_start_sec *= (rbd->frequency_hz / freq);
   rbd->loop_end_sec *= (rbd->frequency_hz / freq);
   rbd->frequency_hz = freq;

   ddesamps = ceilf(rb->dde_sec * rbd->frequency_hz);
   rbd->dde_samples = (unsigned int)ddesamps;
}

int
_oalRingBufferSetNoSamples(_oalRingBuffer *rb, unsigned int no_samples)
{
   _oalRingBufferSample *rbd;
   int rv = AAX_FALSE;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   rbd = rb->sample;
   if (rbd->track == NULL)
   {
      rbd->no_samples_avail = no_samples;
      rbd->duration_sec = (float)no_samples / rbd->frequency_hz;
      rv = AAX_TRUE;
   }
   else if (no_samples <= rbd->no_samples_avail)
   {
      /**
       * Note:
       * Sensors rely in the fact that resizing to a smaller bufferr-size does
       * not alter the actual buffer size, so it can overrun if required.
       */
      rbd->no_samples = no_samples;
      rbd->duration_sec = (float)no_samples / rbd->frequency_hz;
      rv = AAX_TRUE;
   }
   else if (no_samples > rbd->no_samples_avail)
   {
      rbd->no_samples_avail = no_samples;
      _oalRingBufferInitTracks(rb);
      rv = AAX_TRUE;
   }
#ifndef NDEBUG
   else if (rbd->track == NULL) {
      printf("%s: Can't set value when rbd->track != NULL\n", __FUNCTION__);
   } else {
      printf("%s: Unknown error\n", __FUNCTION__);
   }
#endif

   return rv;
}

int
_oalRingBufferSetTrackSize(_oalRingBuffer *rb, unsigned int size)
{
   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   return _oalRingBufferSetNoSamples(rb, size/rb->sample->bytes_sample);  
}

int
_oalRingBufferSetDuration(_oalRingBuffer *rb, float duration)
{
   float freq;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   freq = rb->sample->frequency_hz;
   return _oalRingBufferSetNoSamples(rb, rintf(duration * freq));
}


void
_oalRingBufferSetBytesPerSample(_oalRingBuffer *rb, unsigned char bps)
{
   _oalRingBufferSample *rbd;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   rbd = rb->sample;
   if (rbd->track == NULL) {
      rbd->bytes_sample = bps;
   }
#ifndef NDEBUG
   else printf("%s: Can't set value when rbd->track != NULL\n", __FUNCTION__);
#endif
}

void
_oalRingBufferSetNoTracks(_oalRingBuffer *rb, unsigned char no_tracks)
{
   _oalRingBufferSample *rbd;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   rbd = rb->sample;
   if (rbd->track == NULL) {
      rbd->no_tracks = no_tracks;
   }
#ifndef NDEBUG
   else printf("%s: Can't set value when rbd->track != NULL\n", __FUNCTION__);
#endif
}

void
_oalRingBufferSetFormat(_oalRingBuffer *rb, _aaxCodec **codecs, enum aaxFormat format)
{
   _oalRingBufferSample *rbd;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(format < AAX_FORMAT_MAX);

   rbd = rb->sample;
   if (rbd->track == NULL)
   {
      if (codecs == 0) {
         codecs = _oalRingBufferCodecs;
      }
      rb->format = format;
      rbd->codec = codecs[format];
      rbd->bytes_sample = _oalRingBufferFormat[format].bits/8;
   }
#ifndef NDEBUG
   else printf("%s: Can't set value when rbd->track != NULL\n", __FUNCTION__);
#endif

}

void
_oalRingBufferSetLooping(_oalRingBuffer *rb, unsigned char loops)
{
   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   rb->looping = loops;
#if 0
   if (loops) {
      _oalRingBufferAddLooping(rb);
   }
#endif
}

void
_oalRingBufferSetLoopPoints(_oalRingBuffer *rb, float start, float end)
{
   _oalRingBufferSample *rbd;
   int looping = AAX_FALSE;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   rbd = rb->sample;
   if ((start < end) && (end <= rbd->no_samples)) {
      looping = AAX_TRUE;
   }

   if (looping)
   {
      rbd->loop_start_sec = start/rbd->frequency_hz;
      rbd->loop_end_sec = end/rbd->frequency_hz;
//    _oalRingBufferAddLooping(rb);
   }
}

void
_oalRingBufferSetOffsetSec(_oalRingBuffer *rb, float pos)
{
   _oalRingBufferSample *rbd;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   rbd = rb->sample;
   if (pos > rbd->duration_sec) {
      pos = rbd->duration_sec;
   }
   rb->curr_pos_sec = pos;
   rb->curr_sample = rintf(pos * rbd->frequency_hz);
}

void
_oalRingBufferSetOffsetSamples(_oalRingBuffer *rb, unsigned int pos)
{
   _oalRingBufferSample *rbd;
   float pos_sec;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(rb != 0);
   assert(rb->sample != 0);

   rbd = rb->sample;
   if (pos > rbd->no_samples) {
      pos = rbd->no_samples;
   }
   pos_sec = (float)pos / rbd->frequency_hz;
   rb->curr_pos_sec = pos_sec;
   rb->curr_sample = pos;
}

float
_oalRingBufferGetFrequency(const _oalRingBuffer *rb)
{
   assert(rb != 0);
   assert(rb->sample != 0);

   return rb->sample->frequency_hz;
}

float
_oalRingBufferGetDuration(const _oalRingBuffer *rb)
{
   assert(rb != 0);
   assert(rb->sample != 0);

   return rb->sample->duration_sec;
}

unsigned int
_oalRingBufferGetTrackSize(const _oalRingBuffer *rb)
{
   assert(rb != 0);
   assert(rb->sample != 0);

   return rb->sample->track_len_bytes;
}

unsigned char
_oalRingBufferGetBytesPerSample(const _oalRingBuffer *rb)
{
   assert(rb != 0);
   assert(rb->sample != 0);

   return rb->sample->bytes_sample;
}

unsigned int
_oalRingBufferGetNoSamples(const _oalRingBuffer *rb)
{
   assert(rb != 0);
   assert(rb->sample != 0);

   return rb->sample->no_samples;
}


unsigned char
_oalRingBufferGetNoTracks(const _oalRingBuffer *rb)
{
   assert(rb != 0);
   assert(rb->sample != 0);

   return rb->sample->no_tracks;
}

enum aaxFormat
_oalRingBufferGetFormat(const _oalRingBuffer* rb)
{
   assert(rb != 0);

   return _oalRingBufferFormat[rb->format].format;
}

unsigned char
_oalRingBufferGetLooping(const _oalRingBuffer *rb)
{
   assert(rb != 0);
   assert(rb->sample != 0);

   return rb->looping;
}

void
_oalRingBufferGetLoopPoints(const _oalRingBuffer *rb, unsigned int*s, unsigned int *e)
{
   if (s && e)
   {
      _oalRingBufferSample *rbd = rb->sample;
      *s = (unsigned int)(rbd->loop_start_sec * rbd->frequency_hz);
      *e = (unsigned int)(rbd->loop_end_sec * rbd->frequency_hz);
   }
}

float
_oalRingBufferGetOffsetSec(const _oalRingBuffer *rb)
{
   assert(rb != 0);

   return rb->curr_pos_sec;
}

unsigned int
_oalRingBufferGetOffsetSamples(const _oalRingBuffer *rb)
{
   assert(rb != 0);
   assert(rb->sample != 0);

   return rb->curr_sample;
}

char
_oalRingBufferTestPlaying(const _oalRingBuffer *rb)
{
   assert(rb != 0);
   return !(rb->playing == 0 && rb->stopped == 1);
}

void
_oalRingBufferDelaysAdd(void **data, float fs, unsigned int tracks, const float *delay, const float *gain, unsigned int num, float i_gain, float lb, float lb_gain)
{
   _oalRingBufferReverbData **ptr = (_oalRingBufferReverbData**)data;
   _oalRingBufferReverbData *reverb;

   assert(ptr != 0);
   assert(delay != 0);
   assert(gain != 0);

   if (*ptr == NULL) {
      *ptr = calloc(1, sizeof(_oalRingBufferReverbData));
   }

   reverb = *ptr;
   if (reverb)
   {
      unsigned int j, snum = _AAX_MAX_SPEAKERS;

      if (reverb->history_ptr == 0) {
         _oalRingBufferCreateHistoryBuffer(&reverb->history_ptr,
                                           reverb->reverb_history,
                                           fs, tracks, REVERB_EFFECTS_TIME);
      }

      if (num < _AAX_MAX_DELAYS)
      {
         unsigned int i;

         reverb->gain = i_gain;
         reverb->no_delays = num;
         for (i=0; i<num; i++)
         {
            if ((gain[i] > 0.001f) || (gain[i] < -0.001f))
            {
               for (j=0; j<snum; j++) {
                  reverb->delay[i].sample_offs[j] = (int)(delay[i] * fs);
               }
               reverb->delay[i].gain = gain[i];
            }
            else {
               reverb->no_delays--;
            }
         }
      }

      // http://www.sae.edu/reference_material/pages/Coefficient%20Chart.htm
      if ((num > 0) && (lb != 0) && (lb_gain != 0))
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

         dlb = 0.01f+lb*max_depth;
         dlbp = (REVERB_EFFECTS_TIME-dlb)*lb;
         dlbp = _MINMAX(dlbp, 0.01f, REVERB_EFFECTS_TIME-0.01f);
//       dlbp = 0;

         dlb *= fs;
         dlbp *= fs;
         reverb->no_loopbacks = num;
         for (j=0; j<num; j++)
         {
            reverb->loopback[0].sample_offs[j] = dlbp + dlb*0.9876543f;
            reverb->loopback[1].sample_offs[j] = dlbp + dlb*0.4901861f;
            reverb->loopback[2].sample_offs[j] = dlbp + dlb*0.3333333f;
            reverb->loopback[3].sample_offs[j] = dlbp + dlb*0.2001743f;
            reverb->loopback[4].sample_offs[j] = dlbp + dlb*0.1428571f;
            reverb->loopback[5].sample_offs[j] = dlbp + dlb*0.0909091f;
            reverb->loopback[6].sample_offs[j] = dlbp + dlb*0.0769231f;
         }
      }
      *data = reverb;
   }
}

void
_oalRingBufferDelaysRemove(void **data)
{
   _oalRingBufferReverbData *reverb;

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
_oalRingBufferDelayRemoveNum(_oalRingBuffer *rb, unsigned int n)
{
   unsigned int size;

   assert(rb);

   if (rb->reverb)
   {
      assert(n < rb->reverb->no_delays);

      rb->reverb->no_delays--;
      size = rb->reverb->no_delays - n;
      size *= sizeof(_oalRingBufferDelayInfo);
      memcpy(&rb->reverb->delay[n], &rb->reverb->delay[n+1], size);
   }
}
#endif

unsigned int
_oalRingBufferGetNoSources()
{
   int num = _oalGetSetMonoSources(0, 0);
   if (num > _AAX_MAX_MIXER_REGISTERED) num = _AAX_MAX_MIXER_REGISTERED;
   return num;
}

unsigned int
_oalRingBufferSetNoSources(unsigned int max)
{
   return _oalGetSetMonoSources(max, 0);
}

unsigned int
_oalRingBufferGetSource() {
   return _oalGetSetMonoSources(0, 1);
}

unsigned int
_oalRingBufferPutSource() {
   return _oalGetSetMonoSources(0, -1);
}

void
_oalRingBufferCreateHistoryBuffer(void **hptr, int32_t *history[_AAX_MAX_SPEAKERS], float frequency, int tracks, float dde)
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
/* -------------------------------------------------------------------------- */

_oalFormat_t _oalRingBufferFormat[AAX_FORMAT_MAX] =
{
  {  8, AAX_PCM8S },	/* 8-bit  */
  { 16, AAX_PCM16S },	/* 16-bit */
  { 32, AAX_PCM24S },	/* 24-bit */
  { 32, AAX_PCM24S },	/* 32-bit gets converted to 24-bit */
  { 32, AAX_PCM24S },	/* float gets converted to 24-bit  */
  { 32, AAX_PCM24S },	/* double gets converted to 24-bit */
  {  8, AAX_MULAW },	/* mu-law  */
  {  8, AAX_ALAW },	/* a-law */
  { 16, AAX_PCM16S }	/* IMA4-ADPCM gets converted to 16-bit */
};


float _lin(float v) { return v; }
float _square(float v) { return v*v; }
float _lin2log(float v) { return log10f(v); }
float _log2lin(float v) { return powf(10.0f,v); }
float _lin2db(float v) { return 20.0f*log10f(v); }
float _db2lin(float v) { return _MINMAX(powf(10.0f,v/20.0f),0.0f,10.0f); }
float _rad2deg(float v) { return v*GMATH_RAD_TO_DEG; }
float _deg2rad(float v) { return fmodf(v, 360.0f)*GMATH_DEG_TO_RAD; }
float _cos_deg2rad_2(float v) { return cosf(_deg2rad(v)/2); }
float _2acos_rad2deg(float v) { return 2*acosf(_rad2deg(v)); }
float _cos_2(float v) { return cosf(v/2); }
float _2acos(float v) { return 2*acosf(v); }

float _linear(float v, float f) { return v*f; }
float _compress(float v, float f) { return pow(f, 1.0f-v); }

static unsigned int
_oalGetSetMonoSources(unsigned int max, int num)
{
   static unsigned int _max_sources = _AAX_MAX_SOURCES_AVAIL;
   static unsigned int _sources = _AAX_MAX_SOURCES_AVAIL;
   unsigned int abs_num = abs(num);
   unsigned int ret = _sources;

   if (max)
   {
      if (max > _AAX_MAX_SOURCES_AVAIL) max = _AAX_MAX_SOURCES_AVAIL;
      _max_sources = max;
      _sources = max;
      ret = max;
   }

   if (abs_num && (abs_num < _AAX_MAX_MIXER_REGISTERED))
   {
      unsigned int _src = _sources - num;
      if ((_sources >= (unsigned int)num) && (_src < _max_sources))
      {
         _sources = _src;
         ret = abs_num;
      }
   }

   return ret;
}

/*
 * Low Frequency Oscilator funtions
 *
 * Internally the oscillator always runs between 0.0f and 1.0f
 * The functions return a value between lfo->min and lfo->max which are
 * absolute values that may range beyond the internal limits.
 *
 * lfo->step is a user defined, time (refresh rate) compensated step value
 * that assures the oscillator will run one cycle in the desired frequency.
 *
 * lfo->inv is an internal parameter that defines the counting direction.
 *
 * lfo->value is the current LFO output value in the used defined range
 * (between lfo->min and lfo->max).
 */
float
_oalRingBufferLFOGetFixedValue(void* data, const void *ptr, unsigned track, unsigned int end)
{
   _oalRingBufferLFOInfo* lfo = (_oalRingBufferLFOInfo*)data;
   float rv = 1.0f;
   if (lfo)
   {
      rv = lfo->convert(lfo->value[track], 1.0f);
      rv = lfo->inv ? lfo->max-rv : rv;
   }
   return rv;
}

float
_oalRingBufferLFOGetTriangle(void* data, const void *ptr, unsigned track, unsigned int end)
{
   _oalRingBufferLFOInfo* lfo = (_oalRingBufferLFOInfo*)data;
   float rv = 1.0f;
   if (lfo)
   {
      float step = lfo->step[track];

      rv = lfo->convert(lfo->value[track], 1.0f);
      rv = lfo->inv ? lfo->max-rv : lfo->min+rv;

      lfo->value[track] += step;
      if (((lfo->value[track] <= lfo->min) && (step < 0))
          || ((lfo->value[track] >= lfo->max) && (step > 0))) 
      {
         lfo->step[track] *= -1.0f;
         lfo->value[track] -= step;
      }
   }
   return rv;
}


static float
_fast_sin1(float x)
{
   float y = fmodf(x+0.5f, 2.0f) - 1.0f;
   return 0.5f + 2.0f*(y - y*fabsf(y));
}

float
_oalRingBufferLFOGetSine(void* data, const void *ptr, unsigned track, unsigned int end)
{
   _oalRingBufferLFOInfo* lfo = (_oalRingBufferLFOInfo*)data;
   float rv = 1.0f;
   if (lfo)
   {
      float max = (lfo->max - lfo->min);
      float step = lfo->step[track];
      float v = lfo->value[track];

      lfo->value[track] += step;
      if (((lfo->value[track] <= lfo->min) && (step < 0))
          || ((lfo->value[track] >= lfo->max) && (step > 0)))
      {
         lfo->step[track] *= -1.0f;
         lfo->value[track] -= step;
      }
      v = (v - lfo->min)/max;

      rv = lfo->convert(_fast_sin1(v), max);
      rv = lfo->inv ? lfo->max-rv : lfo->min+rv;
   }
   return rv;
}

float
_oalRingBufferLFOGetSquare(void* data, const void *ptr, unsigned track, unsigned int end)
{
   _oalRingBufferLFOInfo* lfo = (_oalRingBufferLFOInfo*)data;
   float rv = 1.0f;
   if (lfo)
   {
      float step = lfo->step[track];

      rv = lfo->convert((step >= 0.0f ) ? lfo->max-lfo->min : 0, 1.0f);
      rv = lfo->inv ? lfo->max-rv : lfo->min+rv;

      lfo->value[track] += step;
      if (((lfo->value[track] <= lfo->min) && (step < 0))
          || ((lfo->value[track] >= lfo->max) && (step > 0)))
      {
         lfo->step[track] *= -1.0f;
         lfo->value[track] -= step;
      }
   }
   return rv;
}


float
_oalRingBufferLFOGetSawtooth(void* data, const void *ptr, unsigned track, unsigned int end)
{
   _oalRingBufferLFOInfo* lfo = (_oalRingBufferLFOInfo*)data;
   float rv = 1.0f;
   if (lfo)
   {
//    float max = (lfo->max - lfo->min);
      float step = lfo->step[track];

      rv = lfo->convert(lfo->value[track], 1.0f);
      rv = lfo->inv ? lfo->max-rv : lfo->min+rv;

      lfo->value[track] += step;
      if (lfo->value[track] <= lfo->min) {
         lfo->value[track] += lfo->max;
      } else if (lfo->value[track] >= lfo->max) {
         lfo->value[track] -= lfo->max;
      }
   }
   return rv;
}

float
_oalRingBufferLFOGetGainFollow(void* data, const void *ptr, unsigned track, unsigned int end)
{
   _oalRingBufferLFOInfo* lfo = (_oalRingBufferLFOInfo*)data;
   static const float div = 1.0f / (float)0x000fffff;
   float rv = 1.0f;
   if (lfo && ptr && end)
   {
      float olvl = lfo->value[0];

      /* In stereo-link mode the left track (0) provides the data */
      if (track == 0 || lfo->stereo_lnk == AAX_FALSE)
      {
         int32_t *sptr = (int32_t *)ptr;
         unsigned int i = end;
         float lvl, fact;
         uint64_t tmp;

         tmp = 0;
         do {
            tmp += abs(*sptr++);
         } while (--i);
         tmp /= end;
         lvl = _MINMAX(tmp*div, 0.0f, 1.0f);

         olvl = lfo->value[track];
         fact = lfo->step[track];
         lfo->value[track] = _MINMAX(olvl + fact*(lvl - olvl), 0.01f, 0.99f);
      }

      rv = lfo->convert(olvl, lfo->max-lfo->min);
      rv = lfo->inv ? lfo->max-rv : lfo->min+rv;

   }
   return rv;
}

float
_oalRingBufferLFOGetCompressor(void* data, const void *ptr, unsigned track, unsigned int end)
{
   _oalRingBufferLFOInfo* lfo = (_oalRingBufferLFOInfo*)data;
   static const float div = 1.0f / (float)0x000fffff;
   float rv = 1.0f;
   if (lfo && ptr && end)
   {
      float olvl = lfo->value[0];

      /* In stereo-link mode the left track (0) provides the data */
      if (track == 0 || lfo->stereo_lnk == AAX_FALSE)
      {
         int32_t *sptr = (int32_t *)ptr;
         unsigned int i = end;
         float lvl, fact;
         uint64_t tmp;

         tmp = 0;
         do {
            tmp += abs(*sptr++);
         } while (--i);
         tmp /= end;
         lvl = _MINMAX(tmp*div, 0.0f, 1.0f);

         olvl = lfo->value[track];
         fact = lfo->step[track];
         lfo->value[track] = _MINMAX(olvl + fact*(lvl - olvl), 0.01f, 0.99f);
      }

      // Needed for the compressor
      if (olvl > lfo->min) {
         olvl = (olvl-lfo->min);
      } else  {
         olvl = 0.0f;
      }

      rv = lfo->convert(olvl, lfo->max-lfo->min);
      rv = lfo->inv ? lfo->max-rv : lfo->min+rv;
   }
   return rv;
}

float
_oalRingBufferEnvelopeGet(_oalRingBufferEnvelopeInfo* env, char stopped)
{
   float rv = 1.0f;
   if (env)
   {
      unsigned int stage = env->stage;
      rv = env->value;
      if (stage < env->max_stages)
      {
         env->value += env->step[stage];
         if ((++env->pos == env->max_pos[stage])
             || (env->max_pos[stage] == (uint32_t)-1 && stopped))
         {
            env->pos = 0;
            env->stage++;
         }
      }
   }
   return rv;
}

/*
 * Convert 4-bit IMA to 16-bit PCM
 */
static void
_oalRingBufferIMA4ToPCM16(int32_t **__restrict dst, const void *__restrict src, int tracks, int blocksize, unsigned int no_samples)
{
   unsigned int i, blocks, block_smp;
   int16_t *d[_AAX_MAX_SPEAKERS];
   uint8_t *s = (uint8_t *)src;
   int t;

   if (tracks > _AAX_MAX_SPEAKERS)
      return;

   /* copy buffer pointers */
   for(t=0; t<tracks; t++) {
      d[t] = (int16_t*)dst[t];
   }

   block_smp = BLOCKSIZE_TO_SMP(blocksize);
   blocks = no_samples / block_smp;
   i = blocks-1;
   do
   {
      for (t=0; t<tracks; t++)
      {
         _sw_bufcpy_ima_adpcm(d[t], s, 1, block_smp);
         d[t] += block_smp;
         s += blocksize;
      }
   }
   while (--i);

   no_samples -= blocks*block_smp;
   if (no_samples)
   {
      int t;
      for (t=0; t<tracks; t++)
      {
         _sw_bufcpy_ima_adpcm(d[t], s, 1, no_samples);
         s += blocksize;
      }
   }
}


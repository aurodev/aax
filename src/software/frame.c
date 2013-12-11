/*
 * Copyright 2012 by Erik Hofman.
 * Copyright 2012 by Adalin B.V.
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

#include <errno.h>		/* for ETIMEDOUT */
#include <assert.h>

#include <base/timer.h>		/* for gettimeofday */
#include <base/threads.h>
#include <base/buffers.h>

#include <objects.h>
#include <api.h>

#include "arch.h"
#include "ringbuffer.h"
#include "audio.h"

void*
_aaxAudioFrameThread(void* config)
{
   _frame_t *frame = (_frame_t *)config;
   _aaxAudioFrame *smixer, *fmixer, *mixer;
   const _aaxDriverBackend *be;
   _aaxRingBuffer *dest_rb;
   _aaxTimer *timer;
   _handle_t* handle;
   unsigned int pos;
   float delay_sec;
   int res = 0;

   if (!frame || !frame->submix->info->no_tracks) {
      return NULL;
   }
   handle = frame->handle;

   dest_rb = _aaxRingBufferCreate(DELAY_EFFECTS_TIME);
   if (!dest_rb) {
      return NULL;
   }

   fmixer = NULL;
   smixer = frame->submix;
   be = _aaxGetDriverBackendLoopback(&pos);	/* be = handle->backend.ptr */
   if (!be) {
      return NULL;
   }

   _aaxMutexLock(frame->thread.mutex);
   delay_sec = 1.0f / smixer->info->refresh_rate;
   if (be)
   {
      _aaxMixerInfo* info;
      int tracks;

      info = smixer->info;

      /* unregistered frames that are positional are mono */
      tracks = (!handle && _IS_POSITIONAL(frame)) ? 1 : info->no_tracks;
      _aaxRingBufferSetParami(dest_rb, RB_NO_TRACKS, tracks);

      _aaxRingBufferSetFormat(dest_rb, be->codecs, AAX_PCM24S);
      _aaxRingBufferSetParamf(dest_rb, RB_FREQUENCY, info->frequency);
      _aaxRingBufferSetParamf(dest_rb, RB_DURATION_SEC, delay_sec);
      _aaxRingBufferInit(dest_rb, AAX_TRUE);
      _aaxRingBufferSetState(dest_rb, RB_STARTED);
      frame->thread.initialized = AAX_TRUE;
   }

   mixer = smixer;
   if (handle)  /* frame is registered */
   {
      assert (handle->id == HANDLE_ID);

      if (handle->id == HANDLE_ID)
      {
         _intBufferData *dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
         if (dptr)
         {
            _sensor_t* sensor = _intBufGetDataPtr(dptr);
            smixer = sensor->mixer;
            fmixer = frame->submix;
            mixer = fmixer;
            _intBufReleaseData(dptr, _AAX_SENSOR);
         }
      }
   }
#if 0
   else /* frame is not registered */
   {
      mixer = smixer = fmixer = frame->submix;
      memcpy(&sp2d, smixer->props2d, sizeof(_aaxRingBuffer2dProps));
      memcpy(&sp2d.speaker, smixer->info->speaker,
                                     _AAX_MAX_SPEAKERS*sizeof(vec4_t));
      memcpy(&sp2d.hrtf, smixer->info->hrtf, 2*sizeof(vec4_t));
      sp3d = _aax3dPropsDup(smixer->props3d);
   }
#endif

   /* get real duration, it might have been altered for better performance */
   delay_sec = _aaxRingBufferGetParamf(dest_rb, RB_DURATION_SEC);

   timer = _aaxTimerCreate();
   _aaxTimerSetCondition(timer, frame->thread.condition);
   _aaxTimerStartRepeatable(timer, delay_sec);

   do
   {
      if TEST_FOR_FALSE(frame->thread.started) {
         break;
      }

      if (_IS_PLAYING(frame) || _IS_STANDBY(frame))
      {
         if (mixer->emitters_3d || mixer->emitters_2d || mixer->frames)
         {
            if (_IS_PLAYING(frame) && be->state(NULL, DRIVER_AVAILABLE))
            {
               dest_rb = _aaxAudioFrameProcessThreadedFrame(handle, dest_rb,
                                                     mixer, smixer, fmixer, be);
               assert(dest_rb);
            }
            else { /* if (_IS_STANDBY(frame)) */
               _aaxNoneDriverProcessFrame(mixer);
            }
         }
      }
      mixer->capturing++;

      /**
       * _aaxSoftwareMixerSignalFrames uses _aaxConditionSignal to let the
       * frame procede in advance, before the main thread starts mixing so
       * threads will be finished soon after the main thread.
       * As a result _aaxTimerWait may return AAX_TRUE instead, which is
       * not a problem since earlier in the loop there is a test to see if
       * the thread really is finished and then breaks the loop.
       *
       * Note: the thread will not be signaled to start mixing if there's
       *       already a buffer in it's buffer queue.
       */
      res = _aaxTimerWait(timer, frame->thread.mutex);
   }
   while ((res == AAX_TIMEOUT) || (res == AAX_TRUE));

   _aaxTimerDestroy(timer);
   frame->thread.initialized = AAX_FALSE;
   _aaxMutexUnLock(frame->thread.mutex);
   _aaxRingBufferSetState(dest_rb, RB_STOPPED);
   _aaxRingBufferDestroy(dest_rb);

   return frame;
}

void
_aaxAudioFrameMix(_aaxRingBuffer *dest_rb, _intBuffers *ringbuffers,
                  _aaxRingBuffer2dProps *fp2d, 
                  const _aaxDriverBackend *be, void *be_handle)
{
   _intBufferData *buf;

   _intBufGetNum(ringbuffers, _AAX_RINGBUFFER);

   buf = _intBufPopNormal(ringbuffers, _AAX_RINGBUFFER, AAX_TRUE);
   if (buf)
   {
      _aaxRingBufferLFOInfo *lfo;
      unsigned char track, tracks;
      unsigned int dno_samples;
      _aaxRingBuffer *src_rb;
      float g = 1.0f;

      dno_samples = _aaxRingBufferGetParami(dest_rb, RB_NO_SAMPLES);
      tracks = _aaxRingBufferGetParami(dest_rb, RB_NO_TRACKS);
      src_rb = _intBufGetDataPtr(buf);

      lfo = _FILTER_GET_DATA(fp2d, DYNAMIC_GAIN_FILTER);
      if (lfo && lfo->envelope)
      {
          g = 0.0f;
          for (track=0; track<tracks; track++)
          {
              int32_t *sptr = src_rb->sample->track[track];
              float gain =  lfo->get(lfo, sptr, track, dno_samples);

              if (lfo->inv) gain = 1.0f/g;
              g += gain;
          }
          g /= tracks;
      }

      for (track=0; track<tracks; track++)
      {
         int32_t *dptr = dest_rb->sample->track[track];
         int32_t *sptr = src_rb->sample->track[track];
         float gstep = 0.0f;

         _batch_fmadd(dptr, sptr, dno_samples, g, gstep);
      }

      /*
       * push the ringbuffer to the back of the stack so it can
       * be used without the need to delete this one now and 
       * create a new ringbuffer later on.
       */
      _intBufPushNormal(ringbuffers, _AAX_RINGBUFFER, buf, AAX_TRUE);
   }

   _intBufReleaseNum(ringbuffers, _AAX_RINGBUFFER);
}

/* -------------------------------------------------------------------------- */

static void *
_aaxAudioFrameSwapBuffers(void *rb, _intBuffers *ringbuffers, char dde)
{
   _aaxRingBuffer *nrb;
   _intBufferData *buf;
   
   _intBufGetNum(ringbuffers, _AAX_RINGBUFFER);

   buf = _intBufPopNormal(ringbuffers, _AAX_RINGBUFFER, AAX_TRUE);
   if (buf)
   {
      nrb = _intBufSetDataPtr(buf, rb);
      if (dde) {
         _aaxRingBufferCopyDelyEffectsData(nrb, rb);
      }

      _intBufPushNormal(ringbuffers, _AAX_RINGBUFFER, buf, AAX_TRUE);
   }
   else
   {
      nrb = _aaxRingBufferDuplicate(rb, AAX_TRUE, dde);
      _intBufAddDataNormal(ringbuffers, _AAX_RINGBUFFER, rb, AAX_TRUE);
   }

   rb = nrb;
   assert(rb != NULL);
   _intBufReleaseNum(ringbuffers, _AAX_RINGBUFFER);

   return rb;
}


/**
 * ssv: sensor sound velocity
 * sdf: sensor doppler factor
 * pp[23]d: parent object (mixer or audio-frame)
 * fp[23]d: current audio frame
 *
 * NOTE: fp3d must be a copy of the frames dprops3d structure since
 *       this function may alter it's contents.
 */
char
_aaxAudioFrameProcess(_aaxRingBuffer *dest_rb, void *sensor,
                      _aaxAudioFrame *fmixer, float ssv, float sdf,
                      _aaxRingBuffer2dProps *pp2d,
                      _aaxDelayed3dProps *pdp3d_m,
                      _aaxRingBuffer2dProps *fp2d,
                      _aaxDelayed3dProps *fdp3d,
                      _aaxDelayed3dProps *fdp3d_m,
                      const _aaxDriverBackend *be, void *be_handle,
                      char fprocess)
{
   char process;

   /* update the model-view matrix based on our own and that of out parent */
   if (pdp3d_m)
   {
      if (_PROP3D_MTX_HAS_CHANGED(pdp3d_m) || _PROP3D_MTX_HAS_CHANGED(fdp3d_m))
      {
         mtx4Mul(fdp3d_m->matrix, pdp3d_m->matrix, fdp3d->matrix);
#if 0
 printf("!  parent:\t\t\t\tframe:\n");
 PRINT_MATRICES(pdp3d_m->matrix, fdp3d->matrix);
 printf("!  modified frame\n");
 PRINT_MATRIX(fdp3d_m->matrix);
#endif

         _PROP3D_MTX_CLEAR_CHANGED(pdp3d_m);
         _PROP3D_MTX_SET_CHANGED(fdp3d_m);
      }

      if (_PROP3D_MTXSPEED_HAS_CHANGED(pdp3d_m) ||
          _PROP3D_MTXSPEED_HAS_CHANGED(fdp3d_m))
      {
         mtx4Mul(fdp3d_m->velocity, pdp3d_m->velocity, fdp3d->velocity);

#if 0
 printf("parent velocity:\t\t\tframe velocity:\n");
 PRINT_MATRICES(pdp3d_m->velocity, fdp3d->velocity);
 printf("modified frame velocity\n");
 PRINT_MATRIX(fdp3d_m->velocity);
#endif


         _PROP3D_SPEED_CLEAR_CHANGED(pdp3d_m);
         _PROP3D_SPEED_SET_CHANGED(fdp3d_m);
      }
   }

   /** process possible registered emitters */
   process = _aaxEmittersProcess(dest_rb, fmixer->info, ssv, sdf, fp2d, fdp3d_m,
                                 fmixer->emitters_2d, fmixer->emitters_3d,
                                 be, be_handle);

   /** process registered sub-frames */
   if (fprocess && fmixer->frames)
   {
      _aaxRingBuffer *frame_rb = fmixer->ringbuffer;

      /*
       * Make sure there's a ringbuffer when at least one subframe is
       * registered. All subframes use this ringbuffer for rendering.
       */
      if (!frame_rb)
      {
         frame_rb = _aaxRingBufferCreate(DELAY_EFFECTS_TIME);
         if (frame_rb)
         {
            _aaxMixerInfo* info = fmixer->info;
            float dt = 1.0f/info->refresh_rate;

            _aaxRingBufferSetParami(frame_rb, RB_NO_TRACKS, info->no_tracks);
            _aaxRingBufferSetFormat(frame_rb, be->codecs, AAX_PCM24S);
            _aaxRingBufferSetParamf(frame_rb, RB_FREQUENCY, info->frequency);
            _aaxRingBufferSetParamf(frame_rb, RB_DURATION_SEC, dt);
            _aaxRingBufferInit(frame_rb, AAX_TRUE);
            fmixer->ringbuffer = frame_rb;
         }
      }

      /* process registered (non threaded) sub-frames */
      if (frame_rb)
      {
         _intBuffers *hf = fmixer->frames;
         unsigned int i, max, cnt;

         max = _intBufGetMaxNum(hf, _AAX_FRAME);
         cnt = _intBufGetNumNoLock(hf, _AAX_FRAME);
         for (i=0; i<max; i++)
         {
            process = _aaxAudioFrameRender(dest_rb, fmixer, fp2d, fdp3d_m, hf,
                                           i, ssv, sdf, be, be_handle);
            if (process) --cnt;
            if (cnt == 0) break;
         }
         _intBufReleaseNum(hf, _AAX_FRAME);
      }
   }

   /** process registered devices */
   if (fmixer->devices)
   {
      _aaxMixerInfo* info = fmixer->info;
      _aaxSensorsProcess(dest_rb, fmixer->devices, fp2d, info->track);
      process = AAX_TRUE;
   }

   if (fprocess && process)
   {
      be->effects(be, be_handle, dest_rb, fp2d);
      be->postprocess(be_handle, dest_rb, sensor);
   }

   return process;
}

char
_aaxAudioFrameRender(_aaxRingBuffer *dest_rb, _aaxAudioFrame *fmixer, _aaxRingBuffer2dProps *fp2d, _aaxDelayed3dProps *fdp3d_m, _intBuffers *hf, unsigned int i, float ssv, float sdf, const _aaxDriverBackend *be, void *be_handle)
{
   char process = AAX_FALSE;
   _intBufferData *dptr;

   dptr = _intBufGet(hf, _AAX_FRAME, i);
   if (dptr)
   {
      _aaxRingBuffer *frame_rb = fmixer->ringbuffer;
      _frame_t* subframe = _intBufGetDataPtr(dptr);
      _aaxAudioFrame *sfmixer = subframe->submix;
      _aaxDelayed3dProps *sfdp3d_m;
      _aaxDelayed3dProps *sfdp3d;
      _aaxRingBuffer2dProps sfp2d;
      unsigned int size;
      int res;

      size = sizeof(_aaxDelayed3dProps);
      sfdp3d = _aax_aligned_alloc16(size);
      sfdp3d_m = _aax_aligned_alloc16(size);

      _aaxAudioFrameProcessDelayQueue(sfmixer);

      _aax_memcpy(&sfp2d, sfmixer->props2d,
                          sizeof(_aaxRingBuffer2dProps));
      _aax_memcpy(sfdp3d, sfmixer->props3d->dprops3d,
                           sizeof(_aaxDelayed3dProps));
      _aax_memcpy(sfdp3d_m, sfmixer->props3d->m_dprops3d,
                             sizeof(_aaxDelayed3dProps));

      sfdp3d_m->state3d = sfdp3d->state3d;
      sfdp3d_m->pitch = sfdp3d->pitch;
      sfdp3d_m->gain = sfdp3d->gain;
      _PROP_CLEAR(sfmixer->props3d);
      _intBufReleaseData(dptr, _AAX_FRAME);

      /* read-only data */           
      _aax_memcpy(&sfp2d.speaker, fp2d->speaker,
                                  _AAX_MAX_SPEAKERS*sizeof(vec4_t));
      _aax_memcpy(&sfp2d.hrtf, fp2d->hrtf, 2*sizeof(vec4_t));

      /* clear the buffer for use by the subframe */
      _aaxRingBufferSetState(frame_rb, RB_CLEARED);
      _aaxRingBufferSetState(frame_rb, RB_STARTED);

      /*
       * frames render in the ringbuffer of their parent and mix with
       * dest_rb, this could potentialy save a lot of ringbuffers
       */
      res = _aaxAudioFrameProcess(frame_rb, NULL, sfmixer, ssv, sdf,
                                  fp2d, fdp3d_m, &sfp2d, sfdp3d, sfdp3d_m,
                                  be, be_handle, AAX_TRUE);

      /* if the subframe actually did render something, mix the data */
      if (res)
      {
         char dde = (_EFFECT_GET2D_DATA(sfmixer, DELAY_EFFECT) != NULL);
         _aaxDelayed3dProps *m_sfdp3d;

         fmixer->ringbuffer = _aaxAudioFrameSwapBuffers(frame_rb,
                                              sfmixer->frame_ringbuffers, dde);

         /* copy back the altered sfdp3d matrix and velocity vector */
         /* beware, they might have been altered in the mean time! */
         dptr = _intBufGet(hf, _AAX_FRAME, i);
         m_sfdp3d = sfmixer->props3d->m_dprops3d;
         if (!_PROP_MTX_HAS_CHANGED(sfmixer->props3d)) {
            mtx4Copy(m_sfdp3d->matrix, sfdp3d_m->matrix);
         }
         if (!_PROP_SPEED_HAS_CHANGED(sfmixer->props3d)) {
            mtx4Copy(m_sfdp3d->velocity, sfdp3d_m->velocity);
         }
         _intBufReleaseData(dptr, _AAX_FRAME);

         /* finally mix the data with dest_rb */
         _aaxAudioFrameMix(dest_rb, sfmixer->frame_ringbuffers, &sfp2d, 
                           be, be_handle);
         sfmixer->capturing = 1;

         process = AAX_TRUE;
      }

      _aax_aligned_free(sfdp3d);
      _aax_aligned_free(sfdp3d_m);
   }

   return process;
}

void
_aaxAudioFrameProcessDelayQueue(_aaxAudioFrame *frame)
{
   _aax3dProps* fp3d = frame->props3d;
   _aaxDelayed3dProps* fdp3d = fp3d->dprops3d;

   if (_PROP3D_DISTQUEUE_IS_DEFINED(fdp3d))
   {
      _aaxRingBuffer2dProps* fp2d = frame->props2d;
      _aaxDelayed3dProps *tdp3d = NULL;
      _intBufferData *buf3dq;
      float pos3dq;

      _intBufAddData(frame->p3dq, _AAX_DELAYED3D, fdp3d);
      if (frame->curr_pos_sec > fp2d->dist_delay_sec)
      {
         pos3dq = fp2d->bufpos3dq;
         fp2d->bufpos3dq += fp3d->buf3dq_step;
         if (pos3dq > 0.0f)
         {
            do
            {
               buf3dq = _intBufPop(frame->p3dq, _AAX_DELAYED3D);
               if (buf3dq)
               {
                  tdp3d = _intBufGetDataPtr(buf3dq);
                  _aax_aligned_free(tdp3d);
               }
               --fp2d->bufpos3dq;
            }
            while (fp2d->bufpos3dq > 1.0f);
         }
      }

      if (!tdp3d) {			// TODO: get from buffer cache
         tdp3d = _aaxDelayed3dPropsDup(fdp3d);
      }
      fp3d->dprops3d = tdp3d;
   }
}

void*
_aaxAudioFrameProcessThreadedFrame(_handle_t* handle, void *frame_rb,
          _aaxAudioFrame *mixer, _aaxAudioFrame *smixer, _aaxAudioFrame *fmixer,
          const _aaxDriverBackend *be)
{
   void *be_handle = NULL;
   _aaxDelayed3dProps *sdp3d, *sdp3d_m;
   _aaxDelayed3dProps *fdp3d, *fdp3d_m;
   _aaxRingBuffer2dProps sp2d, fp2d;
   _intBufferData *dptr;
   unsigned int size;
   float ssv = 343.3f;
   float sdf = 1.0f;
   char dde;

   assert(handle);
   assert(mixer); /* equals to fmixer for registered frames: always for now */
   assert(smixer); /* sensor mixer */
   assert(fmixer); /* frame submixer */

   be_handle = handle->backend.handle;

   size = sizeof(_aaxDelayed3dProps);
   sdp3d = _aax_aligned_alloc16(size);
   sdp3d_m = _aax_aligned_alloc16(size);
   fdp3d = _aax_aligned_alloc16(size);
   fdp3d_m = _aax_aligned_alloc16(size);

   /**
    * copying here prevents locking the listener the whole time
    * it's used for just one time-frame anyhow
    * Note: modifications here should also be made to
    *       _aaxSoftwareMixerThreadUpdate
    */
   dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
   if (dptr)
   {
      // _aaxAudioFrameProcessDelayQueue is called in
      // _aaxSoftwareMixerThreadUpdate
      ssv = _EFFECT_GETD3D(smixer, VELOCITY_EFFECT, AAX_SOUND_VELOCITY);
      sdf = _EFFECT_GETD3D(smixer, VELOCITY_EFFECT, AAX_DOPPLER_FACTOR);

      _aax_memcpy(&sp2d, smixer->props2d, sizeof(_aaxRingBuffer2dProps));
      _aax_memcpy(sdp3d, smixer->props3d->dprops3d,
                          sizeof(_aaxDelayed3dProps));
      _PROP_CLEAR(smixer->props3d);
      _intBufReleaseData(dptr, _AAX_SENSOR);
   }

   /* read-only data */
   _aax_memcpy(&sp2d.speaker, handle->info->speaker,
                          _AAX_MAX_SPEAKERS*sizeof(vec4_t));
   _aax_memcpy(&sp2d.hrtf, handle->info->hrtf, 2*sizeof(vec4_t));

   /* update the modified properties */
   mtx4Copy(sdp3d_m->matrix, sdp3d->matrix);
   mtx4Mul(sdp3d_m->velocity, sdp3d->matrix, sdp3d->velocity);
   sdp3d_m->state3d = sdp3d->state3d;
   sdp3d_m->pitch = sdp3d->pitch;
   sdp3d_m->gain = sdp3d->gain;

   /* frame */
   _aaxAudioFrameProcessDelayQueue(fmixer);
   _aax_memcpy(&fp2d, fmixer->props2d, sizeof(_aaxRingBuffer2dProps));
   _aax_memcpy(fdp3d, fmixer->props3d->dprops3d,
                       sizeof(_aaxDelayed3dProps));
   _aax_memcpy(fdp3d_m, fmixer->props3d->m_dprops3d,
                         sizeof(_aaxDelayed3dProps));

   fdp3d_m->state3d = fdp3d->state3d;
   fdp3d_m->pitch = fdp3d->pitch;
   fdp3d_m->gain = fdp3d->gain;
// TODO: (test with kx, 4 channel)
// _PROP_CLEAR(fmixer->props3d);

   /* frame read-only data */
   _aax_memcpy(&fp2d.speaker, &sp2d.speaker, _AAX_MAX_SPEAKERS*sizeof(vec4_t));
   _aax_memcpy(&fp2d.hrtf, &sp2d.hrtf, 2*sizeof(vec4_t));

   /* clear the buffer for use by the subframe */
   _aaxRingBufferSetState(frame_rb, RB_CLEARED);
   _aaxRingBufferSetState(frame_rb, RB_STARTED);

   _aaxAudioFrameProcess(frame_rb, NULL, mixer, ssv, sdf, &sp2d, sdp3d_m,
                         &fp2d, fdp3d, fdp3d_m, be, be_handle,AAX_TRUE);

   /* copy back the altered fdp3d matrix and velocity vector   */
   /* beware, they might have been altered in the mean time! */
   if (!_PROP_MTX_HAS_CHANGED(fmixer->props3d)) {
      mtx4Copy(fmixer->props3d->m_dprops3d->matrix, fdp3d_m->matrix);
   }
   if (!_PROP_SPEED_HAS_CHANGED(fmixer->props3d)) {
      mtx4Copy(fmixer->props3d->m_dprops3d->velocity, fdp3d_m->velocity);
   }

   _aax_aligned_free(sdp3d);
   _aax_aligned_free(sdp3d_m);
   _aax_aligned_free(fdp3d);
   _aax_aligned_free(fdp3d_m);

   dde = (_EFFECT_GET2D_DATA(fmixer, DELAY_EFFECT) != NULL);
   return _aaxAudioFrameSwapBuffers(frame_rb, fmixer->play_ringbuffers, dde);
}


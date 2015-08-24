/*
 * Copyright 2007-2014 by Erik Hofman.
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
#include <math.h>		/* for INFINITY */
#include <errno.h>		/* for ETIMEDOUT */

#include <base/threads.h>
#include <base/gmath.h>
#include <base/timer.h>		/* for msecSleep */

#include <dsp/filters.h>
#include <dsp/effects.h>

#include "api.h"
#include "devices.h"

static int _aaxSensorCreateRingBuffer(_handle_t *);
static int _aaxSensorCaptureStart(_handle_t *);
static int _aaxSensorCaptureStop(_handle_t *);

AAX_API int AAX_APIENTRY
aaxSensorSetMatrix(aaxConfig config, aaxMtx4f mtx)
{
   _handle_t *handle = get_handle(config);
   int rv = __release_mode;

   if (!rv)
   {
      if (!handle) {
         _aaxErrorSet(AAX_INVALID_HANDLE);
      } else if (!mtx || detect_nan_mtx4((const float (*)[4])mtx)) {
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      } else {
         rv = AAX_TRUE;
      }
   }

   if (rv)
   {
      const _intBufferData* dptr;
      dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
      if (dptr)
      {
         _sensor_t* sensor = _intBufGetDataPtr(dptr);
         _aaxAudioFrame* smixer = sensor->mixer;
         mtx4Copy(smixer->props3d->dprops3d->matrix, mtx);
         mtx4Copy(smixer->props3d->m_dprops3d->matrix, mtx);
         _PROP_MTX_SET_CHANGED(smixer->props3d);
         _intBufReleaseData(dptr, _AAX_SENSOR);
      }
      else
      {
         _aaxErrorSet(AAX_INVALID_STATE);
         rv = AAX_FALSE;
      }
   }

   return rv;
}

AAX_API int AAX_APIENTRY
aaxSensorGetMatrix(const aaxConfig config, aaxMtx4f mtx)
{
   _handle_t *handle = get_handle(config);
   int rv = __release_mode;

   if (!rv)
   {
      if (!handle) {
         _aaxErrorSet(AAX_INVALID_HANDLE);
      } else if (!mtx) {
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      } else {
         rv = AAX_TRUE;
      }
   }

   if (rv)
   {
      const _intBufferData* dptr;
      dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
      if (dptr)
      {
         _sensor_t* sensor = _intBufGetDataPtr(dptr);
         _aaxAudioFrame* smixer = sensor->mixer;
          mtx4Copy(mtx, smixer->props3d->dprops3d->matrix);
          _PROP_MTX_SET_CHANGED(smixer->props3d);
         _intBufReleaseData(dptr, _AAX_SENSOR);
      }
      else
      {
         _aaxErrorSet(AAX_INVALID_STATE);
         rv = AAX_FALSE;
      }
   }

   return rv;
}

AAX_API int AAX_APIENTRY
aaxSensorSetVelocity(aaxConfig config, const aaxVec3f velocity)
{
   _handle_t *handle = get_handle(config);
   int rv = __release_mode;

   if (!rv)
   {
      if (!handle) {
         _aaxErrorSet(AAX_INVALID_HANDLE);
      } else if (!velocity || detect_nan_vec3(velocity)) {
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      } else {
         rv = AAX_TRUE;
      }
   }

   if (rv)
   {
      const _intBufferData* dptr;
      dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
      if (dptr)
      {
         mtx4_t mtx;
         _sensor_t* sensor = _intBufGetDataPtr(dptr);
         _aaxDelayed3dProps *dp3d;

         mtx4Copy(mtx, aaxIdentityMatrix);
         vec3Copy(mtx[VELOCITY], velocity);
         mtx[VELOCITY][3] = 0.0f;

         dp3d = sensor->mixer->props3d->dprops3d;
//       mtx4InverseSimple(dp3d->velocity, mtx);
         mtx4Copy(dp3d->velocity, mtx);
         _PROP_SPEED_SET_CHANGED(sensor->mixer->props3d);
         _intBufReleaseData(dptr, _AAX_SENSOR);
      }
      else
      {
         _aaxErrorSet(AAX_INVALID_STATE);
         rv = AAX_FALSE;
      }
   }

   return rv;
}

AAX_API int AAX_APIENTRY
aaxSensorGetVelocity(const aaxConfig config, aaxVec3f velocity)
{
   _handle_t *handle = get_handle(config);
   int rv = __release_mode;

   if (!rv)
   {
      if (!handle) {
         _aaxErrorSet(AAX_INVALID_HANDLE);
      } else if (!velocity) {
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      } else {
         rv = AAX_TRUE;
      }
   }

   if (rv)
   {
      const _intBufferData* dptr;
      dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
      if (dptr)
      {
         mtx4_t mtx;
         _sensor_t* sensor = _intBufGetDataPtr(dptr);
         _aaxDelayed3dProps *dp3d;

         dp3d = sensor->mixer->props3d->dprops3d;
//       mtx4InverseSimple(mtx, dp3d->velocity);
         mtx4Copy(mtx, dp3d->velocity);
         _intBufReleaseData(dptr, _AAX_SENSOR);

         vec3Copy(velocity, mtx[VELOCITY]);
      }
      else {
         _aaxErrorSet(AAX_INVALID_STATE);
      }
   }
   return rv;
}

AAX_API unsigned long AAX_APIENTRY
aaxSensorGetOffset(const aaxConfig config, enum aaxType type)
{
   _handle_t *handle = get_handle(config);
   unsigned long rv = 0;

   if (handle)
   {
      const _intBufferData* dptr;
      dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
      if (dptr)
      {
         _sensor_t* sensor = _intBufGetDataPtr(dptr);
         _aaxAudioFrame* smixer = sensor->mixer;

         switch (type)
         {
         case AAX_MICROSECONDS:
            rv = (unsigned long)(smixer->curr_pos_sec*1e6f);
            break;
         case AAX_FRAMES:
         case AAX_SAMPLES:
            rv = (smixer->curr_sample >= UINT_MAX) ?
                   UINT_MAX : smixer->curr_sample;
            break;
         case AAX_BYTES:
         {
            const _intBufferData* dptr_rb;
            dptr_rb = _intBufGet(smixer->play_ringbuffers, _AAX_RINGBUFFER, 0);
            if (dptr_rb)
            {
               _aaxRingBuffer *rb = _intBufGetDataPtr(dptr_rb);
               rv = (smixer->curr_sample >= UINT_MAX) ?
                      UINT_MAX : smixer->curr_sample;
               rv *= rb->get_parami(rb, RB_BYTES_SAMPLE);
            }
            break;
         }
         default:
            _aaxErrorSet(AAX_INVALID_ENUM);
         }
         _intBufReleaseData(dptr, _AAX_SENSOR);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

AAX_API aaxBuffer AAX_APIENTRY
aaxSensorGetBuffer(const aaxConfig config)
{
   _handle_t *handle = get_handle(config);
   aaxBuffer buffer = NULL;

   if (handle)
   {
      const _intBufferData* dptr;
      dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
      if (dptr)
      {
         _sensor_t* sensor = _intBufGetDataPtr(dptr);
         _aaxAudioFrame* smixer = sensor->mixer;
         _intBuffers *dptr_rb = smixer->play_ringbuffers;
         _intBufferData *rbuf;

         rbuf = _intBufPop(dptr_rb, _AAX_RINGBUFFER);
         if (rbuf)
         {
            _aaxRingBuffer *rb = _intBufGetDataPtr(rbuf);
            _buffer_t *buf = calloc(1, sizeof(_buffer_t));
            if (buf)
            {
               buf->id = BUFFER_ID;
               buf->ref_counter = 1;

               buf->blocksize = 1;
               buf->pos = 0;
               buf->format = rb->get_parami(rb, RB_FORMAT);
               buf->frequency = rb->get_paramf(rb, RB_FREQUENCY);

               buf->mipmap = AAX_FALSE;

               buf->info = &_info;
               rb->set_parami(rb, RB_IS_MIXER_BUFFER, AAX_FALSE);
               buf->ringbuffer = rb;

               buffer = (aaxBuffer)buf;
            }
            else {
               _aaxErrorSet(AAX_INSUFFICIENT_RESOURCES);
            }
            _intBufDestroyDataNoLock(rbuf);
         }
         else {
            _aaxErrorSet(AAX_INVALID_REFERENCE);
         }
         _intBufReleaseData(dptr, _AAX_SENSOR);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return buffer;
}

AAX_API int AAX_APIENTRY
aaxSensorWaitForBuffer(aaxConfig config, float timeout)
{
   _handle_t *handle = get_handle(config);
   int rv = AAX_FALSE;
   if (handle)
   {
      const _intBufferData* dptr;

      dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
      if (dptr)
      {
         _sensor_t* sensor = _intBufGetDataPtr(dptr);
         _intBuffers *ringbuffers = sensor->mixer->play_ringbuffers;
         int nbuf = _intBufGetNumNoLock(ringbuffers, _AAX_RINGBUFFER);
         _intBufReleaseData(dptr, _AAX_SENSOR);
         if (nbuf) rv = AAX_TRUE;
      }

      if (!rv)
      {
         rv = _aaxSignalWaitTimed(&handle->buffer_ready, timeout);
         if (rv == AAX_TIMEOUT)
         {
            rv = AAX_FALSE;
            _aaxErrorSet(AAX_TIMEOUT);
         }
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }

   return rv;
}

AAX_API int AAX_APIENTRY
aaxSensorSetState(aaxConfig config, enum aaxState state)
{
   _handle_t *handle = get_valid_handle(config);
   int rv = AAX_FALSE;
   if (handle)
   {
      switch(state)
      {
      case AAX_CAPTURING:
         if ((handle->info->mode == AAX_MODE_READ) && !handle->handle) {
            rv = _aaxSensorCaptureStart(handle);
         }
         else if (handle->handle)	/* registered sensor */
         {
            _aaxSensorCreateRingBuffer(handle);
            _SET_PLAYING(handle);
            rv = AAX_TRUE;
         }
         else				/* capture buffer on playback */
         {
            const _intBufferData* dptr;
            dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
            if (dptr)
            {
               _sensor_t* sensor = _intBufGetDataPtr(dptr);
               sensor->mixer->capturing = AAX_TRUE;
               _intBufReleaseData(dptr, _AAX_SENSOR);
               rv = AAX_TRUE;
            }
            else {
               _aaxErrorSet(AAX_INVALID_STATE);
            }
         }
         break;
//    case AAX_PROCESSED:
      case AAX_SUSPENDED:
      case AAX_STOPPED:
         if ((handle->info->mode == AAX_MODE_READ) && !handle->handle) {
            rv = _aaxSensorCaptureStop(handle);
         }
         else if (handle->handle)	/* registered sensor */
         {
            if (state == AAX_SUSPENDED) {
               _SET_PAUSED(handle);
            } else {
               _SET_PROCESSED(handle);
            }
            rv = AAX_TRUE;
         }
         else if (handle->sensors)		/* capture buffer on playback */
         {
            const _intBufferData* dptr;
            dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
            if (dptr)
            {
               _sensor_t* sensor = _intBufGetDataPtr(dptr);
               sensor->mixer->capturing = AAX_FALSE;
               _intBufReleaseData(dptr, _AAX_SENSOR);
               rv = AAX_TRUE;
            }
            else {
               _aaxErrorSet(AAX_INVALID_STATE);
            }
         }
         break;
      default:
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

AAX_API enum aaxState AAX_APIENTRY
aaxSensorGetState(const aaxConfig config)
{
   enum aaxState ret = AAX_STATE_NONE;
   return ret;
}

AAX_API int AAX_APIENTRY
aaxSensorSetMode(aaxConfig config, enum aaxModeType type, int mode)
{
   int rv = AAX_FALSE;
   return rv;
}

AAX_API int AAX_APIENTRY
aaxSensorGetMode(const aaxConfig config, enum aaxModeType type)
{
   int rv = AAX_FALSE;
   return rv;
}

AAX_API int AAX_APIENTRY
aaxSensorSetSetup(aaxConfig config, enum aaxSetupType type, unsigned int setup)
{
   int rv = AAX_FALSE;
   return rv;
}

AAX_API unsigned int AAX_APIENTRY
aaxSensorGetSetup(const aaxConfig config, enum aaxSetupType type)
{
   unsigned int rv = AAX_FALSE;
   return rv;
}

/* -------------------------------------------------------------------------- */

/* capturing only */
int
_aaxSensorCreateRingBuffer(_handle_t *handle)
{
   _intBufferData *dptr;
   int rv = AAX_FALSE;

   assert(handle);

   if ((handle->info->mode != AAX_MODE_READ) ||
       (handle->thread.started != AAX_FALSE)) {
      return rv;
   }

   dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
   if (dptr)
   {
      _sensor_t *sensor = _intBufGetDataPtr(dptr);
      _aaxAudioFrame *submix = sensor->mixer;
      _aaxMixerInfo* info = submix->info;
      _aaxRingBuffer *rb;

      if (!submix->ringbuffer)
      {
         const _aaxDriverBackend *be = handle->backend.ptr;
         enum aaxRenderMode mode = info->mode;
         float dt = DELAY_EFFECTS_TIME;

         submix->ringbuffer = be->get_ringbuffer(dt, mode);
      }

      rb = submix->ringbuffer;
      if (rb)
      {
         float delay_sec = 1.0f / info->period_rate;
         const _aaxDriverBackend *be;
         float min, max;

         rb->set_format(rb, AAX_PCM24S, AAX_TRUE);
         rb->set_parami(rb, RB_NO_TRACKS, info->no_tracks);

         be = handle->backend.ptr;
         min = be->param(handle->backend.handle, DRIVER_MIN_VOLUME);
         max = be->param(handle->backend.handle, DRIVER_MAX_VOLUME);

         rb->set_paramf(rb, RB_VOLUME_MIN, min);
         rb->set_paramf(rb, RB_VOLUME_MAX, max);

         /* Do not alter the frequency at this time, it has been set by
          * aaxMixerRegisterSensor and may have changed in the mean time
*/       rb->set_paramf(rb, RB_FREQUENCY, info->frequency);
/*        */

         /* create a ringbuffer with a but of overrun space */
         rb->set_paramf(rb, RB_DURATION_SEC, delay_sec*1.0f);
         rb->init(rb, AAX_TRUE);

         /* 
          * Now set the actual duration, this will not alter the allocated
          * space since it is lower that the initial duration.
          */
         rb->set_paramf(rb, RB_DURATION_SEC, delay_sec);
         rb->set_state(rb, RB_STARTED);
      }
      _intBufReleaseData(dptr, _AAX_SENSOR);
   }

   return rv;
}

/* capturing only */
int
_aaxSensorCaptureStart(_handle_t *handle)
{
   int rv = AAX_FALSE;
   assert(handle);
   assert(handle->info->mode == AAX_MODE_READ);
   assert(handle->thread.started == AAX_FALSE);

   if (_IS_INITIAL(handle) || _IS_PROCESSED(handle))
   {
      const _aaxDriverBackend *be = handle->backend.ptr;
      if (be->thread)
      {
         unsigned int ms;
         int r;

         handle->thread.ptr = _aaxThreadCreate();
         assert(handle->thread.ptr != 0);

         _aaxSignalInit(&handle->thread.signal);
         assert(handle->thread.signal.condition != 0);
         assert(handle->thread.signal.mutex != 0);

         handle->thread.started = AAX_TRUE;
         ms = rintf(1000/handle->info->period_rate);
#if 0
         r = _aaxThreadStart(handle->thread.ptr,
                             handle->backend.ptr->thread, handle, ms);
#else
         r = _aaxThreadStart(handle->thread.ptr, _aaxSoftwareMixerThread,
                              handle, ms);
#endif
         if (r == 0)
         {
            int p = 0;
            do
            {
               msecSleep(100);
               r = (handle->ringbuffer != 0);
               if (p++ > 500) break;
            }
            while (r == 0);

            if (r == 0)
            {
               _aaxErrorSet(AAX_TIMEOUT);
               handle->thread.started = AAX_FALSE;
            }
            else
            {
               _intBufferData *dptr;
               dptr = _intBufGet(handle->sensors, _AAX_SENSOR, 0);
               if (dptr)
               {
                  _sensor_t *sensor = _intBufGetDataPtr(dptr);
                  sensor->mixer->capturing = AAX_TRUE;
                  _intBufReleaseData(dptr, _AAX_SENSOR);
               }

               _SET_PLAYING(handle);
               rv = AAX_TRUE;
            }
         }
      }
      else {
         _aaxErrorSet(AAX_INVALID_STATE);
      }
   }
   else if (_IS_STANDBY(handle)) {
      rv = AAX_TRUE;
   }
   return rv;
}

int
_aaxSensorCaptureStop(_handle_t *handle)
{
   int rv = AAX_FALSE;
   if TEST_FOR_TRUE(handle->thread.started)
   {
      if (handle->info->mode == AAX_MODE_READ)
      {
         const _intBufferData* dptr;

         handle->thread.started = AAX_FALSE;

         _aaxSignalTrigger(&handle->thread.signal);
         _aaxThreadJoin(handle->thread.ptr);

         _aaxSignalFree(&handle->thread.signal);
         _aaxThreadDestroy(handle->thread.ptr);

         dptr = _intBufGetNoLock(handle->sensors, _AAX_SENSOR, 0);
         if (dptr)
         {
            _sensor_t *sensor = _intBufGetDataPtr(dptr);
            sensor->mixer->capturing = AAX_FALSE;
/*          Note: _intBufGetNoLock above, no need to unlock
 *          _intBufReleaseData(dptr, _AAX_SENSOR);
 */
         }

         _SET_PROCESSED(handle);
         rv = AAX_TRUE;
      }
   }
   return rv;
}

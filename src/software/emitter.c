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

#include <assert.h>

#include <ringbuffer.h>
#include <objects.h>
#include <api.h>

/**
 * sv_m -> modified sensor velocity vector
 * ssv -> sensor sound velocity
 * sdf -> sensor doppler factor
 * pos
 */
char
_aaxEmittersProcess(_oalRingBuffer *dest_rb, const _aaxMixerInfo *info,
                    float ssv, float sdf, _oalRingBuffer2dProps *fp2d,
                    _oalRingBufferDelayed3dProps *fdp3d_m,
                    _intBuffers *e2d, _intBuffers *e3d,
                    const _aaxDriverBackend* be, void *be_handle)
{
   unsigned int num, stage;
   _intBuffers *he = e3d;
   char rv = AAX_FALSE;
   float dt;

   num = 0;
   stage = 2;
   dt = _oalRingBufferGetParamf(dest_rb, RB_DURATION_SEC);
   do
   {
      unsigned int i, no_emitters;

      no_emitters = _intBufGetMaxNum(he, _AAX_EMITTER);
      num += no_emitters;

      for (i=0; i<no_emitters; i++)
      {
         _intBufferData *dptr_src;
         _emitter_t *emitter;
         _aaxEmitter *src;

         dptr_src = _intBufGet(he, _AAX_EMITTER, i);
         if (!dptr_src) continue;

         dest_rb->curr_pos_sec = 0.0f;

         emitter = _intBufGetDataPtr(dptr_src);
         src = emitter->source;
         if (_IS_PLAYING(src->props3d))
         {
            _intBufferData *dptr_sbuf;
            unsigned int nbuf;
            int streaming;

            rv = AAX_TRUE;

            nbuf = _intBufGetNum(src->buffers, _AAX_EMITTER_BUFFER);
            assert(nbuf > 0);

            streaming = (nbuf > 1);
            dptr_sbuf = _intBufGet(src->buffers, _AAX_EMITTER_BUFFER, src->pos);
            if (dptr_sbuf)
            {
               _embuffer_t *embuf = _intBufGetDataPtr(dptr_sbuf);
               _oalRingBuffer *src_rb = embuf->ringbuffer;
               unsigned int res = 0;
               do
               {
                  _oalRingBuffer2dProps *ep2d = src->props2d;

                  if (_IS_STOPPED(src->props3d)) {
                     _oalRingBufferStop(src_rb);
                  }
                  else if (_oalRingBufferGetParami(src_rb, RB_IS_PLAYING) == 0)
                  {
                     if (streaming) {
                        _oalRingBufferStartStreaming(src_rb);
                     } else {
                        _oalRingBufferStart(src_rb);
                     }
                  }

                  --src->update_ctr;
                   
                  /* 3d mixing */
                  if (stage == 2)
                  {
                     assert(_IS_POSITIONAL(src->props3d));

                     if (!src->update_ctr) {
                        be->prepare3d(src, info, ssv, sdf, 
                                      fp2d->speaker, fdp3d_m);
                     }

                     res = AAX_FALSE;
                     if (src->curr_pos_sec >= ep2d->dist_delay_sec) {
                        res = be->mix3d(be_handle, dest_rb, src_rb, ep2d,
                                       fp2d, emitter->track, src->update_ctr,
                                       nbuf, info->mode);
                     }
                  }
                  else
                  {
                     assert(!_IS_POSITIONAL(src->props3d));
                     res = be->mix2d(be_handle, dest_rb, src_rb, ep2d,
                                           fp2d, src->update_ctr, nbuf);
                  }

                  if (!src->update_ctr) {
                     src->update_ctr = src->update_rate;
                  }

                  src->curr_pos_sec += dt;

                  /*
                   * The current buffer of the source has finished playing.
                   * Decide what to do next.
                   */
                  if (res)
                  {
                     if (streaming)
                     {
                        /* is there another buffer ready to play? */
                        if (++src->pos == nbuf)
                        {
                           /*
                            * The last buffer was processed, return to the
                            * first buffer or stop? 
                            */
                           if TEST_FOR_TRUE(emitter->looping) {
                              src->pos = 0;
                           }
                           else
                           {
                              _SET_STOPPED(src->props3d);
                              _SET_PROCESSED(src->props3d);
                              break;
                           }
                        }

                        res &= _oalRingBufferGetParami(dest_rb, RB_IS_PLAYING);
                        if (res)
                        {
                           _intBufReleaseData(dptr_sbuf,_AAX_EMITTER_BUFFER);
                           dptr_sbuf = _intBufGet(src->buffers,
                                              _AAX_EMITTER_BUFFER, src->pos);
                           embuf = _intBufGetDataPtr(dptr_sbuf);
                           src_rb = embuf->ringbuffer;
                        }
                     }
                     else /* !streaming */
                     {
                        _SET_PROCESSED(src->props3d);
                        break;
                     }
                  }
               }
               while (res);
               _intBufReleaseData(dptr_sbuf, _AAX_EMITTER_BUFFER);
            }
            _intBufReleaseNum(src->buffers, _AAX_EMITTER_BUFFER);
            _oalRingBufferStart(dest_rb);
         }
         _intBufReleaseData(dptr_src, _AAX_EMITTER);
      }
      _intBufReleaseNum(he, _AAX_EMITTER);

      /*
       * stage == 2 is 3d positional audio
       * stage == 1 is stereo audio
       */
      if (stage == 2) {
         he = e2d;	/* switch to stereo */
      }
   }
   while (--stage); /* process 3d positional and stereo emitters */

   return rv;
}


/**
 * ssv:     sensor velocity vector
 * de:      sensor doppler factor
 * speaker: parent frame p2d->speaker position
 * fp3d:    parent frame dp3d->dprops3d
 */
void
_aaxEmitterPrepare3d(_aaxEmitter *src,  const _aaxMixerInfo* info, float ssv, float sdf, vec4_t *speaker, _oalRingBufferDelayed3dProps* fdp3d_m)
{
   _oalRingBufferPitchShiftFunc* dopplerfn;
   _oalRingBufferDelayed3dProps *edp3d, *edp3d_m;
   _oalRingBuffer3dProps *ep3d;
   _oalRingBuffer2dProps *ep2d;
   _oalRingBufferDistFunc* distfn;

   _AAX_LOG(LOG_DEBUG, __FUNCTION__);

   assert(src);
   assert(info);

   ep3d = src->props3d;
   edp3d = ep3d->dprops3d;
   edp3d_m = ep3d->m_dprops3d;
   ep2d = src->props2d;

   /* get pitch and gain before it is pushed to the delay queue */
   edp3d->pitch = _EFFECT_GET(ep2d, PITCH_EFFECT, AAX_PITCH);
   edp3d->gain = _FILTER_GET(ep2d, VOLUME_FILTER, AAX_GAIN);

   // _aaxEmitterProcessDelayQueue
   if (_PROP3D_DISTQUEUE_IS_DEFINED(edp3d) && src->p3dq)
   {
      _oalRingBufferDelayed3dProps *sdp3d = NULL;
      _intBufferData *buf3dq;
      float pos3dq;

      _intBufAddData(src->p3dq, _AAX_DELAYED3D, edp3d);
      if (src->curr_pos_sec <= ep2d->dist_delay_sec) {
         return;
      }

      pos3dq = ep2d->bufpos3dq;
      ep2d->bufpos3dq += ep3d->buf3dq_step;
      if (pos3dq <= 0.0f) return;

      do
      {
         buf3dq = _intBufPopData(src->p3dq, _AAX_DELAYED3D);
         if (buf3dq)
         {
            sdp3d = _intBufGetDataPtr(buf3dq);
            free(buf3dq);
         }
         --ep2d->bufpos3dq;
      }
      while (ep2d->bufpos3dq > 1.0f);

      if (!sdp3d) {                  // TODO: get from buffer cache
         sdp3d = _aaxDelayed3dPropsDup(edp3d);
      }

      ep3d->dprops3d = sdp3d;
      edp3d = ep3d->dprops3d;
   }

   distfn = _FILTER_GET_DATA(ep3d, DISTANCE_FILTER);
   dopplerfn = _EFFECT_GET_DATA(ep3d, VELOCITY_EFFECT);
   assert(dopplerfn);
   assert(distfn);

   /* only update when the matrix and/or the velocity vector has changed */
   if (_PROP3D_MTXSPEED_HAS_CHANGED(edp3d) ||
       _PROP3D_MTXSPEED_HAS_CHANGED(fdp3d_m))
   {
      vec4_t epos;
      float dist_fact, cone_volume = 1.0f;
      float refdist, maxdist, rolloff;
      float dist, esv, vs;
      unsigned int i, t;
      float gain, pitch;
      float min, max;

      _PROP3D_SPEED_CLEAR_CHANGED(edp3d);
      _PROP3D_MTX_CLEAR_CHANGED(edp3d);

      /**
       * align the emitter with the parent frame.
       * (compensate for the parents direction offset)
       */
      mtx4Mul(edp3d_m->matrix, fdp3d_m->matrix, edp3d->matrix);
      dist = vec3Normalize(epos, edp3d_m->matrix[LOCATION]);
#if 0
 printf("# emitter parent:\t\t\t\temitter:\n");
 PRINT_MATRICES(fdp3d_m->matrix, edp3d->matrix);
 printf("# modified emitter\n");
 PRINT_MATRIX(edp3d_m->matrix);
 printf("# dist: %f\n", dist);
#endif

      /* calculate the sound velocity inbetween the emitter and the sensor */
      esv = _EFFECT_GET(ep3d, VELOCITY_EFFECT, AAX_SOUND_VELOCITY);
      vs = (esv+ssv) / 2.0f;

      /*
       * Doppler
       */
      pitch = edp3d->pitch;
      if (dist > 1.0f)
      {
         float ve, vf, df;

         /* align velocity vectors with the modified emitter position
          * relative to the sensor
          */
         mtx4Mul(edp3d_m->velocity, fdp3d_m->velocity, edp3d->velocity);

         vf = 0.0f;
         ve = vec3DotProduct(edp3d_m->velocity[LOCATION], epos);
         df = dopplerfn(vf, ve, vs/sdf);
#if 0
 printf("velocity: %3.2f, %3.2f, %3.2f\n", edp3d_m->velocity[LOCATION][0], edp3d_m->velocity[LOCATION][1], edp3d_m->velocity[LOCATION][2]);
 printf("parent velocity:\t\t\t\temitter velocity:\n");
 PRINT_MATRICES(fdp3d_m->velocity, edp3d->velocity);
 printf("# modified emitter velocity\n");
 PRINT_MATRIX(edp3d_m->velocity);
 printf("doppler: %f, ve: %f, vs: %f\n\n", df, ve, vs/sdf);
#endif
         pitch *= df;
         ep3d->buf3dq_step = df;
      }
      ep2d->final.pitch = pitch;

      /*
       * Distance queues for every speaker (volume)
       */
      gain = edp3d->gain;

      refdist = _FILTER_GETD3D(src, DISTANCE_FILTER, AAX_REF_DISTANCE);
      maxdist = _FILTER_GETD3D(src, DISTANCE_FILTER, AAX_MAX_DISTANCE);
      rolloff = _FILTER_GETD3D(src, DISTANCE_FILTER, AAX_ROLLOFF_FACTOR);
      dist_fact = _MIN(dist/refdist, 1.0f);

      switch (info->mode)
      {
      case AAX_MODE_WRITE_HRTF:
         for (t=0; t<info->no_tracks; t++)
         {
            for (i=0; i<3; i++)
            {
               float dp = vec3DotProduct(speaker[3*t+i], epos);
               float offs, fact;

               ep2d->speaker[t][i] = dp * dist_fact;  /* -1 .. +1 */

               dp = 0.5f+dp/2.0f;  /* 0 .. +1 */
               if (i == DIR_BACK) dp *= dp;
               if (i == DIR_UPWD) dp = 0.25f*(5.0f*dp - dp*dp);

               offs = info->hrtf[HRTF_OFFSET][i];
               fact = info->hrtf[HRTF_FACTOR][i];
               ep2d->hrtf[t][i] = info->hrtf[HRTF_OFFSET][i];
               ep2d->hrtf[t][i] = _MAX(offs+dp*fact, 0.0f);
            }
         }
         break;
      case AAX_MODE_WRITE_SURROUND:
         for (t=0; t<info->no_tracks; t++)
         {
#ifdef USE_SPATIAL_FOR_SURROUND
            float dp = vec3DotProduct(speaker[t], epos);
            ep2d->speaker[t][0] = 0.5f + dp * dist_fact;
#else
            vec4Mulvec4(ep2d->speaker[t], speaker[t], epos);
            vec4ScalarMul(ep2d->speaker[t], dist_fact);
#endif
            for (i=1; i<2; i++) /* skip left-right and back-front */
            {
               float dp = vec3DotProduct(speaker[3*t+i], epos);
               float offs, fact;

               ep2d->speaker[t][i] = dp * dist_fact;  /* -1 .. +1 */

               dp = 0.5f+dp/2.0f;  /* 0 .. +1 */
               if (i == DIR_BACK) dp *= dp;
               if (i == DIR_UPWD) dp = 0.25f*(5.0f*dp - dp*dp);

               offs = info->hrtf[HRTF_OFFSET][i];
               fact = info->hrtf[HRTF_FACTOR][i];
               ep2d->hrtf[t][i] = info->hrtf[HRTF_OFFSET][i];
               ep2d->hrtf[t][i] = _MAX(offs+dp*fact, 0.0f);
            }
         }
         break;
      case AAX_MODE_WRITE_SPATIAL:
         for (t=0; t<info->no_tracks; t++)
         {                      /* speaker == sensor_pos */
            float dp = vec3DotProduct(speaker[t], epos);
            ep2d->speaker[t][0] = 0.5f + dp * dist_fact;
         }
         break;
      default: /* AAX_MODE_WRITE_STEREO */
         for (t=0; t<info->no_tracks; t++)
         {
            vec4Mulvec4(ep2d->speaker[t], speaker[t], epos);
            vec4ScalarMul(ep2d->speaker[t], dist_fact);
         }
      }

      gain *= distfn(dist, refdist, maxdist, rolloff, vs, 1.0f);

      /*
       * audio cone recalculaion
       */
      if (_PROP3D_CONE_IS_DEFINED(edp3d))
      {
         float inner_vec, tmp = -edp3d_m->matrix[DIR_BACK][2];

         inner_vec = _FILTER_GETD3D(src, ANGULAR_FILTER, AAX_INNER_ANGLE);
         if (tmp < inner_vec)
         {
            float outer_vec, outer_gain;
            outer_vec = _FILTER_GETD3D(src, ANGULAR_FILTER, AAX_OUTER_ANGLE);
            outer_gain = _FILTER_GETD3D(src, ANGULAR_FILTER, AAX_OUTER_GAIN);
            if (outer_vec < tmp)
            {
               tmp -= inner_vec;
               tmp *= (outer_gain - 1.0f);
               tmp /= (outer_vec - inner_vec);
               cone_volume = (1.0f + tmp);
            } else {
               cone_volume = outer_gain;
            }
         }
      }
      gain *= cone_volume;

      min = _FILTER_GET2D(src, VOLUME_FILTER, AAX_MIN_GAIN);
      max = _FILTER_GET2D(src, VOLUME_FILTER, AAX_MAX_GAIN);
      ep2d->final.gain = _MINMAX(gain, min, max);
   }
}

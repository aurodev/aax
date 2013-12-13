 /*
 * Copyright 2007-2013 by Erik Hofman.
 * Copyright 2009-2013 by Adalin B.V.
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

#ifdef HAVE_RMALLOC_H
# include <rmalloc.h>
#else
# include <string.h>
#endif
#ifdef HAVE_LIBIO_H
#include <libio.h>              /* for NULL */
#endif
#include <math.h>		/* for floorf */
#include <assert.h>

#include <xml.h>

#include "api.h"
#include "software/arch.h"
#include "software/audio.h"

static int _bufProcessAAXS(_buffer_t*, const void*, float);

static unsigned char  _aaxFormatsBPS[AAX_FORMAT_MAX];

AAX_API aaxBuffer AAX_APIENTRY
aaxBufferCreate(aaxConfig config, unsigned int samples, unsigned channels,
                                   enum aaxFormat format)
{
   unsigned int native_fmt = format & AAX_FORMAT_NATIVE;
   aaxBuffer rv = NULL;

   if ((native_fmt < AAX_FORMAT_MAX) && (samples*channels > 0))
   {
      _buffer_t* buf = calloc(1, sizeof(_buffer_t));
      if (buf)
      {
         _aaxRingBuffer *rb = _aaxRingBufferCreate(0.0f);
         if (rb)
         {
            _handle_t* handle = (_handle_t*)config;
            _aaxCodec** codecs;
            int blocksize;

            if (handle) {
               codecs = handle->backend.ptr->codecs;
            } else {
               codecs = _aaxRingBufferCodecs;
            }

            /* initialize the ringbuffer in native format only */
            rb->set_format(rb->id, buf->codecs, native_fmt);
            rb->set_parami(rb->id, RB_NO_SAMPLES, samples);
            rb->set_parami(rb->id, RB_NO_TRACKS, channels);
            /* Postpone until aaxBufferSetData gets called
             * rb->init(rb->id, AAX_FALSE);
            */

            switch(native_fmt)
            {
            case AAX_IMA4_ADPCM:
               blocksize = DEFAULT_IMA4_BLOCKSIZE;
               break;
            default:
               blocksize = 1;
            }

            buf->id = BUFFER_ID;
            buf->ref_counter = 1;

            buf->blocksize = blocksize;
            buf->pos = 0;
            buf->format = format;
            buf->frequency = 0.0f;

            buf->mipmap = AAX_FALSE;

            buf->ringbuffer = rb;
            buf->info = VALID_HANDLE(handle) ? handle->info : NULL;
            buf->codecs = codecs;

            rv = (aaxBuffer)buf;
         }
         else
         {
            free(buf);
            buf = NULL;
         }
      }
      if (buf == NULL) {
         _aaxErrorSet(AAX_INSUFFICIENT_RESOURCES);
      }
   }
   else
   {
      if (native_fmt >= AAX_FORMAT_MAX) {
         _aaxErrorSet(AAX_INVALID_ENUM);
      } else {
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      }
   }
   return rv;
}

AAX_API int AAX_APIENTRY
aaxBufferSetSetup(aaxBuffer buffer, enum aaxSetupType type, unsigned int setup)
{
   _buffer_t* buf = get_buffer(buffer);
   int rv = AAX_FALSE;
   if (buf)
   {
      _aaxRingBuffer* rb = buf->ringbuffer;
      switch(type)
      {
      case AAX_FREQUENCY:
         if ((setup > 1000) && (setup < 96000))
         {
            if (!buf->frequency) {
               rb->set_paramf(rb->id, RB_FREQUENCY, (float)setup);
            }
            buf->frequency = (float)setup;
            rv = AAX_TRUE;
         }
         else _aaxErrorSet(AAX_INVALID_PARAMETER);
         break;
      case AAX_TRACKS:
         rv = rb->set_parami(rb->id, RB_NO_TRACKS, setup);
         if (!rv) {
            _aaxErrorSet(AAX_INVALID_PARAMETER);
         }
         break;
      case AAX_FORMAT:
      {
         enum aaxFormat native_fmt = setup & AAX_FORMAT_NATIVE;
         if (native_fmt < AAX_FORMAT_MAX)
         {
            buf->format = setup;
            switch(native_fmt)
            {
            case AAX_IMA4_ADPCM:
               buf->blocksize = DEFAULT_IMA4_BLOCKSIZE;
               break;
            default:
               buf->blocksize = 1;
               break;
            }
            rv = AAX_TRUE;
         }
         else _aaxErrorSet(AAX_INVALID_PARAMETER);
         break;
      }
      case AAX_TRACKSIZE:
         rv = rb->set_parami(rb->id, RB_TRACKSIZE, setup);
         if (!rv) {
            _aaxErrorSet(AAX_INVALID_PARAMETER);
         }
         break;
      case AAX_LOOP_START:
         if (setup < rb->get_parami(rb->id, RB_NO_SAMPLES))
         {
            unsigned int end = rb->get_parami(rb->id, RB_LOOPPOINT_END);
            rb->set_parami(rb->id, RB_LOOPPOINT_START, setup);
            rb->set_parami(rb->id, RB_LOOPING, (setup < end) ? AAX_TRUE : AAX_FALSE);
            rv = AAX_TRUE;
         }
         else _aaxErrorSet(AAX_INVALID_PARAMETER);
         break;
      case AAX_LOOP_END:
         if (setup < rb->get_parami(rb->id, RB_NO_SAMPLES))
         {
            unsigned int start = rb->get_parami(rb->id, RB_LOOPPOINT_START);
            rb->set_parami(rb->id, RB_LOOPPOINT_END, setup);
            rb->set_parami(rb->id, RB_LOOPING, (start < setup) ? AAX_TRUE : AAX_FALSE);
            rv = AAX_TRUE;
         }
         else _aaxErrorSet(AAX_INVALID_PARAMETER);
         break;
      case AAX_BLOCK_ALIGNMENT:
         if (setup > 1)
         {
            if (buf->format == AAX_IMA4_ADPCM)
            {
               buf->blocksize = setup;
               rv = AAX_TRUE;
            }
         }
         else if (buf->format != AAX_IMA4_ADPCM)
         {
            buf->blocksize = setup;
            rv = AAX_TRUE;
         }
         break;
      case AAX_POSITION:
         if (setup <= rb->get_parami(rb->id, RB_NO_SAMPLES)) {
            buf->pos = setup;
            rv = AAX_TRUE;
         }
         else  _aaxErrorSet(AAX_INVALID_PARAMETER);
         break;
      default:
         _aaxErrorSet(AAX_INVALID_ENUM);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

AAX_API unsigned int AAX_APIENTRY
aaxBufferGetSetup(const aaxBuffer buffer, enum aaxSetupType type)
{
   _buffer_t* buf = get_buffer(buffer);
   unsigned int rv = AAX_FALSE;
   if (buf)
   {
      _aaxRingBuffer* rb = buf->ringbuffer;
      switch(type)
      {
      case AAX_FREQUENCY:
         rv = (unsigned int)buf->frequency;
         break;
      case AAX_TRACKS:
         rv = rb->get_parami(rb->id, RB_NO_TRACKS);
         break;
      case AAX_FORMAT:
         rv = rb->get_parami(rb->id, RB_FORMAT);
         break;
      case AAX_TRACKSIZE:
         if (buf->frequency)
         {
            float fact = buf->frequency/rb->get_paramf(rb->id, RB_FREQUENCY);
            rv = rb->get_parami(rb->id, RB_NO_SAMPLES) - buf->pos;
            rv *= (unsigned int)(fact*rb->get_parami(rb->id, RB_BYTES_SAMPLE));
         }
         else _aaxErrorSet(AAX_INVALID_STATE);
         break;
      case AAX_NO_SAMPLES:
          if (buf->frequency)
         {
            float fact = buf->frequency/rb->get_paramf(rb->id, RB_FREQUENCY);
            rv = (unsigned int)(fact*(rb->get_parami(rb->id, RB_NO_SAMPLES)-buf->pos));
         }
         else _aaxErrorSet(AAX_INVALID_STATE);
         break;
      case AAX_LOOP_START:
         rv = rb->get_parami(rb->id, RB_LOOPPOINT_START);
         break;
      case AAX_LOOP_END:
         rv = rb->get_parami(rb->id, RB_LOOPPOINT_END);
         break;
      case AAX_BLOCK_ALIGNMENT:
         rv = buf->blocksize;
         break;
      case AAX_POSITION:
         rv = buf->pos;
         break;
      default:
         _aaxErrorSet(AAX_INVALID_ENUM);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

AAX_API int AAX_APIENTRY
aaxBufferSetData(aaxBuffer buffer, const void* d)
{
   _buffer_t* buf = get_buffer(buffer);
   int rv = AAX_FALSE;
   if (buf && (buf->format & AAX_SPECIAL))
   {				/* the data in *d isn't actual sample data */
      unsigned int format = buf->format;
      switch(format)
      {
      case AAX_AAXS16S:
      case AAX_AAXS24S:
         rv = _bufProcessAAXS(buf, d, 0);
         break;
      default:					/* should never happen */
         break;
      }
   }
   else if (buf)
   {
      unsigned int tracks, no_samples, buf_samples;
      _aaxRingBuffer *rb = buf->ringbuffer;

      rb->init(rb->id, AAX_FALSE);
      tracks = rb->get_parami(rb->id, RB_NO_TRACKS);
      no_samples = rb->get_parami(rb->id, RB_NO_SAMPLES);

      buf_samples = tracks*no_samples;
      if (d && (buf_samples > 0))
      {
         unsigned blocksize =  buf->blocksize;
         unsigned int format = buf->format;
         void *data = (void*)d, *m = NULL;
         unsigned int native_fmt;
         char fmt_bps;

				/* do we need to convert to native format? */
         native_fmt = format & AAX_FORMAT_NATIVE;
         if (format & ~AAX_FORMAT_NATIVE)
         {
            fmt_bps = _aaxFormatsBPS[native_fmt];
            m = malloc(buf_samples*fmt_bps);
            if (!m)
            {
               _aaxErrorSet(AAX_INSUFFICIENT_RESOURCES);
               return rv;
            }

            _aax_memcpy(m, data, buf_samples*fmt_bps);
            data = m;
					/* first convert to native endianness */
            if ( ((format & AAX_FORMAT_LE) && is_bigendian()) ||
                 ((format & AAX_FORMAT_BE) && !is_bigendian()) )
            {
               switch (native_fmt)
               {
               case AAX_PCM16S:
                  _batch_endianswap16(data, buf_samples);
                  break;
               case AAX_PCM24S:
               case AAX_PCM32S:
               case AAX_FLOAT:
                  _batch_endianswap32(data, buf_samples);
                  break;
               case AAX_DOUBLE:
                  _batch_endianswap64(data, buf_samples);
                  break;
               default:
                  break;
               }
            }
					/* then convert to proper signedness */
            if (format & AAX_FORMAT_UNSIGNED)
            {
               switch (native_fmt)
               {  
               case AAX_PCM8S:
                  _batch_cvt8u_8s(data, buf_samples);
                  break;
               case AAX_PCM16S:
                  _batch_cvt16u_16s(data, buf_samples);
                  break;
               case AAX_PCM24S:
                  _batch_cvt24u_24s(data, buf_samples);
                  break;
               case AAX_PCM32S:
                  _batch_cvt32u_32s(data, buf_samples);
                  break;
               default:
                  break;
               }
            }
          }
         rb->set_data_interleaved(rb->id, data, blocksize, 0);
         rv = AAX_TRUE;
         free(m);
      }
      else {
         _aaxErrorSet(AAX_INVALID_PARAMETER);
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

AAX_API int AAX_APIENTRY
aaxBufferProcessWaveform(aaxBuffer buffer, float rate, enum aaxWaveformType wtype, float ratio, enum aaxProcessingType ptype)
{
   _buffer_t* buf = get_buffer(buffer);
   int rv = AAX_FALSE;

   if (wtype > AAX_LAST_WAVEFORM) {
      _aaxErrorSet(AAX_INVALID_PARAMETER + 3);
   } else if (ratio > 1.0f || ratio < -1.0f) {
      _aaxErrorSet(AAX_INVALID_PARAMETER + 4);
   } else if (ptype >= AAX_PROCESSING_MAX) {
      _aaxErrorSet(AAX_INVALID_PARAMETER + 5);
   }
   else if (buf && EBF_VALID(buf))
   {
      _aaxRingBuffer* rb = buf->ringbuffer;
      float phase = (ratio < 0.0f) ? GMATH_PI : 0.0f;
      unsigned int no_samples, i, bit = 1;
      unsigned skip;
      float f, fs, fw;

      ratio = fabsf(ratio);
      fw = FNMINMAX(rate, 1.0f, 22050.0f);
      skip = (unsigned char)(1.0f + 99.0f*_MINMAX(rate, 0.0f, 1.0f));

      fs = rb->get_paramf(rb->id, RB_FREQUENCY);
      no_samples = rb->get_parami(rb->id, RB_NO_SAMPLES);

      if (rb->get_state(rb->id, RB_IS_VALID))
      {
         float dt = no_samples/fs;
         fw = floorf((fw*dt)+0.5f)/dt;
      }
      else
      {
         float dt = rb->get_parami(rb->id, RB_NO_SAMPLES_AVAIL)/fs;
         float duration = floorf((fw*dt)+0.5f)/fw;

         no_samples = (unsigned int)ceilf(duration*fs);
         rb->set_parami(rb->id, RB_NO_SAMPLES, no_samples);
         rb->init(rb->id, AAX_FALSE);
      }
      f = fs/fw;

      switch (ptype)
      {
      case AAX_OVERWRITE:
         rb->set_state(rb->id, RB_CLEARED);
         break;
      case AAX_MIX:
      {
         float ratio_orig = FNMINMAX(1.0f-ratio, 0.0f, 1.0f);

         ratio = 2.0f*(1.0f - ratio_orig);
         if (wtype & AAX_SINE_WAVE) ratio /= 2;
         if (wtype & AAX_SQUARE_WAVE) ratio /= 2;
         if (wtype & AAX_TRIANGLE_WAVE) ratio /= 2;
         if (wtype & AAX_SAWTOOTH_WAVE) ratio /= 2;
         if (wtype & AAX_IMPULSE_WAVE) ratio /= 2;
         if (wtype & AAX_WHITE_NOISE) ratio /= 2;
         if (wtype & AAX_PINK_NOISE) ratio /= 2;
         if (wtype & AAX_BROWNIAN_NOISE) ratio /= 2;

         rb->data_multiply(rb, 0, 0, ratio_orig);
         break;
      }
      case AAX_RINGMODULATE:
         ratio = -ratio;
         break;
      default:
         break;
      }

      rv = AAX_TRUE;
      for (i=0; i<AAX_MAX_WAVE; i++)
      {
         float dc = 1.0; /* duty cicle for noise */
         switch (wtype & bit)
         {
         case AAX_SINE_WAVE:
         case AAX_SQUARE_WAVE:
         case AAX_TRIANGLE_WAVE:
         case AAX_SAWTOOTH_WAVE:
         case AAX_IMPULSE_WAVE:
            rv = rb->data_mix_waveform(rb, wtype & bit, f, ratio, phase);
            break;
         case AAX_WHITE_NOISE:
         case AAX_PINK_NOISE:
         case AAX_BROWNIAN_NOISE:
            rv = rb->data_mix_noise(rb, wtype & bit,
                             buf->info->frequency ? buf->info->frequency : 0.0f,
                             ratio, dc, skip);
            break;
         default:
            break;
         }
         bit <<= 1;
      }
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

AAX_API void** AAX_APIENTRY
aaxBufferGetData(const aaxBuffer buffer)
{
   _buffer_t* buf = get_buffer(buffer);
   void** data = NULL;
   if (buf && buf->frequency)
   {
      unsigned int buf_samples, no_samples, tracks;
      unsigned int native_fmt, rb_format, pos;
      enum aaxFormat user_format;
      _aaxRingBuffer *rb;
      char *ptr, bps;
      float fact;

      rb = buf->ringbuffer;
      fact = buf->frequency / rb->get_paramf(rb->id, RB_FREQUENCY);
      pos = (unsigned int)(fact*buf->pos);

      no_samples = (unsigned int)(fact*rb->get_parami(rb->id, RB_NO_SAMPLES) - pos);
      bps = rb->get_parami(rb->id, RB_BYTES_SAMPLE);
      tracks = rb->get_parami(rb->id, RB_NO_TRACKS);
      buf_samples = tracks*no_samples;

      ptr = (char*)sizeof(void*);
      data = (void**)_aax_malloc(&ptr, no_samples*tracks*bps);
      if (data == NULL) 
      {
         _aaxErrorSet(AAX_INSUFFICIENT_RESOURCES);
         return data;
      }

      rb->get_data_interleaved(rb->id, ptr, no_samples, tracks, fact);
      *data = (void*)(ptr + pos*tracks*bps);

      user_format = buf->format;
      native_fmt = user_format & AAX_FORMAT_NATIVE;
      rb_format = rb->get_parami(rb->id, RB_FORMAT);
      if (rb_format != native_fmt)
      {
         if (rb_format != AAX_PCM24S) 	/* first convert to signed 24-bit */
         {
            void **ndata;
            char *ptr;

            ptr = (char*)sizeof(void*);
            ndata = (void**)_aax_malloc(&ptr, buf_samples*sizeof(int32_t));
            if (ndata)
            {
               *ndata = (void*)ptr;
               bufConvertDataToPCM24S(*ndata, *data, buf_samples, rb_format);
               free(data);
               data = ndata;
            }
         } 

         if (native_fmt != AAX_PCM24S)	/* then convert to requested format */
         {
            int new_bps = aaxGetBytesPerSample(native_fmt);
            int block_smp = BLOCKSIZE_TO_SMP(buf->blocksize);
            int new_samples = ((no_samples/block_smp)+1)*block_smp;
            void** ndata;
            char *ptr;

            ptr = (char*)sizeof(void*);
            ndata = (void**)_aax_malloc(&ptr, tracks*new_samples*new_bps);
            if (ndata)
            {
               *ndata = (void*)ptr;
               bufConvertDataFromPCM24S(*ndata, *data, tracks, no_samples,
                                         native_fmt, buf->blocksize);
               free(data);
               data = ndata;
            }
         }
      } /* rb_format != native_fmt */
 
			/* do we need to convert to non-native format? */
      if (user_format & ~AAX_FORMAT_NATIVE)
      {
					/* do we need to change signedness? */
         if (user_format & AAX_FORMAT_UNSIGNED)
         {
            int signed_format = user_format & ~(AAX_FORMAT_LE | AAX_FORMAT_BE);
            switch (signed_format)
            {
            case AAX_PCM8U:
               _batch_cvt8s_8u(*data, buf_samples);
               break;
            case AAX_PCM16U:
               _batch_cvt16s_16u(*data, buf_samples);
               break;
            case AAX_PCM24U:
               _batch_cvt24s_24u(*data, buf_samples);
               break;
            case AAX_PCM32U:
               _batch_cvt32s_32u(*data, buf_samples);
               break;
            default:
               break;
            }
         }
					/* convert to requested endianness */
         if ( ((user_format & AAX_FORMAT_LE) && is_bigendian()) ||
              ((user_format & AAX_FORMAT_BE) && !is_bigendian()) )
         {
            switch (native_fmt)
            {
            case AAX_PCM16S:
               _batch_endianswap16(*data, buf_samples);
               break;
            case AAX_PCM24S:
            case AAX_PCM32S:
            case AAX_FLOAT:
               _batch_endianswap32(*data, buf_samples);
               break;
            case AAX_DOUBLE:
               _batch_endianswap64(*data, buf_samples);
               break;
            default:
               break;
            }
         }
      }
   }
   else if (buf) {	/* buf->frequency is not set */
      _aaxErrorSet(AAX_INVALID_STATE);
   } else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }

   return data;
}

AAX_API int AAX_APIENTRY
aaxBufferDestroy(aaxBuffer buffer)
{
   _buffer_t* buf = get_buffer(buffer);
   int rv = AAX_FALSE;
   if (buf) {
     rv = free_buffer(buf);
   }
   else {
      _aaxErrorSet(AAX_INVALID_HANDLE);
   }
   return rv;
}

AAX_API int AAX_APIENTRY
aaxBufferWriteToFile(aaxBuffer buffer, const char *file, enum aaxProcessingType type)
{
   int rv = AAX_FALSE;
   if (aaxIsValid(buffer, AAX_BUFFER))
   {
      enum aaxFormat format = aaxBufferGetSetup(buffer, AAX_FORMAT);
      unsigned int samples = aaxBufferGetSetup(buffer, AAX_NO_SAMPLES);
      unsigned int freq = aaxBufferGetSetup(buffer, AAX_FREQUENCY);
      char tracks = aaxBufferGetSetup(buffer, AAX_TRACKS);
#if 0
      _buffer_t* buf = get_buffer(buffer);
      _aaxRingBuffer* rb = buf->ringbuffer;
      _aaxRingBufferSample *rbd = rb->sample;
      const int32_t **data;

      data = (const int32_t**)rbd->track;
      rv = _aaxFileDriverWrite(file, type, data, samples, freq, tracks, format);
#else
      void **data = aaxBufferGetData(buffer);
      _aaxFileDriverWrite(file, type, *data, samples, freq, tracks, format);
      free(data);
      rv = AAX_TRUE;
#endif
   }
   return rv;
}

/* -------------------------------------------------------------------------- */

static unsigned char  _aaxFormatsBPS[AAX_FORMAT_MAX] =
{
  1,    /* 8-bit          */
  2,    /* 16-bit         */
  4,    /* 24-bit         */
  4,    /* 32-bit         */
  4,    /* 32-bit floats  */
  8,    /* 64-bit doubles */
  1,    /* mu-law         */
  1,    /* a-law          */
  1     /* IMA4-ADPCM     */
};

_buffer_t*
get_buffer(aaxBuffer buffer)
{
   _buffer_t *handle = (_buffer_t *)buffer;
   _buffer_t* rv  = NULL;
   if (handle && handle->id == BUFFER_ID) {
      rv = handle;
   }
   return rv;
}

int
free_buffer(_buffer_t* buf)
{
   int rv = AAX_FALSE;
   if (buf)
   {
      if (--buf->ref_counter == 0)
      {
         _aaxRingBufferDestroy(buf->ringbuffer);
         buf->ringbuffer = NULL;

         /* safeguard against using already destroyed handles */
         buf->id = 0xdeadbeef;
         free(buf);
      }
      rv = AAX_TRUE;
   }
   return rv;
}

static int
_bufProcessAAXS(_buffer_t* buf, const void* d, float freq)
{
   int rv = AAX_FALSE;
   void *xid;

   assert(d);

   xid = xmlInitBuffer(d, strlen(d));
   if (xid)
   {
      void *xsid = xmlNodeGet(xid, "/sound");
      if (xsid)
      {
         unsigned int i, num = xmlNodeGetNum(xsid, "waveform");
         void *xwid = xmlMarkId(xsid);

         if (!freq) freq = xmlAttributeGetDouble(xsid, "freq_hz");
         if (!freq) freq = 1000.0f;

         for (i=0; i<num; i++)
         {
            if (xmlNodeGetPos(xsid, xwid, "waveform", i) != 0)
            {
               enum aaxProcessingType ptype = AAX_OVERWRITE;
               enum aaxWaveformType wtype = AAX_SINE_WAVE;
               float pitch, ratio;

               ratio = xmlNodeGetDouble(xwid, "ratio");
               pitch = xmlNodeGetDouble(xwid, "pitch");

               if (!xmlAttributeCompareString(xwid, "src", "brownian-noise")) 
               {
                   wtype = AAX_BROWNIAN_NOISE;
               }
               else if (!xmlAttributeCompareString(xwid, "src", "white-noise"))
               {
                   wtype = AAX_WHITE_NOISE;
               } 
               else if (!xmlAttributeCompareString(xwid, "src", "pink-noise")) 
               {
                   wtype = AAX_PINK_NOISE;
               }
               else if (!xmlAttributeCompareString(xwid, "src", "square"))
               {
                  wtype = AAX_SQUARE_WAVE;
               }
               else if (!xmlAttributeCompareString(xwid, "src", "triangle"))
               {
                   wtype = AAX_TRIANGLE_WAVE;
               }
               else if (!xmlAttributeCompareString(xwid, "src", "sawtooth"))
               {
                   wtype = AAX_SAWTOOTH_WAVE;
               }
               else if (!xmlAttributeCompareString(xwid, "src", "impulse"))
               {
                   wtype = AAX_IMPULSE_WAVE;
               }
               else	// !xmlAttributeCompareString(xwid, "src", "sine")
               {
                  wtype = AAX_SINE_WAVE;
               }

               if (!xmlNodeCompareString(xwid, "processing", "add"))
               {
                  ptype = AAX_ADD;
                  if (!ratio) ratio = 1.0f;
                  if (!pitch) pitch = 1.0f;
               }
               else if (!xmlNodeCompareString(xwid, "processing", "mix"))
               {
                  ptype = AAX_MIX;
                  if (!ratio) ratio = 0.5f;
                  if (!pitch) pitch = 1.0f;
               }
               else if (!xmlNodeCompareString(xwid, "processing", "modulate"))
               {
                  ptype = AAX_RINGMODULATE;
                  if (!ratio) ratio = 1.0f;
                  if (!pitch) pitch = 1.0f;
               }
               else  // !xmlNodeCompareString(xwid, "processing", "overwrite")
               {
                  ptype = AAX_OVERWRITE;
                  if (!ratio) ratio = 1.0f;
                  if (!pitch) pitch = 1.0f;
               }

               pitch *= freq;
               rv = aaxBufferProcessWaveform(buf, pitch, wtype, ratio, ptype);
               if (rv == AAX_FALSE) break;
            }
         }
         xmlFree(xwid);
         xmlFree(xsid);
      }
      xmlClose(xid);
   }

   return rv;
}



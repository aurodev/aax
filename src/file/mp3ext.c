/*
 * Copyright 2005-2012 by Erik Hofman.
 * Copyright 2009-2012 by Adalin B.V.
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

#include <stdlib.h>		/* for malloc */
#include <string.h>		/* for strdup */
#include <fcntl.h>		/* SEEK_*, O_* */
#include <assert.h>		/* assert */
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#if HAVE_STRINGS_H
# include <strings.h>
#endif
#if HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_IO_H
# include <io.h>
#endif

#include <aax/aax.h>

#include <ringbuffer.h>
#include <base/dlsym.h>
#include <arch.h>

#include "filetype.h"
#include "audio.h"

        /** libmpg123 */
static _open_fn _aaxMPG123FileOpen;
static _close_fn _aaxMPG123FileClose;
static _update_fn _aaxMPG123FileReadWrite;
	/** libmpg123 */

#ifdef WINXP
static _open_fn _aaxMSACMFileOpen;
static _close_fn _aaxMSACMFileClose;
static _update_fn _aaxMSACMFileReadWrite;

static _open_fn *_aaxMP3FileOpen = (_open_fn*)&_aaxMSACMFileOpen;
static _close_fn *_aaxMP3FileClose = (_close_fn*)&_aaxMSACMFileClose;
static _update_fn *_aaxMP3FileReadWrite = (_update_fn*)&_aaxMSACMFileReadWrite;
#else
static _open_fn *_aaxMP3FileOpen = (_open_fn*)&_aaxMPG123FileOpen;
static _close_fn *_aaxMP3FileClose = (_close_fn*)&_aaxMPG123FileClose;
static _update_fn *_aaxMP3FileReadWrite = (_update_fn*)&_aaxMPG123FileReadWrite;
#endif

static _detect_fn _aaxMP3FileDetect;
static _new_hanle_fn _aaxMP3FileSetup;
static _default_fname_fn _aaxMP3FileInterfaces;
static _extension_fn _aaxMP3FileExtension;

static _get_param_fn _aaxMP3FileGetFormat;
static _get_param_fn _aaxMP3FileGetNoTracks;
static _get_param_fn _aaxMP3FileGetFrequency;
static _get_param_fn _aaxMP3FileGetBitsPerSample;

#ifdef WINXP
	/** windows (xp and later) native */
static int acmMP3Support = AAX_FALSE;

static BOOL CALLBACK acmDriverEnumCallback(HACMDRIVERID, DWORD, DWORD);

DECL_FUNCTION(acmDriverOpen);
DECL_FUNCTION(acmDriverClose);
DECL_FUNCTION(acmDriverEnum);
DECL_FUNCTION(acmDriverDetailsA);
DECL_FUNCTION(acmStreamOpen);
DECL_FUNCTION(acmStreamClose);
DECL_FUNCTION(acmStreamSize);
DECL_FUNCTION(acmStreamConvert);
DECL_FUNCTION(acmStreamPrepareHeader);
DECL_FUNCTION(acmStreamUnprepareHeader);
DECL_FUNCTION(acmFormatTagDetailsA);
#endif

	/** libmpg123 */
DECL_FUNCTION(mpg123_init);
DECL_FUNCTION(mpg123_exit);
DECL_FUNCTION(mpg123_new);
DECL_FUNCTION(mpg123_open_fd);
DECL_FUNCTION(mpg123_read);
DECL_FUNCTION(mpg123_delete);
DECL_FUNCTION(mpg123_format);
DECL_FUNCTION(mpg123_getformat);

_aaxFileHandle*
_aaxDetectMP3File()
{
   _aaxFileHandle* rv = NULL;

   rv = calloc(1, sizeof(_aaxFileHandle));
   if (rv)
   {
      rv->detect = (_detect_fn*)&_aaxMP3FileDetect;
      rv->setup = (_new_hanle_fn*)&_aaxMP3FileSetup;

      rv->open = _aaxMP3FileOpen;
      rv->close = _aaxMP3FileClose;
      rv->update = _aaxMP3FileReadWrite;

      rv->supported = (_extension_fn*)&_aaxMP3FileExtension;
      rv->interfaces = (_default_fname_fn*)&_aaxMP3FileInterfaces;

      rv->get_format = (_get_param_fn*)&_aaxMP3FileGetFormat;
      rv->get_no_tracks = (_get_param_fn*)&_aaxMP3FileGetNoTracks;
      rv->get_frequency = (_get_param_fn*)&_aaxMP3FileGetFrequency;
      rv->get_bits_per_sample = (_get_param_fn*)&_aaxMP3FileGetBitsPerSample;
   }
   return rv;
}

/* -------------------------------------------------------------------------- */

typedef struct
{
   char *name;
   void *id;

   int fd;
   int mode;
   int capturing;

   int frequency;
   enum aaxFormat format;
   uint8_t no_tracks;
   uint8_t bits_sample;

#ifdef WINXP
   HACMSTREAM acmStream;
   ACMSTREAMHEADER acmStreamHeader;

   LPBYTE pcmBuffer;
   unsigned long pcmBufPos;
   unsigned long pcmBufMax;

   BYTE mp3Buffer[MP3_BLOCK_SIZE];
#endif

} _handle_t;


static int
_aaxMP3FileDetect(int mode)
{
   static void *audio = NULL;
   int rv = AAX_FALSE;

   if (!audio)
   {
#ifdef WINXP
      audio = _oalIsLibraryPresent("msacm32", NULL);
      if (audio)
      {
         char *error;
         _oalGetSymError(0);

         TIE_FUNCTION(acmDriverOpen);
         if (pacmDriverOpen)
         {
            TIE_FUNCTION(acmDriverClose);
            TIE_FUNCTION(acmDriverEnum);
            TIE_FUNCTION(acmDriverDetailsA);
            TIE_FUNCTION(acmStreamOpen);
            TIE_FUNCTION(acmStreamClose);
            TIE_FUNCTION(acmStreamSize);
            TIE_FUNCTION(acmStreamConvert);
            TIE_FUNCTION(acmStreamPrepareHeader);
            TIE_FUNCTION(acmStreamUnprepareHeader);
            TIE_FUNCTION(acmFormatTagDetailsA);

            error = _oalGetSymError(0);
            if (!error)
            {
               if (!acmMP3Support) {
                  pacmDriverEnum(acmDriverEnumCallback, 0, 0);
               }

               if (acmMP3Support)
               {
//                _aaxMP3FileOpen = (_open_fn*)&_aaxMSACMFileOpen;
//                _aaxMP3FileClose = (_close_fn*)&_aaxMSACMFileClose;
//                _aaxMP3FileReadWrite = (_update_fn*)&_aaxMSACMFileReadWrite;
                  rv = AAX_TRUE;
               }
            }
         }
      }
#endif	/* WINXP */

      /* if not found, try mpg123 */
      if (!audio)
      {
         audio = _oalIsLibraryPresent("mpg123", "0");
         if (!audio) {
            audio = _oalIsLibraryPresent("libmpg123-0", "0");
         }

         if (audio)
         {
            char *error;
            _oalGetSymError(0);

            TIE_FUNCTION(mpg123_init);
            if (pmpg123_init)
            {
               TIE_FUNCTION(mpg123_exit);
               TIE_FUNCTION(mpg123_new);
               TIE_FUNCTION(mpg123_open_fd);
               TIE_FUNCTION(mpg123_read);
               TIE_FUNCTION(mpg123_delete);
               TIE_FUNCTION(mpg123_format);
               TIE_FUNCTION(mpg123_getformat);

               error = _oalGetSymError(0);
               if (!error) {
                  rv = AAX_TRUE;
               }
            }
         }
      }
   } /* !audio */
   return rv;
}

static void*
_aaxMP3FileSetup(int mode, int freq, int tracks, int format)
{
   _handle_t *handle = NULL;
   
   if(mode == 0)
   {
      handle = calloc(1, sizeof(_handle_t));
      if (handle)
      {
         static const int _mode[] = { 0, O_RDONLY|O_BINARY };

         handle->capturing = 1;
         handle->mode = _mode[handle->capturing];
         handle->frequency = freq;
         handle->no_tracks = tracks;
         handle->format = format;
         handle->bits_sample = aaxGetBitsPerSample(handle->format);
      }
      else {
         _AAX_FILEDRVLOG("MP3File: Insufficient memory");
      }
   }
   else {
      _AAX_FILEDRVLOG("MP3File: playback is not supported");
   }

   return (void*)handle;
}

static int
_aaxMP3FileExtension(char *ext) {
   return !strcasecmp(ext, "mp3");
}

static char*
_aaxMP3FileInterfaces(int mode)
{
   static const char *rd[2] = { "*.mp3\0", "\0" };
   return (char *)rd[mode];
}

static unsigned int
_aaxMP3FileGetFormat(void *id)
{
   _handle_t *handle = (_handle_t *)id;
   return handle->format;
}

static unsigned int
_aaxMP3FileGetNoTracks(void *id)
{
   _handle_t *handle = (_handle_t *)id;
   return handle->no_tracks;
}

static unsigned int
_aaxMP3FileGetFrequency(void *id)
{
   _handle_t *handle = (_handle_t *)id;
   return handle->frequency;
}

static unsigned int
_aaxMP3FileGetBitsPerSample(void *id)
{
   _handle_t *handle = (_handle_t *)id;
   return handle->bits_sample;
}

static int
getFormatFromMP3FileFormat(int enc)
{
   int rv;
   switch (enc)
   {
   case MPG123_ENC_8:
      rv = AAX_PCM8S;
      break;
   case MPG123_ENC_ULAW_8:
      rv = AAX_MULAW;
      break;
   case MPG123_ENC_ALAW_8:
      rv = AAX_ALAW;
      break;
   case MPG123_ENC_SIGNED_16:
      rv = AAX_PCM16S;
      break;
   case MPG123_ENC_SIGNED_24:
      rv = AAX_PCM24S;
      break;
   case MPG123_ENC_SIGNED_32:
      rv = AAX_PCM32S;
      break;
   default:
      rv = AAX_FORMAT_NONE;
   }
   return rv;
}

static int
_aaxMPG123FileOpen(void *id, const char* fname)
{
   _handle_t *handle = (_handle_t *)id;
   int res = -1;

   if (handle && fname)
   {
      handle->fd = open(fname, handle->mode, 0644);
      if (handle->fd >= 0)
      {
         handle->name = strdup(fname);
         if (handle->capturing)
         {
            pmpg123_init();
            handle->id = pmpg123_new(NULL, NULL);
            if (handle->id)
            {
               int enc, channels;
               long rate;

               pmpg123_open_fd(handle->id, handle->fd); 

               pmpg123_format(handle->id, handle->frequency,
                              MPG123_MONO | MPG123_STEREO,
                              MPG123_ENC_SIGNED_16);
               pmpg123_getformat(handle->id, &rate, &channels, &enc);

               if ((1000 <= rate) && (rate <= 192000) &&
                   (1 <= channels) && (channels <= _AAX_MAX_SPEAKERS))
               {
                  handle->frequency = rate;
                  handle->no_tracks = channels;
                  handle->format = getFormatFromMP3FileFormat(enc);
                  handle->bits_sample = aaxGetBitsPerSample(handle->format);

                  res = AAX_TRUE;
               }
               else {
                  _AAX_FILEDRVLOG("MP3File: file may be corrupt");
               }
            }
            else {
               _AAX_FILEDRVLOG("MP3File: Unable to create a handler");
            }
         }
         else
         {
            _AAX_FILEDRVLOG("MP3File: playback is not supported");
            close(handle->fd); /* no mp3 write support (yet) */
         }
      }
      else {
         _AAX_FILEDRVLOG("MP3File: file not found");
      }
   }
   else
   {
      if (!fname) {
         _AAX_FILEDRVLOG("MP3File: No filename prvided");
      } else {
         _AAX_FILEDRVLOG("MP3File: Internal error: handle id equals 0");
      }
   }

   return (res >= 0) ? AAX_TRUE : AAX_FALSE;
}

static int
_aaxMPG123FileClose(void *id)
{
   _handle_t *handle = (_handle_t *)id;
   int ret = AAX_TRUE;

   if (handle)
   {
      if (handle->capturing)
      {
         pmpg123_delete(handle->id);
         handle->id = NULL;
         pmpg123_exit();
      }
      close(handle->fd);
#ifdef WINXP
      free(handle->pcmBuffer);
#endif
      free(handle->name);
      free(handle);
   }

   return ret;
}

static int
_aaxMPG123FileReadWrite(void *id, void *data, unsigned int no_frames)
{
   _handle_t *handle = (_handle_t *)id;
   int rv = 0;

   if (handle->capturing)
   {
      int framesz_bits = handle->no_tracks*handle->bits_sample;
      size_t blocksize = (no_frames*framesz_bits)/8;
      size_t size = 0;
      int ret;

      ret = pmpg123_read(handle->id, data, blocksize, &size);
      if (ret == MPG123_OK) {
         rv = size;
      }
      else
      {
         if (ret != MPG123_DONE) {
            _AAX_FILEDRVLOG("MP3File: error reading data");
         }
         rv = -1;
      }
   }

   return rv;
}

#ifdef WINXP
static int
_aaxMSACMFileOpen(void *id, const char* fname)
{
   _handle_t *handle = (_handle_t *)id;
   int res = -1;

   if (handle && fname)
   {
      handle->fd = open(fname, handle->mode, 0644);
      if (handle->fd >= 0)
      {
         handle->name = strdup(fname);
         if (handle->capturing)
         {
            MPEGLAYER3WAVEFORMAT mp3Fmt;
            WAVEFORMATEX pcmFmt;
            HRESULT hr;

            pcmFmt.wFormatTag = WAVE_FORMAT_PCM;
            pcmFmt.nChannels = handle->no_tracks;
            pcmFmt.nSamplesPerSec = handle->frequency;
            pcmFmt.wBitsPerSample = handle->bits_sample;
            pcmFmt.nBlockAlign = pcmFmt.nChannels*pcmFmt.wBitsPerSample/8;
            pcmFmt.nAvgBytesPerSec= pcmFmt.nSamplesPerSec*pcmFmt.nBlockAlign;
            pcmFmt.cbSize = 0;

            mp3Fmt.wfx.cbSize = MPEGLAYER3_WFX_EXTRA_BYTES;
            mp3Fmt.wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
            mp3Fmt.wfx.nChannels = 2;

            // not really used but must be one of 64, 96, 112, 128, 160kbps
            mp3Fmt.wfx.nAvgBytesPerSec = 128*1024/8;
            mp3Fmt.wfx.wBitsPerSample = 0;
            mp3Fmt.wfx.nBlockAlign = 1;

            // must be the same as pcmFmt.nSamplesPerSec
            mp3Fmt.wfx.nSamplesPerSec = handle->frequency;
            mp3Fmt.fdwFlags = MPEGLAYER3_FLAG_PADDING_OFF;
            mp3Fmt.nBlockSize = MP3_BLOCK_SIZE;
            mp3Fmt.nFramesPerBlock = 1;
            mp3Fmt.nCodecDelay = 1393;
            mp3Fmt.wID = MPEGLAYER3_ID_MPEG;

            handle->acmStream = NULL;
            hr = pacmStreamOpen(&handle->acmStream, NULL,
                               (LPWAVEFORMATEX)&mp3Fmt,
                               &pcmFmt, NULL, 0, 0, 0);
            switch(hr)
            {
            case MMSYSERR_NOERROR:   // success
            {
               hr = pacmStreamSize(handle->acmStream, MP3_BLOCK_SIZE,
                                  &handle->pcmBufMax,
                                  ACM_STREAMSIZEF_SOURCE);
               if (hr != 0)
               {
                  _AAX_FILEDRVLOG("MP3File: Unable te retreive stream size");
                  goto MSACMStreamDone;
               }

               handle->pcmBuffer = malloc(handle->pcmBufMax);
               handle->pcmBufPos = handle->pcmBufMax;

               memset(&handle->acmStreamHeader, 0, sizeof(ACMSTREAMHEADER));
               handle->acmStreamHeader.cbStruct = sizeof(ACMSTREAMHEADER);
               handle->acmStreamHeader.pbSrc = handle->mp3Buffer;
               handle->acmStreamHeader.cbSrcLength = MP3_BLOCK_SIZE;
               handle->acmStreamHeader.pbDst = handle->pcmBuffer;
               handle->acmStreamHeader.cbDstLength = handle->pcmBufMax;
               hr = pacmStreamPrepareHeader(handle->acmStream,
                                           &handle->acmStreamHeader, 0);
               if (hr != 0)
               {
                  _AAX_FILEDRVLOG("MP3File: conversion stream error");
                  goto MSACMStreamDone;
               }

               if ((1000 <= pcmFmt.nSamplesPerSec)
                   && (pcmFmt.nSamplesPerSec <= 192000)
                   && (1 <= pcmFmt.nChannels)
                   && (pcmFmt.nChannels <= _AAX_MAX_SPEAKERS))
               {
                  handle->no_tracks = pcmFmt.nChannels;
                  handle->frequency = pcmFmt.nSamplesPerSec;
                  handle->bits_sample = pcmFmt.wBitsPerSample;
                  handle->format = getFormatFromWAVFileFormat(PCM_WAVE_FILE,
                                                        handle->bits_sample);
                  res = AAX_TRUE;
               }
               else {
                  _AAX_FILEDRVLOG("MP3File: file may be corrupt");
               }
MSACMStreamDone:
               break;
            }
            case MMSYSERR_INVALPARAM:
               _AAX_FILEDRVLOG("Invalid parameters passed to acmStreamOpen");
               break;
            case ACMERR_NOTPOSSIBLE:
               _AAX_FILEDRVLOG("ACM MP3 filter not found");
            default:
               break;
               _AAX_FILEDRVLOG("Unknown error opening ACM stream");
            }
         }
         else
         {
            _AAX_FILEDRVLOG("MP3File: playback is not supported");
            close(handle->fd); /* no mp3 write support (yet) */
         }
      }
      else {
         _AAX_FILEDRVLOG("MP3File: file not found");
      }
   }
   else
   {
      if (!fname) {
         _AAX_FILEDRVLOG("MP3File: No filename prvided");
      } else {
         _AAX_FILEDRVLOG("MP3File: Internal error: handle id equals 0");
      }
   }

   return (res >= 0) ? AAX_TRUE : AAX_FALSE;
}

static int
_aaxMSACMFileClose(void *id)
{
   _handle_t *handle = (_handle_t *)id;
   int ret = AAX_TRUE;

   if (handle)
   {
      pacmStreamUnprepareHeader(handle->acmStream, &handle->acmStreamHeader, 0);
      pacmStreamClose(handle->acmStream, 0);

      close(handle->fd);
      free(handle->name);
      free(handle);

      CoUninitialize();
   }

   return ret;
}

static int
_aaxMSACMFileReadWrite(void *id, void *data, unsigned int no_frames)
{
   _handle_t *handle = (_handle_t *)id;
   int rv = 0;

   if (handle->capturing)
   {
      char *ptr = data;

      if (handle->pcmBufPos < handle->pcmBufMax)
      {
         unsigned int avail;

         avail = _MIN(handle->pcmBufMax-handle->pcmBufPos, no_frames);
         memcpy(ptr, handle->pcmBuffer, avail);

         ptr += avail;
         no_frames -= avail;
         handle->pcmBufPos += avail;
      }

      do
      {
//       memset(handle->mp3Buffer, 0, MP3_BLOCK_SIZE);
         rv = read(handle->fd, handle->mp3Buffer, MP3_BLOCK_SIZE);
         if (rv > 0)
         {
            HRESULT hr;
            hr = pacmStreamConvert(handle->acmStream, &handle->acmStreamHeader,
                                  ACM_STREAMCONVERTF_BLOCKALIGN);
            if (hr == 0) {
               handle->pcmBufPos = 0;
            }
            else {
               break;
            }
         }

         if (handle->pcmBufPos < handle->pcmBufMax)
         {
            unsigned int avail;
            avail = _MIN(handle->pcmBufMax-handle->pcmBufPos, no_frames);
            memcpy(ptr, handle->pcmBuffer, avail);

            ptr += avail;
            no_frames -= avail;
            handle->pcmBufPos += avail;
         }
      }
      while (no_frames);
   }

   return rv;
}

/* -------------------------------------------------------------------------- */

static BOOL CALLBACK
acmDriverEnumCallback(HACMDRIVERID hadid, DWORD dwInstance, DWORD fdwSupport)
{
   if (fdwSupport & ACMDRIVERDETAILS_SUPPORTF_CODEC)
   {
      ACMDRIVERDETAILS details;
      MMRESULT mmr;

      details.cbStruct = sizeof(ACMDRIVERDETAILS);
      mmr = pacmDriverDetailsA(hadid, &details, 0);
      if (!mmr)
      {
         HACMDRIVER driver;
         mmr = pacmDriverOpen(&driver, hadid, 0);
         if (!mmr)
         {
            int i;
            for(i=0; i<details.cFormatTags; i++)
            {
               ACMFORMATTAGDETAILS fmtDetails;

               memset(&fmtDetails, 0, sizeof(fmtDetails));
               fmtDetails.cbStruct = sizeof(ACMFORMATTAGDETAILS);
               fmtDetails.dwFormatTagIndex = i;
               mmr = pacmFormatTagDetailsA(driver, &fmtDetails,
                                         ACM_FORMATTAGDETAILSF_INDEX);
               if (fmtDetails.dwFormatTag == WAVE_FORMAT_MPEGLAYER3)
               {
                  acmMP3Support = AAX_TRUE;
                  break;
               }
            }
            mmr = pacmDriverClose(driver, 0);
         }
      }
   }
   return AAX_TRUE;
}
#endif /* WINXP */

/*
 * Copyright 2005-2017 by Erik Hofman.
 * Copyright 2009-2017 by Adalin B.V.
 *
 * This file is part of AeonWave
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>	// access
#ifdef HAVE_RMALLOC_H
# include <rmalloc.h>
#else
# include <string.h>
#endif


#include "protocol.h"

_prot_t*
_prot_create(_protocol_t protocol)
{
   _prot_t* rv = 0;
   switch(protocol)
   {
   case PROTOCOL_HTTP:
      rv = calloc(1, sizeof(_prot_t));
      if (rv)
      {
         rv->connect = _http_connect;
         rv->process = _http_process;
         rv->name = _http_name;
         rv->set = _http_set;
         rv->get = _http_get;

         rv->protocol = protocol;
         rv->meta_interval = 0;
         rv->meta_pos = 0;
      }
      break;
   case PROTOCOL_DIRECT:
      rv = calloc(1, sizeof(_prot_t));
      if (rv)
      {
         rv->connect = _direct_connect;
         rv->process = _direct_process;
         rv->name = _direct_name;
         rv->set = _direct_set;
         rv->get = _direct_get;

         rv->protocol = protocol;
         rv->meta_interval = 0;
         rv->meta_pos = 0;
      }
      break;
   default:
      break;
   }
   return rv;
}

void*
_prot_free(_prot_t *prot)
{
   free(prot->path);
   free(prot->content_type);
   free(prot->station);
   free(prot->description);
   free(prot->genre);
   free(prot->website);
   free(prot);
   return 0;
}

/* NOTE: modifies url, make sure to strdup it before calling this function */
_protocol_t
_url_split(char *url, char **protocol, char **server, char **path, char **extension, int *port)
{
   _protocol_t rv;
   char *ptr;

   *protocol = NULL;
   *server = NULL;
   *path = NULL;
   *port = 0;

   ptr = strstr(url, "://");
   if (ptr)
   {
      *protocol = (char*)url;
      *ptr = '\0';
      url = ptr + strlen("://");
   }
   else if (access(url, F_OK) != -1)
   {
      *path = url;
      *extension = strrchr(url, '.');
      if (*extension) (*extension)++;
   }

   if (!*path)
   {
      *server = url;

      ptr = strchr(url, '/');
      if (ptr)
      {
         if (ptr != url) *ptr++ = '\0';
         else *server = 0;

         *path = ptr;
         *extension = strrchr(ptr, '.');
         if (*extension) (*extension)++;
      }

      ptr = strchr(url, ':');
      if (ptr)
      {
         *ptr++ = '\0';
         *port = strtol(ptr, NULL, 10);
      }
   }

   if ((*protocol && !strcasecmp(*protocol, "http")) ||
       (*server && **server != 0))
   {
      rv = PROTOCOL_HTTP;
      if (*port <= 0) *port = 80;
   }
   else if (!*protocol || !strcasecmp(*protocol, "file")) {
      rv = PROTOCOL_DIRECT;
   } else {
      rv = PROTOCOL_UNSUPPORTED;
   }

   return rv;
}

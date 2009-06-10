/*
 * gzip.c - hooks for compression of packets
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2003      by Aris Adamantiadis
 * Copyright (c) 2009      by Andreas Schneider <mail@cynapses.org>
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include "config.h"
#include "libssh/priv.h"

#if defined(HAVE_LIBZ) && defined(WITH_LIBZ)

#include <zlib.h>
#include <string.h>
#include <stdlib.h>

#define BLOCKSIZE 4092

static z_stream *initcompress(SSH_SESSION *session, int level) {
  z_stream *stream = NULL;
  int status;

  stream = malloc(sizeof(z_stream));
  if (stream == NULL) {
    return NULL;
  }
  memset(stream, 0, sizeof(z_stream));

  status = deflateInit(stream, level);
  if (status != Z_OK) {
    SAFE_FREE(stream);
    ssh_set_error(session, SSH_FATAL,
        "status %d inititalising zlib deflate", status);
    return NULL;
  }

  return stream;
}

static BUFFER *gzip_compress(SSH_SESSION *session,BUFFER *source,int level){
  z_stream *zout = session->current_crypto->compress_out_ctx;
  void *in_ptr = buffer_get(source);
  unsigned long in_size = buffer_get_len(source);
  BUFFER *dest = NULL;
  static unsigned char out_buf[BLOCKSIZE] = {0};
  unsigned long len;
  int status;

  if(zout == NULL) {
    zout = session->current_crypto->compress_out_ctx = initcompress(session, level);
    if (zout == NULL) {
      return NULL;
    }
  }

  dest = buffer_new();
  if (dest == NULL) {
    return NULL;
  }

  zout->next_out = out_buf;
  zout->next_in = in_ptr;
  zout->avail_in = in_size;
  do {
    zout->avail_out = BLOCKSIZE;
    status = deflate(zout, Z_PARTIAL_FLUSH);
    if (status != Z_OK) {
      buffer_free(dest);
      ssh_set_error(session, SSH_FATAL,
          "status %d deflating zlib packet", status);
      return NULL;
    }
    len = BLOCKSIZE - zout->avail_out;
    if (buffer_add_data(dest, out_buf, len) < 0) {
      buffer_free(dest);
      return NULL;
    }
    zout->next_out = out_buf;
  } while (zout->avail_out == 0);

  return dest;
}

int compress_buffer(SSH_SESSION *session, BUFFER *buf) {
  BUFFER *dest = NULL;

  dest = gzip_compress(session, buf, 9);
  if (dest == NULL) {
    return -1;
  }

  if (buffer_reinit(buf) < 0) {
    buffer_free(dest);
    return -1;
  }

  if (buffer_add_data(buf, buffer_get(dest), buffer_get_len(dest)) < 0) {
    buffer_free(dest);
    return -1;
  }

  buffer_free(dest);
  return 0;
}

/* decompression */

static z_stream *initdecompress(SSH_SESSION *session) {
  z_stream *stream = NULL;
  int status;

  stream = malloc(sizeof(z_stream));
  if (stream == NULL) {
    return NULL;
  }
  memset(stream,0,sizeof(z_stream));

  status = inflateInit(stream);
  if (status != Z_OK) {
    SAFE_FREE(stream);
    ssh_set_error(session, SSH_FATAL,
        "Status = %d initiating inflate context!", status);
    return NULL;
  }

  return stream;
}

static BUFFER *gzip_decompress(SSH_SESSION *session, BUFFER *source) {
  z_stream *zin = session->current_crypto->compress_in_ctx;
  void *in_ptr = buffer_get_rest(source);
  unsigned long in_size = buffer_get_rest_len(source);
  static unsigned char out_buf[BLOCKSIZE] = {0};
  BUFFER *dest = NULL;
  unsigned long len;
  int status;

  if (zin == NULL) {
    zin = session->current_crypto->compress_in_ctx = initdecompress(session);
    if (zin == NULL) {
      return NULL;
    }
  }

  dest = buffer_new();
  if (dest == NULL) {
    return NULL;
  }

  zin->next_out = out_buf;
  zin->next_in = in_ptr;
  zin->avail_in = in_size;

  do {
    zin->avail_out = BLOCKSIZE;
    status = inflate(zin, Z_PARTIAL_FLUSH);
    if (status != Z_OK) {
      ssh_set_error(session, SSH_FATAL,
          "status %d inflating zlib packet", status);
      buffer_free(dest);
      return NULL;
    }

    len = BLOCKSIZE - zin->avail_out;
    if (buffer_add_data(dest,out_buf,len) < 0) {
      buffer_free(dest);
      return NULL;
    }

    zin->next_out = out_buf;
  } while (zin->avail_out == 0);

  return dest;
}

int decompress_buffer(SSH_SESSION *session,BUFFER *buf){
  BUFFER *dest = NULL;

  dest = gzip_decompress(session,buf);
  if (dest == NULL) {
    return -1;
  }

  if (buffer_reinit(buf) < 0) {
    buffer_free(dest);
    return -1;
  }

  if (buffer_add_data(buf, buffer_get(dest), buffer_get_len(dest)) < 0) {
    buffer_free(dest);
    return -1;
  }

  buffer_free(dest);
  return 0;
}

#endif /* HAVE_LIBZ && WITH_LIBZ */
/* vim: set ts=2 sw=2 et cindent: */
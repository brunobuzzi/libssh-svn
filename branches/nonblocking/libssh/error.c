/* error.c */
/* it does contain error processing functions */
/*
Copyright 2003,04 Aris Adamantiadis

This file is part of the SSH Library

The SSH Library is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version.

The SSH Library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
License for more details.

You should have received a copy of the GNU Lesser General Public License
along with the SSH Library; see the file COPYING.  If not, write to
the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
MA 02111-1307, USA. */

#include <stdio.h>
#include <stdarg.h>
#include "libssh/priv.h"

#include "libssh/errors.h"

static int verbosity;

/* ssh_set_error registers an error with a description. the error code is the class of error, and description is obvious.*/
void ssh_set_error(void *error,int code,char *descr,...){
    struct error_struct *err= error;
    va_list va;
    va_start(va,descr);
    vsnprintf(err->error_buffer,ERROR_BUFFERLEN,descr,va);
    va_end(va);
    err->error_code=code;
	ssh_say(1,"\t%s\n",err->error_buffer);
}

char *ssh_get_error(void *error){
    struct error_struct *err=error;
    return err->error_buffer;
}

int ssh_get_error_code(void *error){
    struct error_struct *err=error;
    return err->error_code;
}

void ssh_say(int priority, char *format,...){
    va_list va;
    va_start(va,format);
    if(priority <= verbosity)
        vfprintf(stderr,format,va);
    va_end(va);
}

void ssh_set_verbosity(int num){
    verbosity=num;
}

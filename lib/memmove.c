/*
 *     memmove.c: memmove compat implementation.
 *
 *     Copyright (c) 2001-2008, NLnet Labs. All rights reserved.
 *
 * See COPYING for the license.
*/

/*
Copyright (c) 2005,2006, NLnetLabs
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of NLnetLabs nor the names of its
      contributors may be used to endorse or promote products derived from this
      software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

#include <os.h>
#include <mini-os/lib.h>

#ifndef HAVE_LIBC

void *memmove(void *dest, const void *src, size_t n)
{
       uint8_t* from = (uint8_t*) src;
       uint8_t* to = (uint8_t*) dest;

       if (from == to || n == 0)
               return dest;
       if (to > from && to-from < (int)n) {
               /* to overlaps with from */
               /*  <from......>         */
               /*         <to........>  */
               /* copy in reverse, to avoid overwriting from */
               int i;
               for(i=n-1; i>=0; i--)
                       to[i] = from[i];
               return dest;
       }
       if (from > to  && from-to < (int)n) {
               /* to overlaps with from */
               /*        <from......>   */
               /*  <to........>         */
               /* copy forwards, to avoid overwriting from */
               size_t i;
               for(i=0; i<n; i++)
                       to[i] = from[i];
               return dest;
       }
       memcpy(dest, src, n);
       return dest;
}

#endif

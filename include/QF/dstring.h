/*
	dstring.h

	Dynamic string buffer functions

	Copyright (C) 1996-1997  Id Software, Inc.

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to:

		Free Software Foundation, Inc.
		59 Temple Place - Suite 330
		Boston, MA  02111-1307, USA

	$Id$
*/

#ifndef __dstring_h
#define __dstring_h

#include <stdarg.h>

typedef struct dstring_s {
	unsigned long int size, truesize;
	char *str;
} dstring_t;


// General buffer functions
/** Create a new dstring. size and truesize start at 0 and no string buffer
	is allocated.
*/
dstring_t *dstring_new(void);
/** Delete a dstring. Both the string buffer and dstring object are freed.
*/
void dstring_delete (dstring_t *dstr);
/** Resize the string buffer if necessary. The buffer is guaranteed to be
	large enough to hold size bytes (rounded up to the next 1kB boundary)
*/
void dstring_adjust (dstring_t *dstr);
/** Copy len bytes from data into the dstring, replacing any existing data.
*/
void dstring_copy (dstring_t *dstr, const char *data, unsigned int len);
/** Append len bytes from data onto the end of the dstring.
*/
void dstring_append (dstring_t *dstr, const char *data, unsigned int len);
/** Insert len bytes from data int the dstring at pos. If pos is past the
	end of the dstring, equivalent to dstring_append.
*/
void dstring_insert (dstring_t *dstr, unsigned int pos, const char *data,
					 unsigned int len);
/** Remove len bytes from the dstring starting at pos.
*/
void dstring_snip (dstring_t *dstr, unsigned int pos, unsigned int len);
/** Set the size of the dstring to 0 bytes. Does not free the string buffer
	anticipating reuse.
*/
void dstring_clear (dstring_t *dstr);
/** Replace rlen bytes in dstring at pos with len bytes from data. Moves
	trailing bytes as needed.
*/
void dstring_replace (dstring_t *dstr, unsigned int pos, unsigned int rlen,
						const char *data, unsigned int len);
/** Delete the dstring object retaining the string buffer. The string buffer
	will be just big enough to hold the data. Does NOT ensure the string is
	null terminated.
*/
char *dstring_freeze (dstring_t *dstr);
 
// String-specific functions
/** Allocate a new dstring pre-initialized as a null terminated string. size
	will be 1 and the first byte 0.
*/
dstring_t *dstring_newstr (void);
/** Copy the null terminated string into the dstring. Replaces any existing
	data.
	The dstring does not have to be null terminated but will become so.
*/
void dstring_copystr (dstring_t *dstr, const char *str);
/** Copy up to len bytes from the string into the dstring. Replaces any
	existing data.
	The dstring does not have to be null terminated but will become so.
*/
void dstring_copysubstr (dstring_t *dstr, const char *str, unsigned int len);
/** Append the null terminated string to the end of the dstring.
	The dstring does not have to be null terminated but will become so.
	However, any embedded nulls will be treated as the end of the dstring.
*/
void dstring_appendstr (dstring_t *dstr, const char *str);
/** Append up to len bytes from the string to the end of the dstring.
	The dstring does not have to be null terminated but will become so.
	However, any embedded nulls will be treated as the end of the dstring.
*/
void dstring_appendsubstr (dstring_t *dstr, const char *str, unsigned int len);
/** Insert the null terminated string into the dstring at pos. The dstring
	is NOT forced to be null terminated.
*/
void dstring_insertstr (dstring_t *dstr, unsigned int pos, const char *str);
/** Insert up to len bytes from the string into the dstring at pos. The
	dstring is NOT forced to be null terminated.
*/
void dstring_insertsubstr (dstring_t *dstr, unsigned int pos, const char *str,
						   unsigned int len);
/** Clear the dstring to be equivalent to "". Does not resize the string buffer
	but size is set to 1.
	dstr = dstring_new (); dstring_clearstr (dstr); is exactly equivalent to
	dstr = dstring_newstr ();
*/
void dstring_clearstr (dstring_t *dstr);

//@{
/** Formatted printing to dstrings. Existing data is replaced by the formatted
	string.
*/
int dvsprintf (dstring_t *dstr, const char *fmt, va_list args);
int dsprintf (dstring_t *dstr, const char *fmt, ...) __attribute__((format(printf,2,3)));
//@}
//@{
/** Formatted printing to dstrings. Formatted string is appened to the dstring.
	Embedded nulls in the dstring are ignored.
*/
int davsprintf (dstring_t *dstr, const char *fmt, va_list args);
int dasprintf (dstring_t *dstr, const char *fmt, ...) __attribute__((format(printf,2,3)));
//@}

#endif // __dstring_h

/*
	qtypes.h

	(description)

	Copyright (C) 1996-1997  Id Software, Inc.
	Copyright (C) 1999,2000  contributors of the QuakeForge project
	Please see the file "AUTHORS" for a list of contributors

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

#ifndef __qtypes_h
#define __qtypes_h

#include <stdio.h>

#ifdef __QNX__
#define msg_t qnx_msg_t
#include <sys/types.h>
#undef msg_t
#endif

#define MAX_QPATH	64

#ifndef _DEF_BYTE_
# define _DEF_BYTE_
typedef unsigned char byte;
#endif

#ifndef _DEF_BOOL_
# define _DEF_BOOL_
// KJB Undefined true and false defined in SciTech's DEBUG.H header
#undef true
#undef false
# ifdef __bool_true_false_are_defined
typedef enum    {q_false, q_true} qboolean;
#define true q_true;
#define false q_false;
# else
typedef	enum	{false, true} qboolean;
# endif
#endif

// From mathlib...
typedef float	vec_t;
typedef vec_t	vec3_t[3];
typedef vec_t	vec5_t[5];
typedef	int	fixed4_t;
typedef	int	fixed8_t;
typedef	int	fixed16_t;

#define SIDE_FRONT  0
#define SIDE_BACK   1
#define SIDE_ON     2

// plane_t structure
// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct mplane_s {
	vec3_t  normal;
	float   dist;
	byte    type;			// for texture axis selection and fast side tests
	byte    signbits;		// signx + signy<<1 + signz<<1
	byte    pad[2];
} mplane_t;

#endif // __qtypes_h

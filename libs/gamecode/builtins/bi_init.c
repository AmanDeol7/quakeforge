/*
	bi_init.c

	CSQC builtins init

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

*/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

static __attribute__ ((unused)) const char rcsid[] = 
	"$Id$";

#include "QF/csqc.h"
#include "QF/progs.h"

static void (*const cbuf_progs_init)(progs_t *) = Cbuf_Progs_Init;
static void (*const cvar_progs_init)(progs_t *) = Cvar_Progs_Init;
static void (*const cmd_progs_init)(progs_t *) = Cmd_Progs_Init;
static void (*const file_progs_init)(progs_t *) = File_Progs_Init;
static void (*const inputline_progs_init)(progs_t *) = InputLine_Progs_Init;
static void (*const string_progs_init)(progs_t *) = String_Progs_Init;
static void (*const stringhashe_progs_init)(progs_t *) = StringHash_Progs_Init;

void
BI_Init ()
{
	// do nothing stub for now. used to force linking
}

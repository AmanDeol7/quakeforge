/*
	bi_cvar.c

	CSQC cvar builtins

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

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "QF/csqc.h"
#include "QF/cvar.h"
#include "QF/progs.h"
#include "QF/zone.h"

/*
    bi_Cvar_GetCvarString
    
    QC-Function for get a string from a cvar
*/      
static void
bi_Cvar_GetCvarString (progs_t *pr)
{
	const char *varname = P_STRING (pr, 0);

	RETURN_STRING (pr, Cvar_VariableString (varname));
}

void
Cvar_Progs_Init (progs_t *pr)
{
	PR_AddBuiltin (pr, "Cvar_GetCvarString", bi_Cvar_GetCvarString, -1);
}

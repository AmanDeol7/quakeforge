/*
	#FILENAME#

	#DESCRIPTION#

	Copyright (C) 2001 #AUTHOR#

	Author: #AUTHOR#
	Date: #DATE#

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
static const char rcsid[] =
	"$Id$";

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <getopt.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_IO_H
# include <io.h>
#endif
#include <sys/types.h>
#include <sys/fcntl.h>

#include "QF/cmd.h"
#include "QF/cvar.h"
#include "QF/progs.h"
#include "QF/sys.h"
#include "QF/vfile.h"
#include "QF/zone.h"

#include "QF/vfile.h"

#include "qfprogs.h"
#include "disassemble.h"

void
disassemble_progs (progs_t *pr)
{
	int i;

	for (i = 0; i < pr->progs->numstatements; i++) {
		dfunction_t *f = func_find (i);
		if (f) {
			Sys_Printf ("%s:\n", PR_GetString (pr, f->s_name));
			pr->pr_xfunction = f;
		}
		PR_PrintStatement (pr, &pr->pr_statements[i]);
	}
}

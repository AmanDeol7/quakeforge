/*
	cl_sys_unix.c

	(description)

	Copyright (C) 1996-1997  Id Software, Inc.
	Copyright (C) 2000       Marcus Sundberg [mackan@stacken.kth.se]

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
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "QF/console.h"
#include "QF/qargs.h"
#include "QF/sys.h"

#include "host.h"
#include "net.h"

int         noconinput = 0;
qboolean    is_server = false;
char       *svs_info;

void
Sys_Init (void)
{
#ifdef USE_INTEL_ASM
	Sys_SetFPCW ();
#endif
}

static void
shutdown (void)
{
	// change stdin to blocking
	fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~O_NONBLOCK);
}

void
Sys_Warn (char *warning, ...)
{
	va_list     argptr;
	char        string[1024];

	va_start (argptr, warning);
	vsnprintf (string, sizeof (string), warning, argptr);
	va_end (argptr);
	fprintf (stderr, "Warning: %s", string);
}

void
floating_point_exception_handler (int whatever)
{
//	Sys_Warn("floating point exception\n");
	signal (SIGFPE, floating_point_exception_handler);
}

#ifndef USE_INTEL_ASM
void
Sys_HighFPPrecision (void)
{
}

void
Sys_LowFPPrecision (void)
{
}
#endif

int         skipframes;

int
main (int c, const char *v[])
{
//	static char cwd[1024];
	double      time, oldtime, newtime;

//	signal(SIGFPE, floating_point_exception_handler);
	signal (SIGFPE, SIG_IGN);

	memset (&host_parms, 0, sizeof (host_parms));

	COM_InitArgv (c, v);
	host_parms.argc = com_argc;
	host_parms.argv = com_argv;

	noconinput = COM_CheckParm ("-noconinput");
	if (!noconinput)
		fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) | O_NONBLOCK);

	Sys_RegisterShutdown (Host_Shutdown);
	Sys_RegisterShutdown (Net_LogStop);
	Sys_RegisterShutdown (shutdown);

	Host_Init ();

	oldtime = Sys_DoubleTime ();
	while (1) {
		// find time spent rendering last frame
		newtime = Sys_DoubleTime ();
		time = newtime - oldtime;

		Host_Frame (time);
		oldtime = newtime;
	}
}

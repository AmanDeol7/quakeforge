/*
	sys_unix.c

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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "QF/console.h"
#include "QF/cvar.h"
#include "QF/qargs.h"
#include "QF/sys.h"

#include "client.h"
#include "host.h"
#include "server.h"

qboolean    isDedicated = false;


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
floating_point_exception_handler (int whatever)
{
//	Sys_Warn("floating point exception\n");
	signal (SIGFPE, floating_point_exception_handler);
}

/*
	Sys_ConsoleInput

	Checks for a complete line of text typed in at the console, then forwards
	it to the host command processor
*/
const char *
Sys_ConsoleInput (void)
{
	static char text[256];
	int         len;
	fd_set      fdset;
	struct timeval timeout;

	if (cls.state == ca_dedicated) {
		FD_ZERO (&fdset);
		FD_SET (0, &fdset);				// stdin
		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		if (select (1, &fdset, NULL, NULL, &timeout) == -1
			|| !FD_ISSET (0, &fdset)) return NULL;

		len = read (0, text, sizeof (text));
		if (len < 1)
			return NULL;
		text[len - 1] = 0;				// rip off the \n and terminate

		return text;
	}
	return NULL;
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

int
main (int c, const char *v[])
{

	double      time, oldtime, newtime;

	signal (SIGFPE, SIG_IGN);

	memset (&host_parms, 0, sizeof (host_parms));

	COM_InitArgv (c, v);
	host_parms.argc = com_argc;
	host_parms.argv = com_argv;

	isDedicated = (COM_CheckParm ("-dedicated") != 0);

	fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) | O_NONBLOCK);

	Sys_RegisterShutdown (Host_Shutdown);
	Sys_RegisterShutdown (shutdown);

	Host_Init ();

	if (!sys_nostdout->int_val) {
		fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) | O_NONBLOCK);
		Con_Printf ("Quake -- Version %s\n", NQ_VERSION);
	}

	oldtime = Sys_DoubleTime () - 0.1;
	while (1) {
		// find time spent rendering last frame
		newtime = Sys_DoubleTime ();
		time = newtime - oldtime;

		if (cls.state == ca_dedicated) {	// play vcrfiles at max speed
			if (time < sys_ticrate->value && (!vcrFile || recording)) {
				usleep (1);
				continue;			// not time to run a server only tic yet
			}
			time = sys_ticrate->value;
		}

		if (time > sys_ticrate->value * 2)
			oldtime = newtime;
		else
			oldtime += time;

		Host_Frame (time);
	}
}

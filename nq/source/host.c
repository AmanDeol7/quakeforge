/*
	host.c

	host init

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
static const char rcsid[] = 
	"$Id$";

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "QF/cbuf.h"
#include "QF/idparse.h"
#include "QF/cdaudio.h"
#include "QF/cmd.h"
#include "QF/console.h"
#include "QF/csqc.h"
#include "QF/cvar.h"
#include "QF/draw.h"
#include "QF/input.h"
#include "QF/keys.h"
#include "QF/locs.h"
#include "QF/msg.h"
#include "QF/plugin.h"
#include "QF/progs.h"
#include "QF/qargs.h"
#include "QF/screen.h"
#include "QF/sys.h"
#include "QF/va.h"
#include "QF/vid.h"
#include "QF/gib_thread.h"

#include "buildnum.h"
#include "chase.h"
#include "compat.h"
#include "host.h"
#include "r_dynamic.h"
#include "sbar.h"
#include "server.h"
#include "sv_progs.h"
#include "view.h"

static location_t * (* const _locs_find) (const vec3_t target) = locs_find;


/*
  A server can always be started, even if the system started out as a client
  to a remote system.

  A client can NOT be started if the system started as a dedicated server.

  Memory is cleared/released when a server or client begins, not when they end.
*/

CLIENT_PLUGIN_PROTOS
static plugin_list_t client_plugin_list[] = {
		CLIENT_PLUGIN_LIST
};

qboolean	host_initialized;			// true if into command execution

quakeparms_t host_parms;

cbuf_t     *host_cbuf;

double		host_frametime;
double		host_time;
double		realtime;					// without any filtering or bounding
double		oldrealtime;				// last frame run

int			host_framecount;
int			host_hunklevel;
int			minimum_memory;

client_t   *host_client;				// current client

jmp_buf		host_abortserver;

byte       *host_basepal;

cvar_t     *fs_globalcfg;
cvar_t     *fs_usercfg;

cvar_t     *host_mem_size;

cvar_t     *host_framerate;
cvar_t     *host_speeds;

cvar_t     *sys_ticrate;
cvar_t     *serverprofile;

cvar_t     *cl_quakerc;

cvar_t     *fraglimit;
cvar_t     *timelimit;
cvar_t     *teamplay;
cvar_t     *noexit;
cvar_t     *samelevel;

cvar_t     *skill;
cvar_t     *coop;
cvar_t     *deathmatch;

cvar_t     *pausable;

cvar_t     *temp1;



void
Host_EndGame (const char *message, ...)
{
	char        string[1024];
	va_list     argptr;

	va_start (argptr, message);
	vsnprintf (string, sizeof (string), message, argptr);
	va_end (argptr);
	Con_DPrintf ("Host_EndGame: %s\n", string);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_EndGame: %s", string);	// dedicated servers exit

	if (cls.demonum != -1)
		CL_NextDemo ();
	else
		CL_Disconnect ();

	longjmp (host_abortserver, 1);
}

/*
	Host_Error

	This shuts down both the client and server
*/
void
Host_Error (const char *error, ...)
{
	char        string[1024];
	static qboolean inerror = false;
	va_list     argptr;

	if (inerror)
		Sys_Error ("Host_Error: recursively entered");
	inerror = true;

//	SCR_EndLoadingPlaque ();						// reenable screen updates

	va_start (argptr, error);
	vsnprintf (string, sizeof (string), error, argptr);
	va_end (argptr);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_Error: %s", string);		// dedicated servers exit

	Con_Printf ("Host_Error: %s\n", string);

	CL_Disconnect ();
	cls.demonum = -1;

	inerror = false;

	longjmp (host_abortserver, 1);
}

void
Host_FindMaxClients (void)
{
	int         i;

	svs.maxclients = 1;

	i = COM_CheckParm ("-dedicated");
	if (i) {
		cls.state = ca_dedicated;
		if (i != (com_argc - 1)) {
			svs.maxclients = atoi (com_argv[i + 1]);
		} else
			svs.maxclients = 8;
	} else
		cls.state = ca_disconnected;

	i = COM_CheckParm ("-listen");
	if (i) {
		if (cls.state == ca_dedicated)
			Sys_Error ("Only one of -dedicated or -listen can be specified");
		if (i != (com_argc - 1))
			svs.maxclients = atoi (com_argv[i + 1]);
		else
			svs.maxclients = 8;
	}
	if (svs.maxclients < 1)
		svs.maxclients = 8;
	else if (svs.maxclients > MAX_SCOREBOARD)
		svs.maxclients = MAX_SCOREBOARD;

	svs.maxclientslimit = svs.maxclients;
	if (svs.maxclientslimit < 4)
		svs.maxclientslimit = 4;
	svs.clients =
		Hunk_AllocName (svs.maxclientslimit * sizeof (client_t), "clients");

	if (svs.maxclients > 1)
		Cvar_SetValue (deathmatch, 1.0);
	else
		Cvar_SetValue (deathmatch, 0.0);
}

void
Host_InitLocal (void)
{
	Host_InitCommands ();

	host_framerate =
		Cvar_Get ("host_framerate", "0", CVAR_NONE, NULL,
				"set for slow motion");
	host_speeds =
		Cvar_Get ("host_speeds", "0", CVAR_NONE, NULL,
				"set for running times");

	sys_ticrate = Cvar_Get ("sys_ticrate", "0.05", CVAR_NONE, NULL, "None");
	serverprofile = Cvar_Get ("serverprofile", "0", CVAR_NONE, NULL, "None");

	cl_quakerc = Cvar_Get ("cl_quakerc", "1", CVAR_NONE, NULL,
						   "exec quake.rc on startup");

	fraglimit = Cvar_Get ("fraglimit", "0", CVAR_SERVERINFO, Cvar_Info,
						  "None");
	timelimit = Cvar_Get ("timelimit", "0", CVAR_SERVERINFO, Cvar_Info,
						  "None");
	teamplay = Cvar_Get ("teamplay", "0", CVAR_SERVERINFO, Cvar_Info, "None");
	samelevel = Cvar_Get ("samelevel", "0", CVAR_NONE, NULL, "None");
	noexit = Cvar_Get ("noexit", "0", CVAR_SERVERINFO, Cvar_Info, "None");
	skill = Cvar_Get ("skill", "1", CVAR_NONE, NULL, "0 - 3");
	deathmatch = Cvar_Get ("deathmatch", "0", CVAR_NONE, NULL, "0, 1, or 2");
	coop = Cvar_Get ("coop", "0", CVAR_NONE, NULL, "0 or 1");
	pausable = Cvar_Get ("pausable", "1", CVAR_NONE, NULL, "None");
	temp1 = Cvar_Get ("temp1", "0", CVAR_NONE, NULL, "None");

	Host_FindMaxClients ();

	host_time = 1.0;				// so a think at time 0 won't get called
}

/*
	Host_WriteConfiguration

	Writes key bindings and archived cvars to config.cfg
*/
void
Host_WriteConfiguration (void)
{
	QFile      *f;

	// dedicated servers initialize the host but don't parse and set the
	// config.cfg cvars
	if (host_initialized && !isDedicated && cl_writecfg->int_val) {
		char       *path = va ("%s/config.cfg", com_gamedir);
		f = Qopen (path, "w");
		if (!f) {
			Con_Printf ("Couldn't write config.cfg.\n");
			return;
		}

		Key_WriteBindings (f);
		Cvar_WriteVariables (f);

		Qclose (f);
	}
}

/*
	SV_ClientPrintf

	Sends text across to be displayed
*/
void
SV_ClientPrintf (const char *fmt, ...)
{
	char        string[1024];
	va_list     argptr;

	va_start (argptr, fmt);
	vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_print);
	MSG_WriteString (&host_client->message, string);
}

/*
	SV_BroadcastPrintf

	Sends text to all active clients
*/
void
SV_BroadcastPrintf (const char *fmt, ...)
{
	char        string[1024];
	int         i;
	va_list     argptr;

	va_start (argptr, fmt);
	vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	for (i = 0; i < svs.maxclients; i++)
		if (svs.clients[i].active && svs.clients[i].spawned) {
			MSG_WriteByte (&svs.clients[i].message, svc_print);
			MSG_WriteString (&svs.clients[i].message, string);
		}
}

/*
	Host_ClientCommands

	Send text over to the client to be executed
*/
void
Host_ClientCommands (const char *fmt, ...)
{
	char        string[1024];
	va_list     argptr;

	va_start (argptr, fmt);
	vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_stufftext);
	MSG_WriteString (&host_client->message, string);
}

/*
	SV_DropClient

	Called when the player is getting totally kicked off the host
	if (crash = true), don't bother sending signofs
*/
void
SV_DropClient (qboolean crash)
{
	client_t   *client;
	int         saveSelf, i;

	if (!crash) {
		// send any final messages (don't check for errors)
		if (NET_CanSendMessage (host_client->netconnection)) {
			MSG_WriteByte (&host_client->message, svc_disconnect);
			NET_SendMessage (host_client->netconnection,
							 &host_client->message);
		}

		if (host_client->edict && host_client->spawned) {
			// call the prog function for removing a client
			// this will set the body to a dead frame, among other things
			saveSelf = *sv_globals.self;
			*sv_globals.self =
				EDICT_TO_PROG (&sv_pr_state, host_client->edict);
			PR_ExecuteProgram (&sv_pr_state, sv_funcs.ClientDisconnect);
			*sv_globals.self = saveSelf;
		}

		Con_Printf ("Client %s removed\n", host_client->name);
	}
	// break the net connection
	NET_Close (host_client->netconnection);
	host_client->netconnection = NULL;

	// free the client (the body stays around)
	host_client->active = false;
	host_client->name[0] = 0;
	host_client->old_frags = -999999;
	net_activeconnections--;

	// send notification to all clients
	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++) {
		if (!client->active)
			continue;
		MSG_WriteByte (&client->message, svc_updatename);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteString (&client->message, "");
		MSG_WriteByte (&client->message, svc_updatefrags);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteShort (&client->message, 0);
		MSG_WriteByte (&client->message, svc_updatecolors);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteByte (&client->message, 0);
	}
}

/*
	Host_ShutdownServer

	This only happens at the end of a game, not between levels
*/
void
Host_ShutdownServer (qboolean crash)
{
	char        message[4];
	double      start;
	int         count, i;
	sizebuf_t   buf;

	if (!sv.active)
		return;

	sv.active = false;

	// stop all client sounds immediately
	if (cls.state == ca_connected)
		CL_Disconnect ();

	// flush any pending messages - like the score!!!
	start = Sys_DoubleTime ();
	do {
		count = 0;
		for (i = 0, host_client = svs.clients; i < svs.maxclients;
			 i++, host_client++) {
			if (host_client->active && host_client->message.cursize) {
				if (NET_CanSendMessage (host_client->netconnection)) {
					NET_SendMessage (host_client->netconnection,
									 &host_client->message);
					SZ_Clear (&host_client->message);
				} else {
					NET_GetMessage (host_client->netconnection);
					count++;
				}
			}
		}
		if ((Sys_DoubleTime () - start) > 3.0)
			break;
	}
	while (count);

	// make sure all the clients know we're disconnecting
	buf.data = message;
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte (&buf, svc_disconnect);
	count = NET_SendToAll (&buf, 5);
	if (count)
		Con_Printf
			("Host_ShutdownServer: NET_SendToAll failed for %u clients\n",
			 count);

	for (i = 0, host_client = svs.clients; i < svs.maxclients;
		 i++, host_client++)
		if (host_client->active)
			SV_DropClient (crash);

	// clear structures
	memset (&sv, 0, sizeof (sv));
	memset (svs.clients, 0, svs.maxclientslimit * sizeof (client_t));
}

/*
	Host_ClearMemory

	This clears all the memory used by both the client and server, but does
	not reinitialize anything.
*/
void
Host_ClearMemory (void)
{
	Con_DPrintf ("Clearing memory\n");
	D_FlushCaches ();
	Mod_ClearAll ();
	if (host_hunklevel)
		Hunk_FreeToLowMark (host_hunklevel);

	cls.signon = 0;
	memset (&sv, 0, sizeof (sv));
	memset (&cl, 0, sizeof (cl));
}

/*
	Host_FilterTime

	Returns false if the time is too short to run a frame
*/
qboolean
Host_FilterTime (float time)
{
	float       timedifference;
	float       timescale = 1.0;

	if (cls.demoplayback) {
		timescale = max (0, demo_speed->value);
		time *= timescale;
	}

	realtime += time;

	//FIXME not having the framerate cap is nice, but it breaks net play
	timedifference = (timescale / 72.0) - (realtime - oldrealtime);

	if (!cls.timedemo && (timedifference > 0))
		return false;                   // framerate is too high

	host_frametime = realtime - oldrealtime;
	oldrealtime = realtime;

	if (host_framerate->value > 0)
		host_frametime = host_framerate->value;
	else	// don't allow really long or short frames
		host_frametime = bound (0.001, host_frametime, 0.1);

	return true;
}

/*
	Host_GetConsoleCommands

	Add them exactly as if they had been typed at the console
*/
void
Host_GetConsoleCommands (void)
{
	Con_ProcessInput ();
}

void
Host_ServerFrame (void)
{
	// run the world state  
	*sv_globals.frametime = host_frametime;

	// set the time and clear the general datagram
	SV_ClearDatagram ();

	// check for new clients
	SV_CheckForNewClients ();

	// read client messages
	SV_RunClients ();

	// move things around and think
	// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game)) {
		SV_Physics ();
		sv.time += host_frametime;
	}

	// send all messages to the clients
	SV_SendClientMessages ();
}

/*
	Host_Frame

	Runs all active servers
*/
void
_Host_Frame (float time)
{
	static double time1 = 0, time2 = 0, time3 = 0;
	int           pass1, pass2, pass3;

	if (setjmp (host_abortserver))
		return;							// something bad happened, or the
	// server disconnected

	// keep the random time dependent
	rand ();

	// decide the simulation time
	if (!Host_FilterTime (time))
		return;							// don't run too fast, or packets
										// will flood out

	if (cls.state != ca_dedicated) {
	// get new key events
		IN_SendKeyEvents ();

	// allow mice or other external controllers to add commands
		IN_Commands ();
	}

	// process gib threads
	
	GIB_Thread_Execute ();

	// process console commands
	cmd_source = src_command;
	Cbuf_Execute_Stack (host_cbuf);

	NET_Poll ();

	// if running the server locally, make intentions now
	if (sv.active)
		CL_SendCmd ();

//-------------------
//
// server operations
//
//-------------------

	// check for commands typed to the host
	Host_GetConsoleCommands ();

	if (sv.active)
		Host_ServerFrame ();

//-------------------
//
// client operations
//
//-------------------

	// if running the server remotely, send intentions now after
	// the incoming messages have been read
	if (!sv.active)
		CL_SendCmd ();

	host_time += host_frametime;

	// fetch results from server
	if (cls.state == ca_connected) {
		CL_ReadFromServer ();
	}
	// update video
	if (host_speeds->int_val)
		time1 = Sys_DoubleTime ();

	r_inhibit_viewmodel = (chase_active->int_val
						   || (cl.stats[STAT_ITEMS] & IT_INVISIBILITY)
						   || cl.stats[STAT_HEALTH] <= 0);
	r_force_fullscreen = cl.intermission;
	r_paused = cl.paused;
	r_active = cls.state == ca_active;
	r_view_model = &cl.viewent;
	r_frametime = host_frametime;

	CL_UpdateScreen (cl.time);

	if (host_speeds->int_val)
		time2 = Sys_DoubleTime ();

	// update audio
	if (cls.signon == SIGNONS) {
		S_Update (r_origin, vpn, vright, vup);
		R_DecayLights (host_frametime);
	} else
		S_Update (vec3_origin, vec3_origin, vec3_origin, vec3_origin);

	CDAudio_Update ();

	if (host_speeds->int_val) {
		pass1 = (time1 - time3) * 1000;
		time3 = Sys_DoubleTime ();
		pass2 = (time2 - time1) * 1000;
		pass3 = (time3 - time2) * 1000;
		Con_Printf ("%3i tot %3i server %3i gfx %3i snd\n",
					pass1 + pass2 + pass3, pass1, pass2, pass3);
	}

	host_framecount++;
	fps_count++;
}

void
Host_Frame (float time)
{
	double        time1, time2;
	static double timetotal;
	int           i, c, m;
	static int    timecount;

	if (!serverprofile->int_val) {
		_Host_Frame (time);
		return;
	}

	time1 = Sys_DoubleTime ();
	_Host_Frame (time);
	time2 = Sys_DoubleTime ();

	timetotal += time2 - time1;
	timecount++;

	if (timecount < 1000)
		return;

	m = timetotal * 1000 / timecount;
	timecount = 0;
	timetotal = 0;
	c = 0;
	for (i = 0; i < svs.maxclients; i++) {
		if (svs.clients[i].active)
			c++;
	}

	Con_Printf ("serverprofile: %2i clients %2i msec\n", c, m);
}


#define	VCR_SIGNATURE	0x56435231
// "VCR1"

void
Host_InitVCR (quakeparms_t *parms)
{
	char       *p;
	int         i, len, n;

	if (COM_CheckParm ("-playback")) {
		if (com_argc != 2)
			Sys_Error ("No other parameters allowed with -playback");

		vcrFile = Qopen ("quake.vcr", "rbz");
		if (!vcrFile)
			Sys_Error ("playback file not found");

		Qread (vcrFile, &i, sizeof (int));

		if (i != VCR_SIGNATURE)
			Sys_Error ("Invalid signature in vcr file");

		Qread (vcrFile, &com_argc, sizeof (int));
		com_argv = malloc (com_argc * sizeof (char *));
		SYS_CHECKMEM (com_argv);

		com_argv[0] = parms->argv[0];
		for (i = 0; i < com_argc; i++) {
			Qread (vcrFile, &len, sizeof (int));

			p = malloc (len);
			SYS_CHECKMEM (p);
			Qread (vcrFile, p, len);
			com_argv[i + 1] = p;
		}
		com_argc++;						/* add one for arg[0] */
		parms->argc = com_argc;
		parms->argv = com_argv;
	}

	if ((n = COM_CheckParm ("-record")) != 0) {
		vcrFile = Qopen ("quake.vcr", "wb");

		i = VCR_SIGNATURE;
		Qwrite (vcrFile, &i, sizeof (int));

		i = com_argc - 1;
		Qwrite (vcrFile, &i, sizeof (int));

		for (i = 1; i < com_argc; i++) {
			if (i == n) {
				len = 10;
				Qwrite (vcrFile, &len, sizeof (int));

				Qwrite (vcrFile, "-playback", len);
				continue;
			}
			len = strlen (com_argv[i]) + 1;
			Qwrite (vcrFile, &len, sizeof (int));

			Qwrite (vcrFile, com_argv[i], len);
		}
	}

}

static int
check_quakerc (void)
{
	const char *l, *p;
	int ret = 1;
	QFile *f;

	COM_FOpenFile ("quake.rc", &f);
	if (!f)
		return 1;
	while ((l = Qgetline (f))) {
		if ((p = strstr (l, "stuffcmds"))) {
			if (p == l) {				// only known case so far
				ret = 0;
				break;
			}
		}
	}
	Qclose (f);
	return ret;
}

void
CL_Init_Memory (void)
{
	int         mem_parm = COM_CheckParm ("-mem");
	int         mem_size;
	void       *mem_base;

	if (standard_quake)
		minimum_memory = MINIMUM_MEMORY;
	else
		minimum_memory = MINIMUM_MEMORY_LEVELPAK;

	host_mem_size = Cvar_Get ("host_mem_size", "16", CVAR_NONE, NULL,
							  "Amount of memory (in MB) to allocate for the "
							  PROGRAM " heap");
	if (mem_parm)
		Cvar_Set (host_mem_size, com_argv[mem_parm + 1]);

	if (COM_CheckParm ("-minmemory"))
		Cvar_SetValue (host_mem_size, minimum_memory / (1024 * 1024.0));

	Cvar_SetFlags (host_mem_size, host_mem_size->flags | CVAR_ROM);

	mem_size = (int) (host_mem_size->value * 1024 * 1024);

	if (mem_size < minimum_memory)
		Sys_Error ("Only %4.1f megs of memory reported, can't execute game",
				   mem_size / (float) 0x100000);

	mem_base = malloc (mem_size);

	if (!mem_base)
		Sys_Error ("Can't allocate %d", mem_size);

	Sys_PageIn (mem_base, mem_size);
	Memory_Init (mem_base, mem_size);
}

void
Host_Init (void)
{
	Con_Printf ("Host_Init\n");

	host_cbuf = Cbuf_New (&id_interp);
	cmd_source = src_command;

	Cvar_Init_Hash ();
	Cmd_Init_Hash ();
	Cvar_Init ();
	Sys_Init_Cvars ();

	Sys_Init ();

	Cmd_Init ();

	// execute +set as early as possible
	Cmd_StuffCmds (host_cbuf);
	Cbuf_Execute_Sets (host_cbuf);

	// execute the global configuration file if it exists
	// would have been nice if Cmd_Exec_f could have been used, but it
	// only reads from within the quake file system, and changing that is
	// probably Not A Good Thing (tm).
	fs_globalcfg = Cvar_Get ("fs_globalcfg", FS_GLOBALCFG,
							 CVAR_ROM, NULL, "global configuration file");
	Cmd_Exec_File (host_cbuf, fs_globalcfg->string);
	Cbuf_Execute_Sets (host_cbuf);

	// execute +set again to override the config file
	Cmd_StuffCmds (host_cbuf);
	Cbuf_Execute_Sets (host_cbuf);

	fs_usercfg = Cvar_Get ("fs_usercfg", FS_USERCFG, CVAR_ROM, NULL,
						   "user configuration file");
	Cmd_Exec_File (host_cbuf, fs_usercfg->string);
	Cbuf_Execute_Sets (host_cbuf);

	// execute +set again to override the config file
	Cmd_StuffCmds (host_cbuf);
	Cbuf_Execute_Sets (host_cbuf);

	CL_Init_Memory ();

	pr_gametype = "netquake";

	PI_Init ();

	Chase_Init_Cvars ();
	CL_InitCvars ();
	COM_Filesystem_Init_Cvars ();
	IN_Init_Cvars ();
	VID_Init_Cvars ();
	S_Init_Cvars ();
	Key_Init_Cvars ();
	Con_Init_Cvars ();
	PR_Init_Cvars ();
	SV_Progs_Init_Cvars ();
	R_Init_Cvars ();
	R_Particles_Init_Cvars ();
	Mod_Init_Cvars ();
	Host_Skin_Init_Cvars ();
	V_Init_Cvars ();

	PR_Init ();
	BI_Init ();

	V_Init ();
	COM_Filesystem_Init ();
	Game_Init ();
	COM_Init ();

	PI_RegisterPlugins (client_plugin_list);
	if (isDedicated)
		Con_Init ("server");
	else
		Con_Init ("client");
	if (con_module) {
		con_module->data->console->realtime = &realtime;
		con_module->data->console->quit = Host_Quit_f;
		con_module->data->console->cbuf = host_cbuf;
	}

	Host_InitVCR (&host_parms);
	Host_InitLocal ();

	NET_Init ();

	W_LoadWadFile ("gfx.wad");
	Key_Init (host_cbuf);
	Mod_Init ();

	CL_Demo_Init ();

	SV_Progs_Init ();
	SV_Init ();

	Con_Printf ("%4.1f megabyte heap\n", host_mem_size->value);

	if (cls.state != ca_dedicated) {
		host_basepal = (byte *) COM_LoadHunkFile ("gfx/palette.lmp");
		if (!host_basepal)
			Sys_Error ("Couldn't load gfx/palette.lmp");
		vid_colormap = (byte *) COM_LoadHunkFile ("gfx/colormap.lmp");
		if (!vid_colormap)
			Sys_Error ("Couldn't load gfx/colormap.lmp");

		VID_Init (host_basepal);
		Draw_Init ();
		SCR_Init ();
		R_Init ();

		S_Init (&cl.worldmodel, &viewentity, &host_frametime);

		CDAudio_Init ();
		Sbar_Init ();
		CL_Init ();
		IN_Init ();

		CL_SetState (ca_disconnected);
	}
	Host_Skin_Init ();

	if (!isDedicated && cl_quakerc->int_val)
		Cbuf_InsertText (host_cbuf, "exec quake.rc\n");
	Cmd_Exec_File (host_cbuf, fs_usercfg->string);
	// reparse the command line for + commands other than set
	// (sets still done, but it doesn't matter)
	if (isDedicated || (cl_quakerc->int_val && check_quakerc ()))
		Cmd_StuffCmds (host_cbuf);

	Hunk_AllocName (0, "-HOST_HUNKLEVEL-");
	host_hunklevel = Hunk_LowMark ();

	host_initialized = true;

	Con_Printf ("\nVersion %s (build %04d)\n\n", VERSION,
				build_number ());

	Con_Printf ("\x80\x81\x81\x82 %s Initialized\x80\x81\x81\x82\n", PROGRAM);
	Con_NewMap ();

	CL_UpdateScreen (cl.time);
}

/*
	Host_Shutdown

	FIXME: this is a callback from Sys_Quit and Sys_Error.  It would be
	better to run quit through here before final handoff to the sys code.
*/
void
Host_Shutdown (void)
{
	static qboolean isdown = false;

	if (isdown) {
		printf ("recursive shutdown\n");
		return;
	}
	isdown = true;

	// keep Con_Printf from trying to update the screen
	scr_disabled_for_loading = true;

	Host_WriteConfiguration ();

	NET_Shutdown ();
	if (cls.state != ca_dedicated) {
		CDAudio_Shutdown ();
		S_Shutdown ();
		IN_Shutdown ();
		VID_Shutdown ();
	}
	Con_Shutdown ();
}

/*
	sv_main.c

	(description)

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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#include <stdarg.h>
#include <stdlib.h>

#include "QF/cmd.h"
#include "QF/console.h"
#include "QF/cvar.h"
#include "QF/model.h"
#include "QF/msg.h"
#include "QF/plugin.h"
#include "QF/qargs.h"
#include "QF/sys.h"
#include "QF/va.h"
#include "QF/ver_check.h"
#include "QF/vfs.h"
#include "QF/zone.h"

#include "bothdefs.h"
#include "buildnum.h"
#include "compat.h"
#include "client.h"		//FIXME needed by cls below (for netchan)
#include "crudefile.h"
#include "game.h"
#include "net.h"
#include "pmove.h"
#include "server.h"
#include "sv_progs.h"

client_t   *host_client;				// current client
client_static_t cls;					//FIXME needed by netchan :/

double      sv_frametime;
double      realtime;					// without any filtering or bounding

int         host_hunklevel;

netadr_t    master_adr[MAX_MASTERS];	// address of group servers

quakeparms_t host_parms;

qboolean    host_initialized;			// true if into command execution
qboolean    rcon_from_user;

// DoS protection
// FLOOD_PING, FLOOD_LOG, FLOOD_CONNECT, FLOOD_STATUS, FLOOD_RCON, FLOOD_BAN
// FIXME: these default values need to be tweaked after more testing

double      netdosexpire[DOSFLOODCMDS] = { 1, 1, 2, 0.9, 1, 5 };
double      netdosvalues[DOSFLOODCMDS] = { 12, 1, 3, 1, 1, 1 };

cvar_t     *fs_globalcfg;
cvar_t     *fs_usercfg;

cvar_t     *sv_console_plugin;

cvar_t     *sv_allow_status;
cvar_t     *sv_allow_log;
cvar_t     *sv_allow_ping;

cvar_t     *sv_extensions;				// Use the extended protocols

cvar_t     *sv_mintic;					// bound the size of the
cvar_t     *sv_maxtic;					// physics time tic

cvar_t     *sv_netdosprotect;			// tone down DoS from quake servers

cvar_t     *timeout;					// seconds without any message
cvar_t     *zombietime;					// seconds to sink messages after
										// disconnect

cvar_t     *rcon_password;				// password for remote server
cvar_t     *admin_password;				// password for admin commands

cvar_t     *password;					// password for entering the game
cvar_t     *spectator_password;			// password for entering as a
										// spectator

cvar_t     *allow_download;
cvar_t     *allow_download_skins;
cvar_t     *allow_download_models;
cvar_t     *allow_download_sounds;
cvar_t     *allow_download_maps;

cvar_t     *sv_highchars;
cvar_t     *sv_phs;

cvar_t     *pausable;

extern cvar_t *sv_timekick;
extern cvar_t *sv_timekick_fuzz;
extern cvar_t *sv_timekick_interval;

cvar_t     *sv_minqfversion;			// Minimum QF version allowed to
										// connect
cvar_t     *sv_maxrate;					// Maximum allowable rate (silently
										// capped)

cvar_t     *sv_timestamps;
cvar_t     *sv_timefmt;

// game rules mirrored in svs.info
cvar_t     *fraglimit;
cvar_t     *timelimit;
cvar_t     *teamplay;
cvar_t     *samelevel;
cvar_t     *maxclients;
cvar_t     *maxspectators;
cvar_t     *deathmatch;					// 0, 1, or 2
cvar_t     *spawn;
cvar_t     *watervis;

cvar_t     *hostname;

VFile      *sv_logfile;
VFile      *sv_fraglogfile;

cvar_t     *pr_gc;
cvar_t     *pr_gc_interval;
int         pr_gc_count = 0;

void        SV_AcceptClient (netadr_t adr, int userid, char *userinfo);
void        Master_Shutdown (void);


const char *client_info_filters[] = {  // Info keys needed by client
	"name",
	"topcolor",
	"bottomcolor",
	"rate",
	"msg",
	"skin",
	"team",
	"noaim",
	"pmodel",
	"emodel",
	"spectator",
	"*spectator",
	"*ver",
	NULL
};


qboolean
ServerPaused (void)
{
	return sv.paused;
}

/*
	SV_Shutdown

	Quake calls this before calling Sys_Quit or Sys_Error
*/
void
SV_Shutdown (void)
{
	Master_Shutdown ();
	if (sv_logfile) {
		Qclose (sv_logfile);
		sv_logfile = NULL;
	}
	if (sv_fraglogfile) {
		Qclose (sv_fraglogfile);
		sv_logfile = NULL;
	}
	NET_Shutdown ();
	Con_Shutdown ();
}

/*
	SV_Error

	Sends a datagram to all the clients informing them of the server crash,
	then exits
*/
void
SV_Error (const char *error, ...)
{
	va_list     argptr;
	static char string[1024];
	static qboolean inerror = false;

	if (inerror)
		Sys_Error ("SV_Error: recursively entered (%s)", string);

	inerror = true;

	va_start (argptr, error);
	vsnprintf (string, sizeof (string), error, argptr);
	va_end (argptr);

	SV_Printf ("SV_Error: %s\n", string);

	SV_FinalMessage (va ("server crashed: %s\n", string));

	SV_Shutdown ();

	Sys_Error ("SV_Error: %s\n", string);
}

/*
	SV_FinalMessage

	Used by SV_Error and SV_Quit_f to send a final message to all connected
	clients before the server goes down.  The messages are sent immediately,
	not just stuck on the outgoing message list, because the server is going
	to totally exit after returning from this function.
*/
void
SV_FinalMessage (const char *message)
{
	client_t   *cl;
	int         i;

	SZ_Clear (net_message->message);
	MSG_WriteByte (net_message->message, svc_print);
	MSG_WriteByte (net_message->message, PRINT_HIGH);
	MSG_WriteString (net_message->message, message);
	MSG_WriteByte (net_message->message, svc_disconnect);

	for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++)
		if (cl->state >= cs_spawned)
			Netchan_Transmit (&cl->netchan, net_message->message->cursize,
							  net_message->message->data);
}

/*
	SV_DropClient

	Called when the player is totally leaving the server, either willingly
	or unwillingly.  This is NOT called if the entire server is quiting
	or crashing.
*/
void
SV_DropClient (client_t *drop)
{
	SV_SavePenaltyFilter (drop, ft_mute, drop->lockedtill);
	SV_SavePenaltyFilter (drop, ft_cuff, drop->cuff_time);

	// add the disconnect
	MSG_WriteByte (&drop->netchan.message, svc_disconnect);

	if (drop->state == cs_spawned) {
		if (!drop->spectator) {
			// call the prog function for removing a client
			// this will set the body to a dead frame, among other things
			*sv_globals.self = EDICT_TO_PROG (&sv_pr_state, drop->edict);
			PR_ExecuteProgram (&sv_pr_state, sv_funcs.ClientDisconnect);
		} else if (SpectatorDisconnect) {
			// call the prog function for removing a client
			// this will set the body to a dead frame, among other things
			*sv_globals.self = EDICT_TO_PROG (&sv_pr_state, drop->edict);
			PR_ExecuteProgram (&sv_pr_state, SpectatorDisconnect);
		}
	}
	if (drop->spectator)
		SV_Printf ("Spectator %s removed\n", drop->name);
	else
		SV_Printf ("Client %s removed\n", drop->name);

	if (drop->download) {
		Qclose (drop->download);
		drop->download = NULL;
	}
	if (drop->upload) {
		Qclose (drop->upload);
		drop->upload = NULL;
	}
	*drop->uploadfn = 0;

	drop->state = cs_zombie;			// become free in a few seconds
	drop->connection_started = realtime;	// for zombie timeout

	drop->old_frags = 0;
	SVfloat (drop->edict, frags) = 0;
	drop->name[0] = 0;
	memset (drop->userinfo, 0, sizeof (drop->userinfo));

	// send notification to all remaining clients
	SV_FullClientUpdate (drop, &sv.reliable_datagram);
}

int
SV_CalcPing (client_t *cl)
{
	float       ping;
	int         count, i;
	register client_frame_t *frame;

	ping = 0;
	count = 0;
	for (frame = cl->frames, i = 0; i < UPDATE_BACKUP; i++, frame++) {
		if (frame->ping_time > 0) {
			ping += frame->ping_time;
			count++;
		}
	}
	if (!count)
		return 9999;
	ping /= count;

	return ping * 1000;
}

/*
	SV_FullClientUpdate

	Writes all update values to a sizebuf
*/
void
SV_FullClientUpdate (client_t *client, sizebuf_t *buf)
{
	char        info[MAX_INFO_STRING];
	int         i;

	i = client - svs.clients;

//	SV_Printf("SV_FullClientUpdate:  Updated frags for client %d\n", i);

	MSG_WriteByte (buf, svc_updatefrags);
	MSG_WriteByte (buf, i);
	MSG_WriteShort (buf, client->old_frags);

	MSG_WriteByte (buf, svc_updateping);
	MSG_WriteByte (buf, i);
	MSG_WriteShort (buf, SV_CalcPing (client));

	MSG_WriteByte (buf, svc_updatepl);
	MSG_WriteByte (buf, i);
	MSG_WriteByte (buf, client->lossage);

	MSG_WriteByte (buf, svc_updateentertime);
	MSG_WriteByte (buf, i);
	MSG_WriteFloat (buf, realtime - client->connection_started);

	strncpy (info, client->userinfo, sizeof (info));
	Info_RemovePrefixedKeys (info, '_', client_info_filters);	// server passwords, etc

	MSG_WriteByte (buf, svc_updateuserinfo);
	MSG_WriteByte (buf, i);
	MSG_WriteLong (buf, client->userid);
	MSG_WriteString (buf, info);
}

/*
	SV_FullClientUpdateToClient

	Writes all update values to a client's reliable stream
*/
void
SV_FullClientUpdateToClient (client_t *client, client_t *cl)
{
	ClientReliableCheckBlock (cl, 24 + strlen (client->userinfo));
	if (cl->num_backbuf) {
		SV_FullClientUpdate (client, &cl->backbuf);
		ClientReliable_FinishWrite (cl);
	} else
		SV_FullClientUpdate (client, &cl->netchan.message);
}

/* CONNECTIONLESS COMMANDS */

/*
	CheckForFlood :: EXPERIMENTAL

	Makes it more difficult to use Quake servers for DoS attacks against
	other sites.

	Bad sides: affects gamespy and spytools somewhat...
*/
int
CheckForFlood (flood_enum_t cmdtype)
{
	double      currenttime;
	double      oldestTime;
	static double lastmessagetime = 0;
	static flood_t floodstatus[DOSFLOODCMDS][DOSFLOODIP];
	int         oldest, i;
	static qboolean firsttime = true;

	if (!sv_netdosprotect->int_val)
		return 0;

	oldestTime = 0x7fffffff;
	oldest = 0;

	if (firsttime) {
		memset (floodstatus, sizeof (flood_t) * DOSFLOODCMDS * DOSFLOODIP, 0);

		firsttime = false;
	}

	currenttime = Sys_DoubleTime ();

	for (i = 0; i < DOSFLOODIP; i++) {
		if (NET_CompareBaseAdr (net_from, floodstatus[cmdtype][i].adr))
			break;
		if (floodstatus[cmdtype][i].issued < oldestTime) {
			oldestTime = floodstatus[cmdtype][i].issued;
			oldest = i;
		}
	}

	if (i < DOSFLOODIP && floodstatus[cmdtype][i].issued) {
		if ((floodstatus[cmdtype][i].issued + netdosexpire[cmdtype])
			> currenttime) {
			floodstatus[cmdtype][i].floodcount += 1;
			if (floodstatus[cmdtype][i].floodcount > netdosvalues[cmdtype]) {
				if ((lastmessagetime + 5) < currenttime)
					SV_Printf ("Blocking type %d flood from (or to) %s\n",
								cmdtype, NET_AdrToString (net_from));
				floodstatus[cmdtype][i].floodcount = 0;
				floodstatus[cmdtype][i].issued = currenttime;
				floodstatus[cmdtype][i].cmdcount += 1;
				lastmessagetime = currenttime;
				return 1;
			}
		} else {
			floodstatus[cmdtype][i].floodcount = 0;
		}
	}

	if (i == DOSFLOODIP) {
		i = oldest;
		floodstatus[cmdtype][i].adr = net_from;
		floodstatus[cmdtype][i].firstseen = currenttime;
		floodstatus[cmdtype][i].cmdcount = 0;
		floodstatus[cmdtype][i].floodcount = 0;
	}
	floodstatus[cmdtype][i].issued = currenttime;
	floodstatus[cmdtype][i].cmdcount += 1;
	return 0;
}

/*
	SVC_Status

	Responds with all the info that qplug or qspy can see
	This message can be up to around 5k with worst case string lengths.
*/
void
SVC_Status (void)
{
	client_t   *cl;
	int         ping, bottom, top, i;

	extern int  con_printf_no_log;

	if (!sv_allow_status->int_val)
		return;
	if (CheckForFlood (FLOOD_STATUS))
		return;

	con_printf_no_log = 1;
	Cmd_TokenizeString ("status");
	SV_BeginRedirect (RD_PACKET);
	SV_Printf ("%s\n", svs.info);
	for (i = 0; i < MAX_CLIENTS; i++) {
		cl = &svs.clients[i];
		if ((cl->state == cs_connected || cl->state == cs_spawned)
			&& !cl->spectator) {
			top = atoi (Info_ValueForKey (cl->userinfo, "topcolor"));
			bottom = atoi (Info_ValueForKey (cl->userinfo, "bottomcolor"));
			top = (top < 0) ? 0 : ((top > 13) ? 13 : top);
			bottom = (bottom < 0) ? 0 : ((bottom > 13) ? 13 : bottom);
			ping = SV_CalcPing (cl);
			SV_Printf ("%i %i %i %i \"%s\" \"%s\" %i %i\n", cl->userid,
						cl->old_frags,
						(int) (realtime - cl->connection_started) / 60, ping,
						cl->name, Info_ValueForKey (cl->userinfo, "skin"), top,
						bottom);
		}
	}
	SV_EndRedirect ();
	con_printf_no_log = 0;
}

/* SV_CheckLog */
#define	LOG_HIGHWATER	4096
#define	LOG_FLUSH		10*60

void
SV_CheckLog (void)
{
	sizebuf_t  *sz;

	sz = &svs.log[svs.logsequence & 1];

	// bump sequence if allmost full, or ten minutes have passed and
	// there is something still sitting there
	if (sz->cursize > LOG_HIGHWATER
		|| (realtime - svs.logtime > LOG_FLUSH && sz->cursize)) {
		// swap buffers and bump sequence
		svs.logtime = realtime;
		svs.logsequence++;
		sz = &svs.log[svs.logsequence & 1];
		sz->cursize = 0;
		SV_Printf ("beginning fraglog sequence %i\n", svs.logsequence);
	}

}

/*
	SVC_Log

	Responds with all the logged frags for ranking programs.
	If a sequence number is passed as a parameter and it is
	the same as the current sequence, an A2A_NACK will be returned
	instead of the data.
*/
void
SVC_Log (void)
{
	char        data[MAX_DATAGRAM + 64];
	int         seq;

	if (!sv_allow_log->int_val)
		return;
	if (CheckForFlood (FLOOD_LOG))
		return;

	if (Cmd_Argc () == 2)
		seq = atoi (Cmd_Argv (1));
	else
		seq = -1;

	if (seq == svs.logsequence - 1 || !sv_fraglogfile) {
		// they already have this data, or we aren't logging frags
		data[0] = A2A_NACK;
		NET_SendPacket (1, data, net_from);
		return;
	}

	Con_DPrintf ("sending log %i to %s\n", svs.logsequence - 1,
				 NET_AdrToString (net_from));

//	snprintf (data, sizeof (data), "stdlog %i\n", svs.logsequence-1);
//	strncat (data,  (char *)svs.log_buf[((svs.logsequence-1)&1)],
//	sizeof(data) - strlen (data));
	snprintf (data, sizeof (data), "stdlog %i\n%s",
			  svs.logsequence - 1,
			  (char *) svs.log_buf[((svs.logsequence - 1) & 1)]);

	NET_SendPacket (strlen (data) + 1, data, net_from);
}

/*
	SVC_Ping

	Just responds with an acknowledgement
*/
void
SVC_Ping (void)
{
	char        data;

	if (!sv_allow_ping->int_val)
		return;
	if (CheckForFlood (FLOOD_PING))
		return;

	data = A2A_ACK;

	NET_SendPacket (1, &data, net_from);
}

/*
	SVC_GetChallenge

	Returns a challenge number that can be used
	in a subsequent client_connect command.
	We do this to prevent denial of service attacks that
	flood the server with invalid connection IPs.  With a
	challenge, they must give a valid IP address.
*/
void
SVC_GetChallenge (void)
{
	int         oldest, oldestTime, i;
	char       *extended = "";

	oldest = 0;
	oldestTime = 0x7fffffff;

	// see if we already have a challenge for this ip
	for (i = 0; i < MAX_CHALLENGES; i++) {
		if (NET_CompareBaseAdr (net_from, svs.challenges[i].adr))
			break;
		if (svs.challenges[i].time < oldestTime) {
			oldestTime = svs.challenges[i].time;
			oldest = i;
		}
	}

	if (i == MAX_CHALLENGES) {
		// overwrite the oldest
		svs.challenges[oldest].challenge = (rand () << 16) ^ rand ();
		svs.challenges[oldest].adr = net_from;
		svs.challenges[oldest].time = realtime;
		i = oldest;
	}

	if (sv_extensions->int_val) {
		extended = " QF";
	}

	// send it to the client
	Netchan_OutOfBandPrint (net_from, "%c%i%s", S2C_CHALLENGE,
							svs.challenges[i].challenge, extended);
}

/*
	SVC_DirectConnect

	A connection request that did not come from the master
*/
void
SVC_DirectConnect (void)
{
	char        userinfo[1024];
	const char *s;
	client_t   *cl, *newcl;
	client_t    temp;
	edict_t    *ent;
	int         challenge, clients, edictnum, qport, spectators, version, i;
	static int  userid;
	netadr_t    adr;
	qboolean    spectator;

	if (CheckForFlood (FLOOD_CONNECT))
		return;

	version = atoi (Cmd_Argv (1));
	if (version != PROTOCOL_VERSION) {
		Netchan_OutOfBandPrint (net_from, "%c\nServer is version %s.\n",
								A2C_PRINT, QW_VERSION);
		SV_Printf ("* rejected connect from version %i\n", version);
		return;
	}

	qport = atoi (Cmd_Argv (2));

	challenge = atoi (Cmd_Argv (3));

	// note an extra byte is needed to replace spectator key
	strncpy (userinfo, Cmd_Argv (4), sizeof (userinfo) - 2);
	userinfo[sizeof (userinfo) - 2] = 0;

	// Validate the userinfo string.
	if (!Info_Validate(userinfo)) {
		Netchan_OutOfBandPrint (net_from, "%c\nInvalid userinfo string.\n",
								A2C_PRINT);
		return;
	}

	// see if the challenge is valid
	for (i = 0; i < MAX_CHALLENGES; i++) {
		if (NET_CompareBaseAdr (net_from, svs.challenges[i].adr)) {
			if (challenge == svs.challenges[i].challenge)
				break;					// good
			Netchan_OutOfBandPrint (net_from, "%c\nBad challenge.\n",
									A2C_PRINT);
			return;
		}
	}
	if (i == MAX_CHALLENGES) {
		Netchan_OutOfBandPrint (net_from, "%c\nNo challenge for address.\n",
								A2C_PRINT);
		return;
	}

	s = Info_ValueForKey (userinfo, "*qf_version");
	if ((!s[0]) || sv_minqfversion->string[0]) {	// kick old clients?
		if (ver_compare (s, sv_minqfversion->string) < 0) {
			SV_Printf ("%s: Version %s is less than minimum version %s.\n",
						NET_AdrToString (net_from), s,
					   sv_minqfversion->string);

			Netchan_OutOfBandPrint (net_from, "%c\nserver requires QuakeForge "
									"v%s or greater. Get it from "
									"http://www.quakeforge.net/\n", A2C_PRINT,
									sv_minqfversion->string);
			return;
		}
	}
	// check for password or spectator_password
	s = Info_ValueForKey (userinfo, "spectator");
	if (s[0] && strcmp (s, "0")) {
		if (spectator_password->string[0] &&
			!strcaseequal (spectator_password->string, "none") &&
			!strequal (spectator_password->string, s)) {	// failed
			SV_Printf ("%s: spectator password failed\n",
						NET_AdrToString (net_from));
			Netchan_OutOfBandPrint (net_from,
									"%c\nrequires a spectator password\n\n",
									A2C_PRINT);
			return;
		}
		Info_RemoveKey (userinfo, "spectator");	// remove passwd
		Info_SetValueForStarKey (userinfo, "*spectator", "1", MAX_INFO_STRING,
								 !sv_highchars->int_val);
		spectator = true;
	} else {
		s = Info_ValueForKey (userinfo, "password");
		if (password->string[0]
			&& !strcaseequal (password->string, "none")
			&& !strequal (password->string, s)) {
			SV_Printf ("%s:password failed\n", NET_AdrToString (net_from));
			Netchan_OutOfBandPrint (net_from,
									"%c\nserver requires a password\n\n",
									A2C_PRINT);
			return;
		}
		spectator = false;
		Info_RemoveKey (userinfo, "password");	// remove passwd
	}

	adr = net_from;
	userid++;							// so every client gets a unique id

	newcl = &temp;
	memset (newcl, 0, sizeof (client_t));

	newcl->userid = userid;

	// works properly
	if (!sv_highchars->int_val) {
		byte       *p, *q;

		for (p = (byte *) newcl->userinfo, q = (byte *) userinfo;
			 *q && p < (byte *) newcl->userinfo + sizeof (newcl->userinfo) - 1;
			 q++)
			if (*q > 31 && *q <= 127)
				*p++ = *q;
	} else
		strncpy (newcl->userinfo, userinfo, sizeof (newcl->userinfo) - 1);

	// if there is allready a slot for this ip, drop it
	for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
		if (cl->state == cs_free)
			continue;
		if (NET_CompareBaseAdr (adr, cl->netchan.remote_address)
			&& (cl->netchan.qport == qport
				|| adr.port == cl->netchan.remote_address.port)) {
			if (cl->state == cs_connected) {
				SV_Printf ("%s:dup connect\n", NET_AdrToString (adr));
				userid--;
				return;
			}

			SV_Printf ("%s:reconnect\n", NET_AdrToString (adr));
			SV_DropClient (cl);
			break;
		}
	}

	// count up the clients and spectators
	clients = 0;
	spectators = 0;
	for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
		if (cl->state == cs_free)
			continue;
		if (cl->spectator)
			spectators++;
		else
			clients++;
	}

	// if at server limits, refuse connection
	if (maxclients->int_val > MAX_CLIENTS)
		Cvar_SetValue (maxclients, MAX_CLIENTS);
	if (maxspectators->int_val > MAX_CLIENTS)
		Cvar_SetValue (maxspectators, MAX_CLIENTS);
	if (maxspectators->int_val + maxclients->int_val > MAX_CLIENTS)
		Cvar_SetValue (maxspectators, MAX_CLIENTS - maxclients->int_val);
	if ((spectator && spectators >= maxspectators->int_val)
		|| (!spectator && clients >= maxclients->int_val)) {
		SV_Printf ("%s:full connect\n", NET_AdrToString (adr));
		Netchan_OutOfBandPrint (adr, "%c\nserver is full\n\n", A2C_PRINT);
		return;
	}
	// find a client slot
	newcl = NULL;
	for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
		if (cl->state == cs_free) {
			newcl = cl;
			break;
		}
	}
	if (!newcl) {
		SV_Printf ("WARNING: miscounted available clients\n");
		return;
	}
	// build a new connection
	// accept the new client
	// this is the only place a client_t is ever initialized
	*newcl = temp;

	Netchan_OutOfBandPrint (adr, "%c", S2C_CONNECTION);

	edictnum = (newcl - svs.clients) + 1;

	Netchan_Setup (&newcl->netchan, adr, qport);

	newcl->state = cs_connected;
	newcl->prespawned = false;
	newcl->spawned = false;
	svs.num_clients++;

	newcl->datagram.allowoverflow = true;
	newcl->datagram.data = newcl->datagram_buf;
	newcl->datagram.maxsize = sizeof (newcl->datagram_buf);

	// spectator mode can ONLY be set at join time
	newcl->spectator = spectator;

	ent = EDICT_NUM (&sv_pr_state, edictnum);
	newcl->edict = ent;

	// parse some info from the info strings
	SV_ExtractFromUserinfo (newcl);

	// JACK: Init the floodprot stuff.
	for (i = 0; i < 10; i++)
		newcl->whensaid[i] = 0.0;
	newcl->whensaidhead = 0;
	newcl->lockedtill = SV_RestorePenaltyFilter(newcl, ft_mute);

	// call the progs to get default spawn parms for the new client
	PR_ExecuteProgram (&sv_pr_state, sv_funcs.SetNewParms);
	for (i = 0; i < NUM_SPAWN_PARMS; i++)
		newcl->spawn_parms[i] = sv_globals.parms[i];

	if (newcl->spectator)
		SV_Printf ("Spectator %s (%s) connected\n", newcl->name,
				   NET_AdrToString (adr));
	else
		Con_Printf ("Client %s (%s) connected\n", newcl->name,
					NET_AdrToString (adr));
	newcl->sendinfo = true;

	// QuakeForge stuff.
	newcl->msecs = 0;
	newcl->msec_cheating = 0;
	newcl->last_check = -1;

	newcl->cuff_time = SV_RestorePenaltyFilter(newcl, ft_cuff);
}

int
Rcon_Validate (cvar_t *pass)
{
	if (!strlen (pass->string))
		return 0;

	if (strcmp (Cmd_Argv (1), pass->string))
		return 0;

	return 1;
}

char *
Name_of_sender (void)
{
	client_t   *cl;
	int         i;

	for (i=0, cl=svs.clients ; i<MAX_CLIENTS ; i++,cl++) {
		if (cl->state == cs_free)
			continue;
		if (!NET_CompareBaseAdr(net_from, cl->netchan.remote_address))
			continue;
		if (cl->netchan.remote_address.port != net_from.port)
			continue;
		return cl->name;
	}
	return NULL;
}

/*
  SVC_RemoteCommand

  A client issued an rcon command.
  Shift down the remaining args
  Redirect all printfs
*/
void
SVC_RemoteCommand (void)
{
	const char *command;
	char       *name;
	qboolean    admin_cmd = false;
	qboolean    do_cmd = false;

	if (CheckForFlood (FLOOD_RCON))
		return;

	if (Rcon_Validate (rcon_password)) {
		do_cmd = true;
	} else if (Rcon_Validate (admin_password)) {
		admin_cmd = true;
		if (strcmp(Cmd_Argv(2), "say") == 0
			||  strcmp(Cmd_Argv(2), "kick") == 0
			||  strcmp(Cmd_Argv(2), "ban") == 0
			||  strcmp(Cmd_Argv(2), "mute") == 0
			||  strcmp(Cmd_Argv(2), "cuff") == 0
			||  strcmp(Cmd_Argv(2), "exec") == 0
			||  strcmp(Cmd_Argv(2), "addip") == 0
			||  strcmp(Cmd_Argv(2), "listip") == 0
			||  strcmp(Cmd_Argv(2), "writeip") == 0
			||  strcmp(Cmd_Argv(2), "removeip") == 0)
			do_cmd = true;
	}
	if ((name = Name_of_sender())) {			// log issuer
		if (do_cmd && admin_cmd) {
			SV_BroadcastPrintf (PRINT_HIGH, "Admin %s issued %s command:\n",
								name, Cmd_Argv(2));
		} else if (admin_cmd) {
			SV_Printf ("Admin %s issued %s command:\n", name, Cmd_Argv(2));
		} else {
			SV_Printf("User %s %s rcon command:\n", name,
					   do_cmd? "issued":"attempted");
		}
	}
	
	if (do_cmd) {
		command = Cmd_Args (2);

		SV_Printf ("Rcon from %s:\n\trcon (hidden) %s\n",
					NET_AdrToString (net_from), command);

		SV_BeginRedirect (RD_PACKET);
		if (name)
			rcon_from_user = true;
		Cmd_ExecuteString (command, src_command);
		rcon_from_user = false;
	} else {
		SV_Printf ("Bad rcon from %s:\n%s\n", NET_AdrToString (net_from),
					net_message->message->data + 4);

		SV_BeginRedirect (RD_PACKET);
		if (admin_cmd) {
			SV_Printf("Command not valid with admin password.\n");
		} else {
			SV_Printf ("Bad rcon_password.\n");
		}
	}

	SV_EndRedirect ();
}

/*
  SV_ConnectionlessPacket

  A connectionless packet has four leading 0xff
  characters to distinguish it from a game channel.
  Clients that are in the game can still send
  connectionless packets.
*/
void
SV_ConnectionlessPacket (void)
{
	const char *c, *s;

	MSG_BeginReading (net_message);
	MSG_ReadLong (net_message);					// skip the -1 marker

	s = MSG_ReadStringLine (net_message);

	Cmd_TokenizeString (s);

	c = Cmd_Argv (0);

	if (!strcmp (c, "ping")
		|| (c[0] == A2A_PING && (c[1] == 0 || c[1] == '\n'))) {
		SVC_Ping ();
		return;
	}
	if (c[0] == A2A_ACK && (c[1] == 0 || c[1] == '\n')) {
		SV_Printf ("A2A_ACK from %s\n", NET_AdrToString (net_from));
		return;
	} else if (!strcmp (c, "status")) {
		SVC_Status ();
		return;
	} else if (!strcmp (c, "log")) {
		SVC_Log ();
		return;
	} else if (!strcmp (c, "connect")) {
		SVC_DirectConnect ();
		return;
	} else if (!strcmp (c, "getchallenge")) {
		SVC_GetChallenge ();
		return;
	} else if (!strcmp (c, "rcon"))
		SVC_RemoteCommand ();
	else
		SV_Printf ("bad connectionless packet from %s:\n%s\n",
					NET_AdrToString (net_from), s);
}

/*
	PACKET FILTERING

	You can add or remove addresses from the filter list with:

	addip <ip>
	removeip <ip>

	The ip address is specified in dot format, and any unspecified digits
	will match any value, so you can specify an entire class C network with
	"addip 192.246.40".

	Removeip will only remove an address specified exactly the same way.
	You cannot addip a subnet, then removeip a single host.

	listip
	Prints the current list of filters.

	writeip
	Dumps "addip <ip>" commands to listip.cfg so it can be execed at a
	later date.  The filter lists are not saved and restored by default,
	because I beleive it would cause too much confusion.

	filterban <0 or 1>

	If 1 (the default), then ip addresses matching the current list will be
	prohibited from entering the game.  This is the default setting.

	If 0, then only addresses matching the list will be allowed.  This lets
	you easily set up a private game, or a game that only allows players
	from your local network.
*/

typedef struct {
	int				mask;
#ifdef HAVE_IPV6
	byte			ip[16];
#else
	byte			ip[4];
#endif
	double			time;
	filtertype_t	type;
} ipfilter_t;

#define	MAX_IPFILTERS	1024

cvar_t     *filterban;
cvar_t     *sv_filter_automask;
int         numipfilters;
ipfilter_t  ipfilters[MAX_IPFILTERS];
unsigned int ipmasks[33]; // network byte order

void
SV_GenerateIPMasks (void)
{
	int i;
	unsigned long int j = 0xFFFFFFFF;

	for (i = 32; i >= 0; i--) {
		ipmasks[i] = htonl (j);
		j = j << 1;
	}
}

// Note: this function is non-reentrant and not threadsafe
const char *
SV_PrintIP (byte *ip)
{
#ifdef HAVE_IPV6
	static char buf[INET6_ADDRSTRLEN];
	if (!inet_ntop (AF_INET6, ip, buf, INET6_ADDRSTRLEN))
#else
	static char buf[INET_ADDRSTRLEN];
	if (!inet_ntop (AF_INET, ip, buf, INET_ADDRSTRLEN))
#endif
        Sys_Error ("SV_CleanIPList: inet_ntop_failed.  wtf?\n");
	return buf;
}

static inline void
SV_MaskIPTrim (byte *ip, int mask)
{
    int i;
#ifdef HAVE_IPV6
    int intcount = 4;
#else
    int intcount = 1;
#endif

    for (i = 0; i < intcount; i++) {
        ((unsigned int *)ip)[i] &= ipmasks[mask > 32 ? 32 : mask];
        if ((mask -= 32) < 0)
            mask = 0;
    }
}

// assumes b has already been masked
static inline qboolean
SV_MaskIPCompare (byte *a, byte *b, int mask)
{
	int i;
#ifdef HAVE_IPV6
	int intcount = 4;
#else
	int intcount = 1;
#endif

	for (i = 0; i < intcount; i++) {
		if ((((unsigned int *)a)[i] & ipmasks[mask > 32 ? 32 : mask]) != ((unsigned int *)b)[i])
			return false;
		if ((mask -= 32) < 0)
			mask = 0;
	}

	return true;
}

static inline qboolean
SV_IPCompare (byte *a, byte *b)
{
	int i;
#ifdef HAVE_IPV6
	int intcount = 4;
#else
	int intcount = 1;
#endif

	for (i = 0; i < intcount; i++)
		if (((unsigned int *)a)[i] != ((unsigned int *)b)[i])
			return false;

	return true;
}

static inline void
SV_IPCopy (byte *dest, byte *src)
{
	int i;
#ifdef HAVE_IPV6
	int intcount = 4;
#else
	int intcount = 1;
#endif

	for (i = 0; i < intcount; i++)
		((unsigned int *)dest)[i] = ((unsigned int *)src)[i];
}

qboolean
SV_StringToFilter (const char *address, ipfilter_t *f)
{
#ifdef HAVE_IPV6
	byte		b[16] = {};
#else
	byte        b[4] = {};
#endif
	int			mask = 0;
	int			i;
	char		*s;
	char		*slash;
	char		*c;

	s = strdup (address);
	if (!s)
		Sys_Error ("SV_StringToFilter: memory allocation failure\n");

	// Parse out the mask (the /8 part)
	if ((slash = strchr (s, '/'))) {
		char *endptr;
		*slash = '\0';
		slash++;
		if (*slash < '0' || *slash > '9' || strchr (slash, '/'))
			goto bad_address;
		mask = strtol (slash, &endptr, 10);
		if (!*slash || *endptr)
			goto bad_address;
	} else
		mask = -1;

	// parse the ip for ipv6
#ifdef HAVE_IPV6
	if (inet_pton (AF_INET6, s, b) != 1) {
		b[10] = 0xFF; // Prefix bytes for hosts that don't support ipv6
		b[11] = 0xFF; // (see RFC 2373, section 2.5.4)
		i = 12;
#else
		i = 0;
#endif
		c = s;

		// parse for ipv4, as dotted quad, only we have implicit trailing segments
		do {
			int j;
			if (*c == '.')
				c++;
			j = strtol (c, &c, 10);
			if (j < 0 || j > 255 || i >= sizeof (b))
				goto bad_address;
			b[i++] = j;
		} while (*c == '.');
		if (*c)
			goto bad_address;

		// change trailing 0 segments to be a mask, eg 1.2.0.0 gives a /16 mask
		if (mask == -1) {
			if (sv_filter_automask->int_val) {
				mask = sizeof (b) * 8;
				i = sizeof (b) - 1;
				while (i >= 0 && !b[i]) {
					mask -= 8;
					i--;
				}
			} else
				mask = 0;
		}
#ifdef HAVE_IPV6
	}
#endif

#ifdef HAVE_IPV6
	if (mask > 128)
#else
	if (mask > 32)
#endif
		goto bad_address;

	// incase they did 1.2.3.4/16, change it to 1.2.0.0 for easier comparison
	SV_MaskIPTrim (b, mask);

	// yada :)
	f->mask = mask;
	SV_IPCopy (f->ip, b);

	free (s);
	return true;

bad_address:
	SV_Printf ("Bad filter address: %s\n", address);
	free (s);
	return false;
}

void
SV_RemoveIPFilter (int i)
{
	for (; i + 1 < numipfilters; i++)
		ipfilters[i] = ipfilters[i + 1];

	numipfilters--;
}


void
SV_CleanIPList (void)
{
	int         i;
	char		*type;

	for (i = 0; i < numipfilters;) {
		if (ipfilters[i].time && (ipfilters[i].time <= realtime)) {
			switch (ipfilters[i].type) {
				case ft_ban: type = "Ban"; break;
				case ft_mute: type = "Mute"; break;
				case ft_cuff: type = "Cuff"; break;
				default: Sys_Error ("SV_CleanIPList: invalid filter type");
			}
			SV_Printf ("SV_CleanIPList: %s for %s/%d removed\n", type,
					   SV_PrintIP (ipfilters[i].ip), ipfilters[i].mask);
			SV_RemoveIPFilter (i);
		} else
			i++;
	}
}

void
SV_AddIP_f (void)
{
	int         i;
	double		bantime;
	filtertype_t	type;

	if (Cmd_Argc () < 2 || Cmd_Argc () > 4) {
		SV_Printf ("Usage: addip <ip>/<mask> [<time> [<ban/mute/cuff>] ]\n");
		return;
	}

	if (Cmd_Argc () >= 3)
		bantime = atof (Cmd_Argv (2)) * 60;
	else
		bantime = 0.0;

	if (Cmd_Argc () >= 4) {
		if (strequal ("ban", Cmd_Argv (3)))
			type = ft_ban;
		else if (strequal ("mute", Cmd_Argv (3)))
			type = ft_mute;
		else if (strequal ("cuff", Cmd_Argv (3)))
			type = ft_cuff;
		else {
			SV_Printf ("Unknown filter type '%s'\n", Cmd_Argv (3));
			return;
		}
	} else
		type = ft_ban;

	if (numipfilters == MAX_IPFILTERS) {
		SV_Printf ("IP filter list is full\n");
		return;
	}

	if (SV_StringToFilter (Cmd_Argv (1), &ipfilters[numipfilters])) {
		ipfilters[numipfilters].time = bantime ? realtime + bantime : 0.0;
		ipfilters[numipfilters].type = type;
		// FIXME: this should boot any matching clients
	    for (i = 0; i < MAX_CLIENTS; i++) {
			client_t *cl = &svs.clients[i];
			char text[1024];
			char *typestr;
			char timestr[1024];

			if (cl->state == cs_free)
				continue;

			if (SV_MaskIPCompare (cl->netchan.remote_address.ip,
								  ipfilters[numipfilters].ip,
								  ipfilters[numipfilters].mask)) {
				switch (type) {
					case ft_ban:
						typestr = "banned";
						SV_DropClient (cl);
						break;
					case ft_mute:
						typestr = "muted";
						cl->lockedtill = bantime;
						break;
					case ft_cuff:
						typestr = "cuffed";
						cl->cuff_time = bantime;
						break;
					default:
						Sys_Error ("SV_AddIP_f: unknown filter type %d", type);
				}
				if (bantime)
					snprintf (timestr, sizeof (timestr), "for %.1f minutes",
							  bantime / 60);
				else
					strncpy (timestr, "permanently", sizeof (timestr));
				snprintf (text, sizeof (text), "You are %s %s\n%s",
						  typestr, timestr, type == ft_ban ? "" :
						  "\nReconnecting won't help...");
				ClientReliableWrite_Begin (cl, svc_centerprint, strlen (text) + 2);
				ClientReliableWrite_String (cl, text);
				// FIXME: print on the console too
			}
		}
		SV_Printf ("Added IP Filter for %s/%u\n",
				   SV_PrintIP (ipfilters[numipfilters].ip),
				   ipfilters[numipfilters].mask);
		numipfilters++;
	}
}

void
SV_RemoveIP_f (void)
{
	int         i;
	ipfilter_t  f;

	if (!SV_StringToFilter (Cmd_Argv (1), &f))
		return;
	for (i = 0; i < numipfilters; i++)
		if (ipfilters[i].mask == f.mask && SV_IPCompare (ipfilters[i].ip, f.ip)) {
			SV_RemoveIPFilter (i);
			SV_Printf ("Removed IP Filter for %s/%u.\n",
					   SV_PrintIP (f.ip), f.mask);
			return;
		}
	SV_Printf ("Didn't find %s/%u.\n", SV_PrintIP (f.ip), f.mask);
}

void
SV_ListIP_f (void)
{
	int         i;
	char		*type;
	char		timestr[30];

	SV_Printf ("IP Filter list:\n");
	for (i = 0; i < numipfilters; i++) {
		switch (ipfilters[i].type) {
			case ft_ban: type = "Ban:"; break;
			case ft_mute: type = "Mute:"; break;
			case ft_cuff: type = "Cuff:"; break;
			default: Sys_Error ("SV_ListIP_f: invalid filter type");
		}

		if (ipfilters[i].time)
			snprintf (timestr, sizeof (timestr), "%ds",
					  (int) (ipfilters[i].time ? ipfilters[i].time - realtime : 0));
		else
			strcpy (timestr, "Permanent");

		SV_Printf ("%-5s %-10s %s/%u\n", type, timestr,
				   SV_PrintIP (ipfilters[i].ip), ipfilters[i].mask);
	}
	SV_Printf ("%d IP Filter%s\n", numipfilters,
			   numipfilters == 1 ? "" : "s");
}

void
SV_WriteIP_f (void)
{
	char        name[MAX_OSPATH];
	int         i;
	VFile      *f;
	char		*type;

	snprintf (name, sizeof (name), "%s/listip.cfg", com_gamedir);

	SV_Printf ("Writing IP Filters to %s.\n", name);

	f = Qopen (name, "wb");
	if (!f) {
		SV_Printf ("Couldn't open %s\n", name);
		return;
	}

	for (i = 0; i < numipfilters; i++) {
		switch (ipfilters[i].type) {
			case ft_ban: type = "ban"; break;
			case ft_mute: type = "mute"; break;
			case ft_cuff: type = "cuff"; break;
			default: Sys_Error ("SV_WriteIP_f: invalid filter type");
		}
		Qprintf (f, "addip %s/%u 0 %s\n", SV_PrintIP (ipfilters[i].ip),
				 ipfilters[i].mask, type);
	}

	Qclose (f);
}

void
SV_netDoSexpire_f (void)
{
	int         arg1, i;

	if (Cmd_Argc () == 1) {
		SV_Printf ("Current DoS prot. expire settings: ");
		for (i = 0; i < DOSFLOODCMDS; i++)
			SV_Printf ("%f ", netdosexpire[i]);
		SV_Printf ("\n");
		if (!sv_netdosprotect->int_val)
			SV_Printf ("(disabled)\n");
		return;
	}

	if (Cmd_Argc () != DOSFLOODCMDS + 1) {
		SV_Printf ("Usage: netdosexpire <ping> <log> <connect> <status> "
				   "<rcon> <ban>\n");
		return;
	}

	for (i = 0; i < DOSFLOODCMDS; i++) {
		arg1 = atoi (Cmd_Argv (i + 1));
		if (arg1 > 0)
			netdosexpire[i] = arg1;
	}
	return;
}

void
SV_netDoSvalues_f (void)
{
	int         arg1, i;

	if (Cmd_Argc () == 1) {
		SV_Printf ("Current DoS prot. value settings: ");
		for (i = 0; i < DOSFLOODCMDS; i++)
			SV_Printf ("%f ", netdosvalues[i]);
		SV_Printf ("\n");
		if (!sv_netdosprotect->int_val)
			SV_Printf ("(disabled)\n");
		return;
	}

	if (Cmd_Argc () != DOSFLOODCMDS + 1) {
		SV_Printf ("Usage: netdosvalues <ping> <log> <connect> <status> "
				   "<rcon> <ban>\n");
		return;
	}

	for (i = 0; i < DOSFLOODCMDS; i++) {
		arg1 = atoi (Cmd_Argv (i + 1));
		if (arg1 > 0)
			netdosvalues[i] = arg1;
	}
	return;
}

void
SV_SendBan (double till)
{
	char        data[128];
//	char       *data2;

	if (CheckForFlood (FLOOD_BAN))
		return;

	data[0] = data[1] = data[2] = data[3] = 0xff;
	data[4] = A2C_PRINT;

	if (till) {
		snprintf (data + 5, sizeof (data) - 5,
				  "\nbanned for %.1f more minutes.\n", (till - realtime)/60.0);
	} else {
		snprintf (data + 5, sizeof (data) - 5, "\nbanned permanently.\n");
	}

	NET_SendPacket (strlen (data), data, net_from);

/*	data[4] = A2C_CLIENT_COMMAND;
	snprintf (data + 5, sizeof (data) - 5, "disconnect\n");
	data2 = data + strlen (data) + 1;
	snprintf (data2, sizeof (data) - (data2 - data), "12345");
	NET_SendPacket (strlen (data) + strlen (data2) + 2, data, net_from);*/
	// FIXME: this should send a disconnect to the client!
}

qboolean
SV_FilterIP (byte *ip, double *until)
{
	int         i;

	*until = 0;

	for (i = 0; i < numipfilters; i++) {
		if (ipfilters[i].type != ft_ban)
			continue;
		if (SV_MaskIPCompare (ip, ipfilters[i].ip, ipfilters[i].mask)) {
			if (!ipfilters[i].time) {
				// normal ban
				return filterban->int_val;
			} else if (ipfilters[i].time > realtime) {
				*until = ipfilters[i].time;
				return true;	// banned no matter what
			} else {
				// time expired, set free
				SV_RemoveIPFilter (i);
				i--; // counter the increment, so we don't skip any
			}
		}
	}
	return !filterban->int_val;
}

void
SV_SavePenaltyFilter (client_t *cl, filtertype_t type, double pentime)
{
	int i;
	byte *b;
	if (pentime < realtime)   // no point
		return;

	b = cl->netchan.remote_address.ip;

	for (i = 0; i < numipfilters; i++)
		if (ipfilters[i].mask == 32 && SV_IPCompare (ipfilters[i].ip, b)
			&& ipfilters[i].type == type) {
			Con_Printf ("Penalty for user %d already exists\n", cl->userid);
			return;
		}

	if (numipfilters == MAX_IPFILTERS) {
		Con_Printf ("IP filter list is full\n");
		return;
	}

	Con_Printf ("Penalty saved for user %d\n", cl->userid);
	ipfilters[numipfilters].mask = 32;
	SV_IPCopy (ipfilters[numipfilters].ip, b);
	ipfilters[numipfilters].time = pentime;
	ipfilters[numipfilters].type = type;
	numipfilters++;
	return;
}

double
SV_RestorePenaltyFilter (client_t *cl, filtertype_t type)
{
	int         i;
	byte		*ip;

	ip = cl->netchan.remote_address.ip;

	// search for existing penalty filter of same type
	for (i = 0; i < numipfilters; i++) {
		if (type == ipfilters[i].type && SV_IPCompare (ip, ipfilters[i].ip)) {
			Con_Printf ("Penalty restored for user %d\n", cl->userid);
			return ipfilters[i].time;
		}
	}
	return 0.0;
}

void
SV_ReadPackets (void)
{
	client_t   *cl;
	int         qport, i;
	qboolean    good;
	double      until;

	good = false;
	while (NET_GetPacket ()) {
		if (SV_FilterIP (net_from.ip, &until)) {
			SV_SendBan (until);			// tell them we aren't listening...
			continue;
		}
		// check for connectionless packet (0xffffffff) first
		if (*(int *) net_message->message->data == -1) {
			SV_ConnectionlessPacket ();
			continue;
		}

		if (net_message->message->cursize < 11) {
			SV_Printf ("%s: Runt packet\n", NET_AdrToString (net_from));
			continue;
		}
		// read the qport out of the message so we can fix up
		// stupid address translating routers
		MSG_BeginReading (net_message);
		MSG_ReadLong (net_message);				// sequence number
		MSG_ReadLong (net_message);				// sequence number
		qport = MSG_ReadShort (net_message) & 0xffff;

		// check for packets from connected clients
		for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
			if (cl->state == cs_free)
				continue;
			if (!NET_CompareBaseAdr (net_from, cl->netchan.remote_address))
				continue;
			if (cl->netchan.qport != qport)
				continue;
			if (cl->netchan.remote_address.port != net_from.port) {
				Con_DPrintf ("SV_ReadPackets: fixing up a translated port\n");
				cl->netchan.remote_address.port = net_from.port;
			}
			if (Netchan_Process (&cl->netchan)) {	// this is a valid,
				// sequenced packet, so
				// process it
				svs.stats.packets++;
				good = true;
				cl->send_message = true;	// reply at end of frame
				if (cl->state != cs_zombie)
					SV_ExecuteClientMessage (cl);
			}
			break;
		}

		if (i != MAX_CLIENTS)
			continue;

		// packet is not from a known client
//		SV_Printf ("%s:sequenced packet without connection\n",
//				   NET_AdrToString(net_from));
	}
}

/*
  SV_CheckTimeouts

  If a packet has not been received from a client in timeout.value
  seconds, drop the conneciton.

  When a client is normally dropped, the client_t goes into a zombie
  state for a few seconds to make sure any final reliable message gets
  resent if necessary
*/
void
SV_CheckTimeouts (void)
{
	client_t   *cl;
	float       droptime;
	int         nclients, i;

	droptime = realtime - timeout->value;
	nclients = 0;

	for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
		if (cl->state == cs_connected || cl->state == cs_spawned) {
			if (!cl->spectator)
				nclients++;
			if (cl->netchan.last_received < droptime) {
				SV_BroadcastPrintf (PRINT_HIGH, "%s timed out\n", cl->name);
				SV_DropClient (cl);
				cl->state = cs_free;	// don't bother with zombie state
				svs.num_clients--;
			}
		}
		if (cl->state == cs_zombie &&
			realtime - cl->connection_started > zombietime->value) {
			cl->state = cs_free;		// can now be reused
			svs.num_clients--;
		}
	}
	if (sv.paused && !nclients) {
		// nobody left, unpause the server
		SV_TogglePause ("Pause released since no players are left.\n");
	}
}

/*
  SV_GetConsoleCommands

  Add them exactly as if they had been typed at the console
*/
void
SV_GetConsoleCommands (void)
{
	Con_ProcessInput ();
}

/*
	SV_CheckVars
*/
void
SV_CheckVars (void)
{
	static char const *pw, *spw;
	int         v;

	if (password->string == pw && spectator_password->string == spw)
		return;
	pw = password->string;
	spw = spectator_password->string;

	v = 0;
	if (pw && pw[0] && strcmp (pw, "none"))
		v |= 1;
	if (spw && spw[0] && strcmp (spw, "none"))
		v |= 2;

	SV_Printf ("Updated needpass.\n");
	if (!v)
		Info_SetValueForKey (svs.info, "needpass", "", MAX_SERVERINFO_STRING,
							 !sv_highchars->int_val);
	else
		Info_SetValueForKey (svs.info, "needpass", va ("%i", v),
							 MAX_SERVERINFO_STRING, !sv_highchars->int_val);
}

/*
	SV_GarbageCollect

	Run string GC on progs every pr_gc_interval frames
*/
void
SV_GarbageCollect ()
{
	if (pr_gc->int_val == 1
		|| (pr_gc->int_val == 2 && sv_pr_state.progs->version == PROG_VERSION)) {
		pr_gc_count++;
		if (pr_gc_count >= pr_gc_interval->int_val) {
			pr_gc_count = 0;
			PR_GarbageCollect (&sv_pr_state);
		}
	} else {
		// Make sure the count gets reset if the gc is disabled.  I
		// could use a callback, but I'm lazy
		pr_gc_count = 0;
	}
}

void
SV_Frame (float time)
{
	static double start, end;

	start = Sys_DoubleTime ();
	svs.stats.idle += start - end;

	// keep the random time dependent
	rand ();

	// decide the simulation time
	if (!sv.paused) {
		realtime += time;
		sv.time += time;
	}
	// check timeouts
	SV_CheckTimeouts ();

	// toggle the log buffer if full
	SV_CheckLog ();

	// clean out expired bans/cuffs/mutes
	SV_CleanIPList ();

	// move autonomous things around if enough time has passed
	if (!sv.paused)
		SV_Physics ();

	// get packets
	SV_ReadPackets ();

	// check for commands typed to the host
	SV_GetConsoleCommands ();

	// process console commands
	Cbuf_Execute ();

	SV_CheckVars ();

	// send messages back to the clients that had packets read this frame
	SV_SendClientMessages ();

	// send a heartbeat to the master if needed
	Master_Heartbeat ();

	SV_GarbageCollect ();

	// collect timing statistics
	end = Sys_DoubleTime ();
	svs.stats.active += end - start;
	if (++svs.stats.count == STATFRAMES) {
		svs.stats.latched_active = svs.stats.active;
		svs.stats.latched_idle = svs.stats.idle;
		svs.stats.latched_packets = svs.stats.packets;
		svs.stats.active = 0;
		svs.stats.idle = 0;
		svs.stats.packets = 0;
		svs.stats.count = 0;
	}
	Con_ProcessInput ();	//XXX evil hack to get the cursor in the right place
}

void
SV_InitLocal (void)
{
	int         i;
	extern cvar_t *sv_maxvelocity;
	extern cvar_t *sv_gravity;
	extern cvar_t *sv_aim;
	extern cvar_t *sv_stopspeed;
	extern cvar_t *sv_spectatormaxspeed;
	extern cvar_t *sv_accelerate;
	extern cvar_t *sv_airaccelerate;
	extern cvar_t *sv_wateraccelerate;
	extern cvar_t *sv_friction;
	extern cvar_t *sv_waterfriction;

	SV_UserInit ();

	rcon_password = Cvar_Get ("rcon_password", "", CVAR_NONE, NULL, "Set the "
							  "password for rcon 'root' commands");
	admin_password = Cvar_Get ("admin_password", "", CVAR_NONE, NULL, "Set "
							   "the password for rcon admin commands");
	password = Cvar_Get ("password", "", CVAR_NONE, NULL, "Set the server "
						 "password for players");
	spectator_password = Cvar_Get ("spectator_password", "", CVAR_NONE, NULL,
								   "Set the spectator password");
	sv_mintic = Cvar_Get ("sv_mintic", "0.03", CVAR_NONE, NULL, "The minimum "
						  "amount of time the server will wait before sending "
						  "packets to a client. Set to .5 to make modem users "
						  "happy");
	sv_maxtic = Cvar_Get ("sv_maxtic", "0.1", CVAR_NONE, NULL, "The maximum "
						  "amount of time in seconds before a client a "
						  "receives an update from the server");
	fraglimit = Cvar_Get ("fraglimit", "0", CVAR_SERVERINFO, Cvar_Info,
						  "Amount of frags a player must attain in order to "
						  "exit the level");
	timelimit = Cvar_Get ("timelimit", "0", CVAR_SERVERINFO, Cvar_Info,
						  "Sets the amount of time in minutes that is needed "
						  "before advancing to the next level");
	teamplay = Cvar_Get ("teamplay", "0", CVAR_SERVERINFO, Cvar_Info,
						 "Determines teamplay rules. 0 off, 1 You cannot hurt "
						 "yourself nor your teammates, 2 You can hurt "
						 "yourself, your teammates, and you will lose one "
						 "frag for killing a teammate, 3 You can hurt "
						 "yourself but you cannot hurt your teammates");
	samelevel = Cvar_Get ("samelevel", "0", CVAR_SERVERINFO, Cvar_Info,
						  "Determines the rules for level changing and "
						  "exiting. 0 Allows advancing to the next level,"
						  "1 The same level will be played until someone "
						  "exits, 2 The same level will be played and the "
						  "exit will kill anybody that tries to exit, 3 The "
						  "same level will be played and the exit will kill "
						  "anybody that tries to exit, except on the Start "
						  "map.");
	maxclients = Cvar_Get ("maxclients", "8", CVAR_SERVERINFO, Cvar_Info,
						   "Sets how many clients can connect to your "
						   "server, this includes spectators and players");
	maxspectators = Cvar_Get ("maxspectators", "8", CVAR_SERVERINFO, Cvar_Info,
							  "Sets how many spectators can connect to your "
							  "server. The maxclients value takes precedence "
							  "over this value so this value should always be "
							  "equal-to or less-then the maxclients value");
	hostname = Cvar_Get ("hostname", "unnamed", CVAR_SERVERINFO, Cvar_Info,
						 "Report or sets the server name");
	deathmatch = Cvar_Get ("deathmatch", "1", CVAR_SERVERINFO, Cvar_Info, 
						   "Sets the rules for weapon and item respawning. "
						   "1 Does not leave weapons on the map. You can "
						   "pickup weapons and items and they will respawn, "
						   "2 Leaves weapons on the map. You can only pick up "
						   "a weapon once. Picked up items will not respawn, "
						   "3 Leaves weapons on the map. You can only pick up "
						   "a weapon once. Picked up items will respawn.");
	spawn = Cvar_Get ("spawn", "0", CVAR_SERVERINFO, Cvar_Info,
					  "Spawn the player entity");
	watervis = Cvar_Get ("watervis", "0", CVAR_SERVERINFO, Cvar_Info,
						 "Toggle the use of r_watervis by OpenGL clients");
	timeout = Cvar_Get ("timeout", "65", CVAR_NONE, NULL, "Sets the amount of "
						"time in seconds before a client is considered "
						"disconnected if the server does not receive a "
						"packet");
	zombietime = Cvar_Get ("zombietime", "2", CVAR_NONE, NULL, "The number of "
						   "seconds that the server will keep the character "
						   "of a player on the map who seems to have "
						   "disconnected");
	sv_maxvelocity = Cvar_Get ("sv_maxvelocity", "2000", CVAR_NONE, NULL,
							   "Sets the maximum velocity an object can "
							   "travel");
	sv_extensions = Cvar_Get ("sv_extensions", "1", CVAR_NONE, NULL,
							  "Use protocol extensions for QuakeForge "
							  "clients");
	sv_gravity = Cvar_Get ("sv_gravity", "800", CVAR_NONE, NULL,
						   "Sets the global value for the amount of gravity");
	sv_stopspeed = Cvar_Get ("sv_stopspeed", "100", CVAR_NONE, NULL, 
							 "Sets the value that determines how fast the "
							 "player should come to a complete stop");
	sv_maxspeed = Cvar_Get ("sv_maxspeed", "320", CVAR_NONE, NULL,
							"Sets the maximum speed a player can move");
	sv_spectatormaxspeed = Cvar_Get ("sv_spectatormaxspeed", "500", CVAR_NONE,
									 NULL, "Sets the maximum speed a "
									 "spectator can move");
	sv_accelerate = Cvar_Get ("sv_accelerate", "10", CVAR_NONE, NULL,
							  "Sets the acceleration value for the players");
	sv_airaccelerate = Cvar_Get ("sv_airaccelerate", "0.7", CVAR_NONE, NULL,
								 "Sets how quickly the players accelerate in "
								 "air");
	sv_wateraccelerate = Cvar_Get ("sv_wateraccelerate", "10", CVAR_NONE, NULL,
								   "Sets the water acceleration value");
	sv_friction = Cvar_Get ("sv_friction", "4", CVAR_NONE, NULL,
							"Sets the friction value for the players");
	sv_waterfriction = Cvar_Get ("sv_waterfriction", "4", CVAR_NONE, NULL,
								 "Sets the water friction value");
	sv_aim = Cvar_Get ("sv_aim", "2", CVAR_NONE, NULL,
					   "Sets the value for auto-aiming leniency");
	sv_timekick = Cvar_Get ("sv_timekick", "3", CVAR_SERVERINFO, Cvar_Info,
							"Time cheat protection");
	sv_timekick_fuzz = Cvar_Get ("sv_timekick_fuzz", "15", CVAR_NONE, NULL,
								 "Time cheat \"fuzz factor\"");
	sv_timekick_interval = Cvar_Get ("sv_timekick_interval", "30", CVAR_NONE,
									 NULL, "Time cheat check interval");
	sv_minqfversion = Cvar_Get ("sv_minqfversion", "0", CVAR_SERVERINFO,
								Cvar_Info, "Minimum QF version on client");
	sv_maxrate = Cvar_Get ("sv_maxrate", "0", CVAR_SERVERINFO, Cvar_Info,
						   "Maximum allowable rate");
	sv_allow_log = Cvar_Get ("sv_allow_log", "1", CVAR_NONE, NULL,
							 "Allow remote logging");
	sv_allow_status = Cvar_Get ("sv_allow_status", "1", CVAR_NONE, NULL,
								"Allow remote status queries (qstat etc)");
	sv_allow_ping = Cvar_Get ("sv_allow_pings", "1", CVAR_NONE, NULL,
							  "Allow remote pings (qstat etc)");
	sv_netdosprotect = Cvar_Get ("sv_netdosprotect", "0", CVAR_NONE, NULL,
								 "DoS flood attack protection");
	sv_timestamps = Cvar_Get ("sv_timestamps", "0", CVAR_NONE, NULL,
							  "Time/date stamps in log entries");
	sv_timefmt = Cvar_Get ("sv_timefmt", "[%b %e %X] ", CVAR_NONE, NULL,
						   "Time/date format to use");
	filterban = Cvar_Get ("filterban", "1", CVAR_NONE, NULL, 
						  "Determines the rules for the IP list "
						  "0 Only IP addresses on the Ban list will be "
						  "allowed onto the server, 1 Only IP addresses NOT "
						  "on the Ban list will be allowed onto the server");
	sv_filter_automask = Cvar_Get ("sv_filter_automask", "1", CVAR_NONE, NULL,
									"Automatically determine the mask length "
									"when it is not explicitely given.  e.g. "
									"\"addip 1.2.0.0\" would be the same as "
									"\"addip 1.2.0.0/16\"");
	allow_download = Cvar_Get ("allow_download", "1", CVAR_NONE, NULL,
							   "Toggle if clients can download game data from "
							   "the server");
	allow_download_skins = Cvar_Get ("allow_download_skins", "1", CVAR_NONE,
									 NULL, "Toggle if clients can download "
									 "skins from the server");
	allow_download_models = Cvar_Get ("allow_download_models", "1", CVAR_NONE,
									  NULL, "Toggle if clients can download "
									  "models from the server");
	allow_download_sounds = Cvar_Get ("allow_download_sounds", "1", CVAR_NONE,
									  NULL, "Toggle if clients can download "
									  "sounds from the server");
	allow_download_maps = Cvar_Get ("allow_download_maps", "1", CVAR_NONE,
									NULL, "Toggle if clients can download "
									"maps from the server");
	sv_highchars = Cvar_Get ("sv_highchars", "1", CVAR_NONE, NULL,
							 "Toggle the use of high character color names "
							 "for players");
	sv_phs = Cvar_Get ("sv_phs", "1", CVAR_NONE, NULL, "Possibly Hearable "
					   "Set. If set to zero, the server calculates sound "
					   "hearability in realtime");
 	pausable = Cvar_Get ("pausable", "1", CVAR_NONE, NULL,
						 "Toggle if server can be paused 1 is on, 0 is off");
	pr_gc = Cvar_Get ("pr_gc", "2", CVAR_NONE, NULL, "Enable/disable the "
					  "garbage collector.  0 is off, 1 is on, 2 is auto (on "
					  "for newer qfcc progs, off otherwise)");
	pr_gc_interval = Cvar_Get ("pr_gc_interval", "50", CVAR_NONE, NULL,
							   "Number of frames to wait before running "
							   "string garbage collector.");
	// DoS protection
	Cmd_AddCommand ("netdosexpire", SV_netDoSexpire_f, "FIXME: part of DoS "
					"protection obviously, but I don't know what it does. No "
					"Description");
	Cmd_AddCommand ("netdosvalues", SV_netDoSvalues_f, "FIXME: part of DoS "
					"protection obviously, but I don't know what it does. No "
					"Description");
	Cmd_AddCommand ("addip", SV_AddIP_f, "Add a single IP or a domain of IPs "
					"to the IP list of the server.\n"
					"Useful for banning people. (addip (ipnumber))");
	Cmd_AddCommand ("removeip", SV_RemoveIP_f, "Remove an IP address from the "
					"server IP list. (removeip (ipnumber))");
	Cmd_AddCommand ("listip", SV_ListIP_f, "Print out the current list of IPs "
					"on the server list.");
	Cmd_AddCommand ("writeip", SV_WriteIP_f, "Record all IP addresses on the "
					"server IP list. The file name is listip.cfg");

	for (i = 0; i < MAX_MODELS; i++)
		snprintf (localmodels[i], sizeof (localmodels[i]), "*%i", i);

	Info_SetValueForStarKey (svs.info, "*version", QW_VERSION,
							 MAX_SERVERINFO_STRING, !sv_highchars->int_val);

	// Brand server as QF, with appropriate QSG standards version  --KB
	Info_SetValueForStarKey (svs.info, "*qf_version", VERSION,
							 MAX_SERVERINFO_STRING, !sv_highchars->int_val);
	Info_SetValueForStarKey (svs.info, "*qsg_version", QW_QSG_VERSION,
							 MAX_SERVERINFO_STRING, !sv_highchars->int_val);

	CF_Init ();

	// init fraglog stuff
	svs.logsequence = 1;
	svs.logtime = realtime;
	svs.log[0].data = svs.log_buf[0];
	svs.log[0].maxsize = sizeof (svs.log_buf[0]);
	svs.log[0].cursize = 0;
	svs.log[0].allowoverflow = true;
	svs.log[1].data = svs.log_buf[1];
	svs.log[1].maxsize = sizeof (svs.log_buf[1]);
	svs.log[1].cursize = 0;
	svs.log[1].allowoverflow = true;
}

/*
	Master_Heartbeat

	Send a message to the master every few minutes to
	let it know we are alive, and log information
*/
#define	HEARTBEAT_SECONDS	300
void
Master_Heartbeat (void)
{
	char        string[2048];
	int         active, i;

	if (realtime - svs.last_heartbeat < HEARTBEAT_SECONDS)
		return;							// not time to send yet

	svs.last_heartbeat = realtime;

	// count active users
	active = 0;
	for (i = 0; i < MAX_CLIENTS; i++)
		if (svs.clients[i].state == cs_connected ||
			svs.clients[i].state == cs_spawned) active++;

	svs.heartbeat_sequence++;
	snprintf (string, sizeof (string), "%c\n%i\n%i\n", S2M_HEARTBEAT,
			  svs.heartbeat_sequence, active);


	// send to group master
	for (i = 0; i < MAX_MASTERS; i++)
		if (master_adr[i].port) {
			SV_Printf ("Sending heartbeat to %s\n",
						NET_AdrToString (master_adr[i]));
			NET_SendPacket (strlen (string), string, master_adr[i]);
		}
}

/*
	Master_Shutdown

	Informs all masters that this server is going down
*/
void
Master_Shutdown (void)
{
	char        string[2048];
	int         i;

	snprintf (string, sizeof (string), "%c\n", S2M_SHUTDOWN);

	// send to group master
	for (i = 0; i < MAX_MASTERS; i++)
		if (master_adr[i].port) {
			SV_Printf ("Sending heartbeat to %s\n",
						NET_AdrToString (master_adr[i]));
			NET_SendPacket (strlen (string), string, master_adr[i]);
		}
}

/*
	SV_ExtractFromUserinfo

	Pull specific info from a newly changed userinfo string
	into a more C freindly form.
*/
void
SV_ExtractFromUserinfo (client_t *cl)
{
	const char *val;
	char       *q, *p;
	char        newname[80];
	client_t   *client;
	int         i;
	int         dupc = 1;


	// name for C code
	val = Info_ValueForKey (cl->userinfo, "name");

	// trim user name
	strncpy (newname, val, sizeof (newname) - 1);
	newname[sizeof (newname) - 1] = 0;

	for (p = newname; (*p == ' ' || *p == '\r' || *p == '\n') && *p; p++);

	if (p != newname && !*p) {
		// white space only
		strcpy (newname, "unnamed");
		p = newname;
	}

	if (p != newname && *p) {
		for (q = newname; *p; *q++ = *p++);
		*q = 0;
	}
	for (p = newname + strlen (newname) - 1;
		 p != newname && (*p == ' ' || *p == '\r' || *p == '\n'); p--);
	p[1] = 0;

	if (strcmp (val, newname)) {
		Info_SetValueForKey (cl->userinfo, "name", newname, MAX_INFO_STRING,
							 !sv_highchars->int_val);
		val = Info_ValueForKey (cl->userinfo, "name");
	}

	if (!val[0] || strcaseequal (val, "console")) {
		Info_SetValueForKey (cl->userinfo, "name", "unnamed", MAX_INFO_STRING,
							 !sv_highchars->int_val);
		val = Info_ValueForKey (cl->userinfo, "name");
	}
	// check to see if another user by the same name exists
	while (1) {
		for (i = 0, client = svs.clients; i < MAX_CLIENTS; i++, client++) {
			if (client->state != cs_spawned || client == cl)
				continue;
			if (strcaseequal (client->name, val))
				break;
		}
		if (i != MAX_CLIENTS) {			// dup name
			if (val[0] == '(') {
				if (val[2] == ')')
					val += 3;
				else if (val[3] == ')')
					val += 4;
			}

			snprintf (newname, sizeof (newname), "(%d)%-.40s", dupc++, val);
			Info_SetValueForKey (cl->userinfo, "name", newname,
								 MAX_INFO_STRING, !sv_highchars->int_val);
			val = Info_ValueForKey (cl->userinfo, "name");

			// If the new name was not set (due to the info string being too
			// long), drop the client to prevent an infinite loop.
			if(strcmp(val, newname)) {
				Netchan_OutOfBandPrint (net_from, "%c\nPlease choose a "
										"different name.\n", A2C_PRINT);
				SV_ClientPrintf (cl, PRINT_HIGH, "Please choose a different "
								 "name.\n");
				SV_Printf("Client %d kicked for invalid name\n", cl->userid);
				SV_DropClient (cl);
				return;
			}

		} else
			break;
	}

	if (strncmp (val, cl->name, strlen (cl->name))) {
		if (!sv.paused) {
			if (!cl->lastnametime || realtime - cl->lastnametime > 5) {
				cl->lastnamecount = 0;
				cl->lastnametime = realtime;
			} else if (cl->lastnamecount++ > 4) {
				SV_BroadcastPrintf (PRINT_HIGH, "%s was kicked for name "
									"spam\n", cl->name);
				SV_ClientPrintf (cl, PRINT_HIGH, "You were kicked from the "
								 "game for name spamming\n");
				SV_DropClient (cl);
				return;
			}
		}

		if (cl->state >= cs_spawned && !cl->spectator)
			SV_BroadcastPrintf (PRINT_HIGH, "%s changed name to %s\n",
								cl->name, val);
	}


	strncpy (cl->name, val, sizeof (cl->name) - 1);

	// rate command
	val = Info_ValueForKey (cl->userinfo, "rate");
	if (strlen (val)) {
		i = atoi (val);

		if ((sv_maxrate->int_val) && (i > sv_maxrate->int_val)) {
			i = bound (500, i, sv_maxrate->int_val);
		} else {
			i = bound (500, i, 10000);
		}
		cl->netchan.rate = 1.0 / i;
	}
	// msg command
	val = Info_ValueForKey (cl->userinfo, "msg");
	if (strlen (val)) {
		cl->messagelevel = atoi (val);
	}

	cl->stdver = atof (Info_ValueForKey (cl->userinfo, "*qsg_version"));
}

void
SV_InitNet (void)
{
	int         port, p;

	SV_GenerateIPMasks ();

	port = PORT_SERVER;
	p = COM_CheckParm ("-port");
	if (p && p < com_argc) {
		port = atoi (com_argv[p + 1]);
		SV_Printf ("Port: %i\n", port);
	}
	NET_Init (port);

	Netchan_Init ();

	Net_Log_Init(sv.sound_precache);

	// heartbeats will always be sent to the id master
	svs.last_heartbeat = -99999;		// send immediately
//  NET_StringToAdr ("192.246.40.70:27000", &idmaster_adr);
}

void
SV_Init (void)
{
	COM_InitArgv (host_parms.argc, (const char**)host_parms.argv);
//	COM_AddParm ("-game");
//	COM_AddParm ("qw");

	if (COM_CheckParm ("-minmemory"))
		host_parms.memsize = MINIMUM_MEMORY;

	if (host_parms.memsize < MINIMUM_MEMORY)
		SV_Error ("Only %4.1f megs of memory reported, can't execute game",
				  host_parms.memsize / (float) 0x100000);

	Cvar_Init_Hash ();
	Cmd_Init_Hash ();
	Memory_Init (host_parms.membase, host_parms.memsize);
	Cvar_Init ();
	Sys_Init_Cvars ();
	Sys_Init ();

	Cbuf_Init ();
	Cmd_Init ();
	SV_InitOperatorCommands ();

	// execute +set as early as possible
	Cmd_StuffCmds_f ();
	Cbuf_Execute_Sets ();

	// execute the global configuration file if it exists
	// would have been nice if Cmd_Exec_f could have been used, but it
	// only reads from within the quake file system, and changing that is
	// probably Not A Good Thing (tm).
	fs_globalcfg = Cvar_Get ("fs_globalcfg", FS_GLOBALCFG,
							 CVAR_ROM, 0, "global configuration file");
	Cmd_Exec_File (fs_globalcfg->string);
	Cbuf_Execute_Sets ();

	// execute +set again to override the config file
	Cmd_StuffCmds_f ();
	Cbuf_Execute_Sets ();

	fs_usercfg = Cvar_Get ("fs_usercfg", FS_USERCFG,
						   CVAR_ROM, 0, "user configuration file");
	Cmd_Exec_File (fs_usercfg->string);
	Cbuf_Execute_Sets ();

	// execute +set again to override the config file
	Cmd_StuffCmds_f ();
	Cbuf_Execute_Sets ();

	PI_Init ();

	sv_console_plugin = Cvar_Get ("sv_console_plugin", "server",
								  CVAR_ROM, 0, "Plugin used for the console");
	Con_Init (sv_console_plugin->string);
	Sys_SetPrintf (SV_Print);

	COM_Filesystem_Init_Cvars ();
	Game_Init_Cvars ();
	COM_Init_Cvars ();
	Mod_Init_Cvars ();
	Netchan_Init_Cvars ();
	Pmove_Init_Cvars ();
	SV_Progs_Init_Cvars ();
	PR_Init_Cvars ();

	// and now reprocess the cmdline's sets for overrides
	Cmd_StuffCmds_f ();
	Cbuf_Execute_Sets ();

	COM_Filesystem_Init ();
	Game_Init ();
	COM_Init ();

	PR_Init ();
	SV_Progs_Init ();
	Mod_Init ();

	SV_InitNet ();

	SV_InitLocal ();
	Pmove_Init ();

	Hunk_AllocName (0, "-HOST_HUNKLEVEL-");
	host_hunklevel = Hunk_LowMark ();

	Cbuf_InsertText ("exec server.cfg\n");

	host_initialized = true;

//	SV_Printf ("Exe: "__TIME__" "__DATE__"\n");
	SV_Printf ("%4.1f megabyte heap\n", host_parms.memsize / (1024 * 1024.0));

	SV_Printf ("\n");
	SV_Printf ("%s server, Version %s (build %04d)\n", PROGRAM, VERSION,
				build_number ());
	SV_Printf ("\n");

	SV_Printf ("<==> %s initialized <==>\n", PROGRAM);

	// process command line arguments
	Cmd_Exec_File (fs_usercfg->string);
	Cmd_StuffCmds_f ();
	Cbuf_Execute ();

	// if a map wasn't specified on the command line, spawn start.map
	if (sv.state == ss_dead)
		Cmd_ExecuteString ("map start", src_command);
	if (sv.state == ss_dead)
		SV_Error ("Could not initialize server");
}

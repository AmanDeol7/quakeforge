/*
	sv_send.c

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

#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#include "QF/console.h"
#include "QF/cvar.h"
#include "QF/msg.h"
#include "QF/sound.h" // FIXME: DEFAULT_SOUND_PACKET_*
#include "QF/sys.h"

#include "bothdefs.h"
#include "compat.h"
#include "server.h"
#include "sv_demo.h"
#include "sv_progs.h"

#define CHAN_AUTO   0
#define CHAN_WEAPON 1
#define CHAN_VOICE  2
#define CHAN_ITEM   3
#define CHAN_BODY   4

/* SV_Printf redirection */

char        outputbuf[8000];
int         con_printf_no_log;
redirect_t  sv_redirected;


static void
SV_FlushRedirect (void)
{
	char        send[8000 + 6];

	if (sv_redirected == RD_PACKET) {
		send[0] = 0xff;
		send[1] = 0xff;
		send[2] = 0xff;
		send[3] = 0xff;
		send[4] = A2C_PRINT;
		memcpy (send + 5, outputbuf, strlen (outputbuf) + 1);

		Netchan_SendPacket (strlen (send) + 1, send, net_from);
	} else if (sv_redirected == RD_CLIENT) {
		ClientReliableWrite_Begin (host_client, svc_print,
								   strlen (outputbuf) + 3);
		ClientReliableWrite_Byte (host_client, PRINT_HIGH);
		ClientReliableWrite_String (host_client, outputbuf);
	}
	// clear it
	outputbuf[0] = 0;
}

/*
	SV_BeginRedirect

	Send SV_Printf data to the remote client
	instead of the console
*/
void
SV_BeginRedirect (redirect_t rd)
{
	sv_redirected = rd;
	outputbuf[0] = 0;
}

void
SV_EndRedirect (void)
{
	SV_FlushRedirect ();
	sv_redirected = RD_NONE;
}

#define	MAXPRINTMSG	4096

/*
	SV_Printf

	Handles cursor positioning, line wrapping, etc
*/
// FIXME: the msg variables need to be renamed/cleaned up
void
SV_Print (const char *fmt, va_list args)
{
	static int  pending = 0;			// partial line being printed
	char		premsg[MAXPRINTMSG];
	unsigned char msg[MAXPRINTMSG];
	char        msg2[MAXPRINTMSG];
	char        msg3[MAXPRINTMSG];

	time_t      mytime = 0;
	struct tm  *local = NULL;
	qboolean    timestamps = false;

	unsigned char *in, *out;

	vsnprintf (premsg, sizeof (premsg), fmt, args);
	in = premsg;
	out = msg;

	// expand FFnickFF to nick <userid>
	do {
		switch (*in) {
			case 0xFF: {
				char *end = strchr (in + 1, 0xFF);
				int userid = 0;
				int len;
				int i;

				if (!end)
					end = in + strlen (in);
				*end = '\0';
				for (i = 0; i < MAX_CLIENTS; i++) {
					if (!svs.clients[i].state)
						continue;
					if (!strcmp (svs.clients[i].name, in + 1)) {
						userid = svs.clients[i].userid;
						break;
					}
				}
				len = snprintf (out, sizeof (msg) - (out - msg),
								"%s <%d>", in + 1, userid);
				out += len;
				in = end + 1;
				break;
			}
			default:
				*out++ = *in++;
		}
	} while (sizeof (msg) - (out - msg) > 0 && *in);
	*out = '\0';

	if (sv_redirected) {				// Add to redirected message
		if (strlen (msg) + strlen (outputbuf) > sizeof (outputbuf) - 1)
			SV_FlushRedirect ();
		strncat (outputbuf, msg, sizeof (outputbuf) - strlen (outputbuf));
	}
	if (!con_printf_no_log) {
		// We want to output to console and maybe logfile
		if (sv_timestamps && sv_timefmt && sv_timefmt->string
			&& sv_timestamps->int_val && !pending)
			timestamps = true;

		if (timestamps) {
			mytime = time (NULL);
			local = localtime (&mytime);
			strftime (msg3, sizeof (msg3), sv_timefmt->string, local);

			snprintf (msg2, sizeof (msg2), "%s%s", msg3, msg);
		} else {
			snprintf (msg2, sizeof (msg2), "%s", msg);
		}
		if (msg2[strlen (msg2) - 1] != '\n') {
			pending = 1;
		} else {
			pending = 0;
		}

		Con_Printf ("%s", msg2);		// also echo to debugging console
	}
}

void
SV_Printf (const char *fmt, ...)
{
	va_list     argptr;

	va_start (argptr, fmt);
	SV_Print (fmt, argptr);
	va_end (argptr);
}

/* EVENT MESSAGES */

static void
SV_PrintToClient (client_t *cl, int level, const char *string)
{
	static unsigned char *buffer;
	const unsigned char *a;
	unsigned char *b;
	int size;
	static int buffer_size;

	size = strlen (string) + 1;
	if (size > buffer_size) {
		buffer_size = (size + 1023) & ~1023; // 1k multiples
		if (buffer)
			free (buffer);
		buffer = malloc (buffer_size);
		if (!buffer)
			Sys_Error ("SV_PrintToClient: could not allocate %d bytes",
					   buffer_size);
	}

	a = string;
	b = buffer;
	// strip 0xFFs
	while ((*b = *a++))
		if (*b != 0xFF)
			b++;

	ClientReliableWrite_Begin (cl, svc_print, strlen (buffer) + 3);
	ClientReliableWrite_Byte (cl, level);
	ClientReliableWrite_String (cl, buffer);
}

/*
	SV_ClientPrintf

	Sends text across to be displayed if the level passes
*/
void
SV_ClientPrintf (int recorder, client_t *cl, int level, const char *fmt, ...)
{
	char        string[1024];
	va_list     argptr;

	if (level < cl->messagelevel)
		return;

	va_start (argptr, fmt);
	vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	if (recorder && sv.demorecording) {
		DemoWrite_Begin (dem_single, cl - svs.clients, strlen (string) + 3);
		MSG_WriteByte (&demo.dbuf->sz, svc_print);
		MSG_WriteByte (&demo.dbuf->sz, level);
		MSG_WriteString (&demo.dbuf->sz, string);
	}

	SV_PrintToClient (cl, level, string);
}

/*
	SV_BroadcastPrintf

	Sends text to all active clients
*/
void
SV_BroadcastPrintf (int level, const char *fmt, ...)
{
	char        string[1024];
	client_t   *cl;
	int         i;
	va_list     argptr;

	va_start (argptr, fmt);
	vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	SV_Printf ("%s", string);			// print to the console

	for (i = 0, cl = svs.clients; i < MAX_CLIENTS; i++, cl++) {
		if (level < cl->messagelevel)
			continue;
		if (cl->state < cs_zombie)
			continue;

		SV_PrintToClient (cl, level, string);
	}

	if (sv.demorecording) {
		DemoWrite_Begin (dem_all, cl - svs.clients, strlen (string) + 3);
		MSG_WriteByte (&demo.dbuf->sz, svc_print);
		MSG_WriteByte (&demo.dbuf->sz, level);
		MSG_WriteString (&demo.dbuf->sz, string);
	}
}

/*
	SV_BroadcastCommand

	Sends text to all active clients
*/
void
SV_BroadcastCommand (const char *fmt, ...)
{
	char        string[1024];
	va_list     argptr;

	if (!sv.state)
		return;
	va_start (argptr, fmt);
	vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&sv.reliable_datagram, svc_stufftext);
	MSG_WriteString (&sv.reliable_datagram, string);
}

/*
	SV_Multicast

	Sends the contents of sv.multicast to a subset of the clients,
	then clears sv.multicast.

	MULTICAST_ALL	same as broadcast
	MULTICAST_PVS	send to clients potentially visible from org
	MULTICAST_PHS	send to clients potentially hearable from org
*/
void
SV_Multicast (const vec3_t origin, int to)
{
	byte       *mask;
	client_t   *client;
	int         leafnum, j;
	mleaf_t    *leaf;
	qboolean    reliable;

	leaf = Mod_PointInLeaf (origin, sv.worldmodel);
	if (!leaf)
		leafnum = 0;
	else
		leafnum = leaf - sv.worldmodel->leafs;

	reliable = false;

	switch (to) {
	case MULTICAST_ALL_R:
		reliable = true;			// intentional fallthrough
	case MULTICAST_ALL:
		mask = sv.pvs;				// leaf 0 is everything;
		break;

	case MULTICAST_PHS_R:
		reliable = true;			// intentional fallthrough
	case MULTICAST_PHS:
		mask = sv.phs + leafnum * 4 * ((sv.worldmodel->numleafs + 31) >> 5);
		break;

	case MULTICAST_PVS_R:
		reliable = true;			// intentional fallthrough
	case MULTICAST_PVS:
		mask = sv.pvs + leafnum * 4 * ((sv.worldmodel->numleafs + 31) >> 5);
		break;

	default:
		mask = NULL;
		Sys_Error ("SV_Multicast: bad to:%i", to);
	}

	// send the data to all relevent clients
	for (j = 0, client = svs.clients; j < MAX_CLIENTS; j++, client++) {
		if (client->state != cs_spawned)
			continue;

		if (to == MULTICAST_PHS_R || to == MULTICAST_PHS) {
			vec3_t      delta;

			VectorSubtract (origin, SVvector (client->edict, origin), delta);
			if (VectorLength (delta) <= 1024)
				goto inrange;
		}

		leaf = Mod_PointInLeaf (SVvector (client->edict, origin),
								sv.worldmodel);
		if (leaf) {
			// -1 is because pvs rows are 1 based, not 0 based like leafs
			leafnum = leaf - sv.worldmodel->leafs - 1;
			if (!(mask[leafnum >> 3] & (1 << (leafnum & 7)))) {
//				SV_Printf ("supressed multicast\n");
				continue;
			}
		}

	  inrange:
		if (reliable) {
			ClientReliableCheckBlock (client, sv.multicast.cursize);
			ClientReliableWrite_SZ (client, sv.multicast.data,
									sv.multicast.cursize);
		} else
			SZ_Write (&client->datagram, sv.multicast.data,
					  sv.multicast.cursize);
	}

	if (sv.demorecording) {
		if (reliable) {
//			DemoWrite_Begin (dem_multiple, cls, sv.multicast.cursize);
			DemoWrite_Begin (dem_all, 0, sv.multicast.cursize);
			SZ_Write (&demo.dbuf->sz, sv.multicast.data, sv.multicast.cursize);
		} else
			SZ_Write (&demo.datagram, sv.multicast.data, sv.multicast.cursize);
	}

	SZ_Clear (&sv.multicast);
}

/*  
	SV_StartSound

	Each entity can have eight independant sound sources, like voice,
	weapon, feet, etc.

	Channel 0 is an auto-allocate channel, the others override anything
	already running on that entity/channel pair.

	An attenuation of 0 will play full volume everywhere in the level.
	Larger attenuations will drop off.  (max 4 attenuation)
*/
void
SV_StartSound (edict_t *entity, int channel, const char *sample, int volume,
			   float attenuation)
{
	int         ent, field_mask, sound_num, i;
	qboolean    use_phs;
	qboolean    reliable = false;
	vec3_t      origin;

	if (volume < 0 || volume > 255)
		Sys_Error ("SV_StartSound: volume = %i", volume);

	if (attenuation < 0 || attenuation > 4)
		Sys_Error ("SV_StartSound: attenuation = %f", attenuation);

	if (channel < 0 || channel > 15)
		Sys_Error ("SV_StartSound: channel = %i", channel);

	// find precache number for sound
	for (sound_num = 1; sound_num < MAX_SOUNDS
		 && sv.sound_precache[sound_num]; sound_num++)
		if (!strcmp (sample, sv.sound_precache[sound_num]))
			break;

	if (sound_num == MAX_SOUNDS || !sv.sound_precache[sound_num]) {
		SV_Printf ("SV_StartSound: %s not precacheed\n", sample);
		return;
	}

	ent = NUM_FOR_EDICT (&sv_pr_state, entity);

	if ((channel & 8) || !sv_phs->int_val)	// no PHS flag
	{
		if (channel & 8)
			reliable = true;			// sounds that break the phs are
										// reliable
		use_phs = false;
		channel &= 7;
	} else
		use_phs = true;

//	if (channel == CHAN_BODY || channel == CHAN_VOICE)
//		reliable = true;

	channel = (ent << 3) | channel;

	field_mask = 0;
	if (volume != DEFAULT_SOUND_PACKET_VOLUME)
		channel |= SND_VOLUME;
	if (attenuation != DEFAULT_SOUND_PACKET_ATTENUATION)
		channel |= SND_ATTENUATION;

	// use the entity origin unless it is a bmodel
	if (SVfloat (entity, solid) == SOLID_BSP) {
		for (i = 0; i < 3; i++)
			origin[i] = SVvector (entity, origin)[i] + 0.5 *
				(SVvector (entity, mins)[i] + SVvector (entity, maxs)[i]);
	} else {
		VectorCopy (SVvector (entity, origin), origin);
	}

	MSG_WriteByte (&sv.multicast, svc_sound);
	MSG_WriteShort (&sv.multicast, channel);
	if (channel & SND_VOLUME)
		MSG_WriteByte (&sv.multicast, volume);
	if (channel & SND_ATTENUATION)
		MSG_WriteByte (&sv.multicast, attenuation * 64);
	MSG_WriteByte (&sv.multicast, sound_num);
	MSG_WriteCoordV (&sv.multicast, origin);

	if (use_phs)
		SV_Multicast (origin, reliable ? MULTICAST_PHS_R : MULTICAST_PHS);
	else
		SV_Multicast (origin, reliable ? MULTICAST_ALL_R : MULTICAST_ALL);
}

/* FRAME UPDATES */

int         sv_nailmodel, sv_supernailmodel, sv_playermodel;

void
SV_FindModelNumbers (void)
{
	int         i;

	sv_nailmodel = -1;
	sv_supernailmodel = -1;
	sv_playermodel = -1;

	for (i = 0; i < MAX_MODELS; i++) {
		if (!sv.model_precache[i])
			break;
		if (!strcmp (sv.model_precache[i], "progs/spike.mdl"))
			sv_nailmodel = i;
		if (!strcmp (sv.model_precache[i], "progs/s_spike.mdl"))
			sv_supernailmodel = i;
		if (!strcmp (sv.model_precache[i], "progs/player.mdl"))
			sv_playermodel = i;
	}
}

void
SV_WriteClientdataToMessage (client_t *client, sizebuf_t *msg)
{
	edict_t    *ent, *other;
	int         i, clnum;

	ent = client->edict;

	clnum = NUM_FOR_EDICT (&sv_pr_state, ent) - 1;

	// send the chokecount for r_netgraph
	if (client->chokecount) {
		MSG_WriteByte (msg, svc_chokecount);
		MSG_WriteByte (msg, client->chokecount);
		client->chokecount = 0;
	}
	// send a damage message if the player got hit this frame
	if (SVfloat (ent, dmg_take) || SVfloat (ent, dmg_save)) {
		other = PROG_TO_EDICT (&sv_pr_state, SVentity (ent, dmg_inflictor));
		MSG_WriteByte (msg, svc_damage);
		MSG_WriteByte (msg, SVfloat (ent, dmg_save));
		MSG_WriteByte (msg, SVfloat (ent, dmg_take));
		for (i = 0; i < 3; i++)
			MSG_WriteCoord (msg, SVvector (other, origin)[i] + 0.5 *
							(SVvector (other, mins)[i] +
							 SVvector (other, maxs)[i]));
		SVfloat (ent, dmg_take) = 0;
		SVfloat (ent, dmg_save) = 0;
	}

	// add this to server demo
	if (sv.demorecording && msg->cursize) {
		DemoWrite_Begin (dem_single, clnum, msg->cursize);
		SZ_Write (&demo.dbuf->sz, msg->data, msg->cursize);
	}

	// a fixangle might get lost in a dropped packet.  Oh well.
	if (SVfloat (ent, fixangle)) {
		MSG_WriteByte (msg, svc_setangle);
		MSG_WriteAngleV (msg, SVvector (ent, angles));
		VectorCopy (SVvector (ent, angles), demo.angles[clnum]);
		SVfloat (ent, fixangle) = 0;
		demo.fixangle[clnum] = true;

		if (sv.demorecording) {
			MSG_WriteByte (&demo.datagram, svc_setangle);
			MSG_WriteByte (&demo.datagram, clnum);
			MSG_WriteAngleV (&demo.datagram, demo.angles[clnum]);
		}
	}
}

static void
get_stats (edict_t *ent, int spectator, int stats[MAX_CL_STATS])
{
	memset (stats, 0, sizeof (int) * MAX_CL_STATS);

	stats[STAT_HEALTH] = SVfloat (ent, health);
	stats[STAT_WEAPON] = SV_ModelIndex (PR_GetString (&sv_pr_state,
										  SVstring (ent, weaponmodel)));
	stats[STAT_AMMO] = SVfloat (ent, currentammo);
	stats[STAT_ARMOR] = SVfloat (ent, armorvalue);
	stats[STAT_SHELLS] = SVfloat (ent, ammo_shells);
	stats[STAT_NAILS] = SVfloat (ent, ammo_nails);
	stats[STAT_ROCKETS] = SVfloat (ent, ammo_rockets);
	stats[STAT_CELLS] = SVfloat (ent, ammo_cells);
	if (!spectator)
		stats[STAT_ACTIVEWEAPON] = SVfloat (ent, weapon);
	// stuff the sigil bits into the high bits of items for sbar
	stats[STAT_ITEMS] = ((int) SVfloat (ent, items)
						 | ((int) *sv_globals.serverflags << 28));

	// Extensions to the QW 2.40 protocol for Mega2k  --KB
	stats[STAT_VIEWHEIGHT] = (int) SVvector (ent, view_ofs)[2];

	// FIXME: this should become a * key!  --KB
	if (SVfloat (ent, movetype) == MOVETYPE_FLY
		&& !atoi (Info_ValueForKey (svs.info, "playerfly")))
		SVfloat (ent, movetype) = MOVETYPE_WALK;

	stats[STAT_FLYMODE] = (SVfloat (ent, movetype) == MOVETYPE_FLY);
}

/*
	SV_UpdateClientStats

	Performs a delta update of the stats array.  This should only be performed
	when a reliable message can be delivered this frame.
*/
static void
SV_UpdateClientStats (client_t *client)
{
	edict_t    *ent;
	int         i;
	int         stats[MAX_CL_STATS];

	ent = client->edict;

	// if we are a spectator and we are tracking a player, we get his stats
	// so our status bar reflects his
	if (client->spectator && client->spec_track > 0)
		ent = svs.clients[client->spec_track - 1].edict;

	get_stats (ent, client->spectator, stats);

	for (i = 0; i < MAX_CL_STATS; i++)
		if (stats[i] != client->stats[i]) {
			client->stats[i] = stats[i];
			if (stats[i] >= 0 && stats[i] <= 255) {
				ClientReliableWrite_Begin (client, svc_updatestat, 3);
				ClientReliableWrite_Byte (client, i);
				ClientReliableWrite_Byte (client, stats[i]);
			} else {
				ClientReliableWrite_Begin (client, svc_updatestatlong, 6);
				ClientReliableWrite_Byte (client, i);
				ClientReliableWrite_Long (client, stats[i]);
			}
		}
}

static qboolean
SV_SendClientDatagram (client_t *client)
{
	byte        buf[MAX_DATAGRAM];
	sizebuf_t   msg;

	msg.data = buf;
	msg.maxsize = sizeof (buf);
	msg.cursize = 0;
	msg.allowoverflow = true;
	msg.overflowed = false;

	// add the client specific data to the datagram
	SV_WriteClientdataToMessage (client, &msg);

	// send over all the objects that are in the PVS
	// this will include clients, a packetentities, and
	// possibly a nails update
	SV_WriteEntitiesToClient (client, &msg, false);

	// copy the accumulated multicast datagram
	// for this client out to the message
	if (client->datagram.overflowed)
		SV_Printf ("WARNING: datagram overflowed for %s\n", client->name);
	else
		SZ_Write (&msg, client->datagram.data, client->datagram.cursize);
	SZ_Clear (&client->datagram);

	// send deltas over reliable stream
	if (Netchan_CanReliable (&client->netchan))
		SV_UpdateClientStats (client);

	if (msg.overflowed) {
		SV_Printf ("WARNING: msg overflowed for %s\n", client->name);
		SZ_Clear (&msg);
	}
	// send the datagram
	Netchan_Transmit (&client->netchan, msg.cursize, buf);

	return true;
}

static void
SV_UpdateToReliableMessages (void)
{
	client_t   *client;
	edict_t    *ent;
	int         i, j;

	// check for changes to be sent over the reliable streams to all clients
	for (i = 0, host_client = svs.clients; i < MAX_CLIENTS; i++, host_client++) {
		if (host_client->state != cs_spawned && host_client->state != cs_server)
			continue;
		if (host_client->sendinfo) {
			host_client->sendinfo = false;
			SV_FullClientUpdate (host_client, &sv.reliable_datagram);
		}
		if (host_client->old_frags != (int) SVfloat (host_client->edict, frags)) {
			for (j = 0, client = svs.clients; j < MAX_CLIENTS; j++, client++) {
				if (client->state < cs_connected)
					continue;
				ClientReliableWrite_Begin (client, svc_updatefrags, 4);
				ClientReliableWrite_Byte (client, i);
				ClientReliableWrite_Short (client, SVfloat (host_client->edict,
															frags));
			}

			if (sv.demorecording) {
				DemoWrite_Begin (dem_all, 0, 4);
				MSG_WriteByte (&demo.dbuf->sz, svc_updatefrags);
				MSG_WriteByte (&demo.dbuf->sz, i);
				MSG_WriteShort (&demo.dbuf->sz,
								SVfloat (host_client->edict, frags));
			}

			host_client->old_frags = SVfloat (host_client->edict, frags);
		}
		// maxspeed/entgravity changes
		ent = host_client->edict;

		if (sv_fields.gravity != -1
			&& host_client->entgravity != SVfloat (ent, gravity)) {
			host_client->entgravity = SVfloat (ent, gravity);
			ClientReliableWrite_Begin (host_client, svc_entgravity, 5);
			ClientReliableWrite_Float (host_client, host_client->entgravity);
			if (sv.demorecording) {
				DemoWrite_Begin (dem_single, i, 5);
				MSG_WriteByte (&demo.dbuf->sz, svc_entgravity);
				MSG_WriteFloat (&demo.dbuf->sz, host_client->entgravity);
			}
		}
		if (sv_fields.maxspeed != -1
			&& host_client->maxspeed != SVfloat (ent, maxspeed)) {
			host_client->maxspeed = SVfloat (ent, maxspeed);
			ClientReliableWrite_Begin (host_client, svc_maxspeed, 5);
			ClientReliableWrite_Float (host_client, host_client->maxspeed);
			if (sv.demorecording) {
				DemoWrite_Begin (dem_single, i, 5);
				MSG_WriteByte (&demo.dbuf->sz, svc_maxspeed);
				MSG_WriteFloat (&demo.dbuf->sz, host_client->maxspeed);
			}
		}
	}

	if (sv.datagram.overflowed)
		SZ_Clear (&sv.datagram);

	// append the broadcast messages to each client messages
	for (j = 0, client = svs.clients; j < MAX_CLIENTS; j++, client++) {
		if (client->state < cs_connected)
			continue;				// reliables go to all connected or spawned

		ClientReliableCheckBlock (client, sv.reliable_datagram.cursize);
		ClientReliableWrite_SZ (client, sv.reliable_datagram.data,
								sv.reliable_datagram.cursize);

		if (client->state != cs_spawned)
			continue;					// datagrams only go to spawned
		SZ_Write (&client->datagram, sv.datagram.data, sv.datagram.cursize);
	}

	if (sv.demorecording && sv.reliable_datagram.cursize) {
		DemoWrite_Begin (dem_all, 0, sv.reliable_datagram.cursize);
		SZ_Write (&demo.dbuf->sz, sv.reliable_datagram.data,
				  sv.reliable_datagram.cursize);
	}
	if (sv.demorecording)
		SZ_Write (&demo.datagram, sv.datagram.data, sv.datagram.cursize); // FIXME: ???

	SZ_Clear (&sv.reliable_datagram);
	SZ_Clear (&sv.datagram);
}

#if defined(_WIN32) && !defined(__GNUC__)
# pragma optimize( "", off )
#endif

void
SV_SendClientMessages (void)
{
	client_t   *c;
	int         i, j;

	// update frags, names, etc
	SV_UpdateToReliableMessages ();

	// build individual updates
	for (i = 0, c = svs.clients; i < MAX_CLIENTS; i++, c++) {
		if (c->state < cs_zombie)
			continue;

		if (c->drop) {
			SV_DropClient (c);
			c->drop = false;
			continue;
		}
		// check to see if we have a backbuf to stick in the reliable
		if (c->num_backbuf) {
			// will it fit?
			if (c->netchan.message.cursize + c->backbuf_size[0] <
				c->netchan.message.maxsize) {

				Con_DPrintf ("%s: backbuf %d bytes\n",
							 c->name, c->backbuf_size[0]);

				// it'll fit
				SZ_Write (&c->netchan.message, c->backbuf_data[0],
						  c->backbuf_size[0]);

				// move along, move along
				for (j = 1; j < c->num_backbuf; j++) {
					memcpy (c->backbuf_data[j - 1], c->backbuf_data[j],
							c->backbuf_size[j]);
					c->backbuf_size[j - 1] = c->backbuf_size[j];
				}

				c->num_backbuf--;
				if (c->num_backbuf) {
					memset (&c->backbuf, 0, sizeof (c->backbuf));
					c->backbuf.data = c->backbuf_data[c->num_backbuf - 1];
					c->backbuf.cursize = c->backbuf_size[c->num_backbuf - 1];
					c->backbuf.maxsize =
						sizeof (c->backbuf_data[c->num_backbuf - 1]);
				}
			}
		}
		// if the reliable message overflowed, drop the client
		if (c->netchan.message.overflowed) {
			int i;

			Analyze_Server_Packet (c->netchan.message.data,
								   c->netchan.message.cursize, 0);

			for (i = 0; i < c->num_backbuf; i++) {
				Analyze_Server_Packet (c->backbuf_data[i],
									   c->backbuf_size[i], 0);
			}

			SZ_Clear (&c->netchan.message);
			SZ_Clear (&c->datagram);
			SV_BroadcastPrintf (PRINT_HIGH, "%s overflowed\n", c->name);
			SV_Printf ("WARNING: reliable overflow for %s\n", c->name);
			SV_DropClient (c);
			c->send_message = true;
			c->netchan.cleartime = 0;	// don't choke this message
		}
		// only send messages if the client has sent one
		// and the bandwidth is not choked
		if (!c->send_message)
			continue;
		c->send_message = false;		// try putting this after choke?
		if (!sv.paused && !Netchan_CanPacket (&c->netchan)) {
			c->chokecount++;
			continue;					// bandwidth choke
		}

		if (c->state == cs_spawned)
			SV_SendClientDatagram (c);
		else
			Netchan_Transmit (&c->netchan, 0, NULL);	// just update
														// reliable
	}
}

void
SV_SendDemoMessage (void)
{
	int         i, j, cls = 0;
	client_t   *c;
	byte        buf[MAX_DATAGRAM];
	sizebuf_t	msg;
	int         stats[MAX_CL_STATS];
	float       min_fps;

	if (!sv.demorecording)
		return;

	if (sv_demoPings->value) {
		if (sv.time - demo.pingtime > sv_demoPings->value) {
			SV_DemoPings ();
			demo.pingtime = sv.time;
		}
	}


	if (!sv_demofps->value)
		min_fps = 20.0;
	else
		min_fps = sv_demofps->value;

	min_fps = max (4, min_fps);
	if (sv.time - demo.time < 1.0 / min_fps)
		return;

	for (i = 0, c = svs.clients ; i<MAX_CLIENTS ; i++, c++) {
		if (c->state != cs_spawned)
			continue;	// datagrams only go to spawned

		cls |= 1 << i;
	}

	if (!cls) {
		SZ_Clear (&demo.datagram);
		return;
	}

	msg.data = buf;
	msg.maxsize = sizeof (buf);
	msg.cursize = 0;
	msg.allowoverflow = true;
	msg.overflowed = false;
	
	for (i = 0, c = svs.clients; i < MAX_CLIENTS; i++, c++) {
		if (c->state != cs_spawned)
			continue;	// datagrams only go to spawned

		if (c->spectator)
			continue;

		get_stats (c->edict, 0, stats);

		for (j = 0 ; j < MAX_CL_STATS ; j++)
			if (stats[j] != demo.stats[i][j]) {
				demo.stats[i][j] = stats[j];
				if (stats[j] >=0 && stats[j] <= 255) {
					DemoWrite_Begin (dem_stats, i, 3);
					MSG_WriteByte (&demo.dbuf->sz, svc_updatestat);
					MSG_WriteByte (&demo.dbuf->sz, j);
					MSG_WriteByte (&demo.dbuf->sz, stats[j]);
				} else {
					DemoWrite_Begin (dem_stats, i, 6);
					MSG_WriteByte (&demo.dbuf->sz, svc_updatestatlong);
					MSG_WriteByte (&demo.dbuf->sz, j);
					MSG_WriteLong (&demo.dbuf->sz, stats[j]);
				}
			}
	}

	// send over all the objects that are in the PVS
	// this will include clients, a packetentities, and
	// possibly a nails update
	msg.cursize = 0;
	if (!demo.recorder.delta_sequence)
		demo.recorder.delta_sequence = -1;
	SV_WriteEntitiesToClient (&demo.recorder, &msg, true);
	DemoWrite_Begin (dem_all, 0, msg.cursize);
	SZ_Write (&demo.dbuf->sz, msg.data, msg.cursize);
	// copy the accumulated multicast datagram
	// for this client out to the message
	if (demo.datagram.cursize) {
		DemoWrite_Begin (dem_all, 0, demo.datagram.cursize);
		SZ_Write (&demo.dbuf->sz, demo.datagram.data, demo.datagram.cursize);
		SZ_Clear (&demo.datagram);
	}

	demo.recorder.delta_sequence =
		demo.recorder.netchan.incoming_sequence & 255;
	demo.recorder.netchan.incoming_sequence++;
	demo.frames[demo.parsecount & DEMO_FRAMES_MASK].time = demo.time = sv.time;

	// that's a backup of 3sec in 20fps, should be enough
	if (demo.parsecount - demo.lastwritten > 60) {
		SV_DemoWritePackets (1);
	}

	demo.parsecount++;
	DemoSetMsgBuf (demo.dbuf,
				   &demo.frames[demo.parsecount & DEMO_FRAMES_MASK].buf);

	if (sv_demoMaxSize->int_val && demo.size > sv_demoMaxSize->int_val * 1024)
		SV_Stop (1);
}

#if defined(_WIN32) && !defined(__GNUC__)
# pragma optimize( "", on )
#endif

/*
	SV_SendMessagesToAll

	FIXME: does this sequence right?
*/
void
SV_SendMessagesToAll (void)
{
	client_t   *c;
	int         i;

	for (i = 0, c = svs.clients; i < MAX_CLIENTS; i++, c++)
		if (c->state < cs_zombie)	// FIXME: should this only send to active?
			c->send_message = true;

	SV_SendClientMessages ();
}

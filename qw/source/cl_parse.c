/*
	cl_parse.c

	parse a message received from the server

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
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#ifdef HAVE_ERRNO_H
# include <errno.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "QF/cdaudio.h"
#include "QF/cmd.h"
#include "QF/console.h"
#include "QF/cvar.h"
#include "QF/hash.h"
#include "QF/msg.h"
#include "QF/screen.h"
#include "QF/sound.h"
#include "QF/teamplay.h"
#include "QF/va.h"
#include "QF/vfile.h"
#include "QF/dstring.h"

#include "bothdefs.h"
#include "cl_ents.h"
#include "cl_input.h"
#include "cl_main.h"
#include "cl_parse.h"
#include "cl_skin.h"
#include "cl_tent.h"
#include "client.h"
#include "compat.h"
#include "host.h"
#include "pmove.h"
#include "protocol.h"
#include "sbar.h"
#include "view.h"

char       *svc_strings[] = {
	"svc_bad",
	"svc_nop",
	"svc_disconnect",
	"svc_updatestat",
	"svc_version",				// [long] server version
	"svc_setview",				// [short] entity number
	"svc_sound",				// <see code>
	"svc_time",					// [float] server time
	"svc_print",				// [string] null terminated string
	"svc_stufftext",			// [string] stuffed into client's console
								// buffer the string should be \n terminated
	"svc_setangle",				// [vec3] set view angle to this absolute value

	"svc_serverdata",			// [long] version ...
	"svc_lightstyle",			// [byte] [string]
	"svc_updatename",			// [byte] [string]
	"svc_updatefrags",			// [byte] [short]
	"svc_clientdata",			// <shortbits + data>
	"svc_stopsound",			// <see code>
	"svc_updatecolors",			// [byte] [byte]
	"svc_particle",				// [vec3] <variable>
	"svc_damage",				// [byte] impact [byte] blood [vec3] from

	"svc_spawnstatic",
	"OBSOLETE svc_spawnbinary",
	"svc_spawnbaseline",

	"svc_temp_entity",			// <variable>
	"svc_setpause",
	"svc_signonnum",
	"svc_centerprint",
	"svc_killedmonster",
	"svc_foundsecret",
	"svc_spawnstaticsound",
	"svc_intermission",
	"svc_finale",

	"svc_cdtrack",
	"svc_sellscreen",

	"svc_smallkick",
	"svc_bigkick",

	"svc_updateping",
	"svc_updateentertime",

	"svc_updatestatlong",
	"svc_muzzleflash",
	"svc_updateuserinfo",
	"svc_download",
	"svc_playerinfo",
	"svc_nails",
	"svc_choke",
	"svc_modellist",
	"svc_soundlist",
	"svc_packetentities",
	"svc_deltapacketentities",
	"svc_maxspeed",
	"svc_entgravity",

	"svc_setinfo",
	"svc_serverinfo",
	"svc_updatepl",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL",
	"NEW PROTOCOL"
};

int         oldparsecountmod;
int         parsecountmod;
double      parsecounttime;

int         viewentity;

int         cl_spikeindex, cl_playerindex, cl_flagindex;
int         cl_h_playerindex, cl_gib1index, cl_gib2index, cl_gib3index;

int         packet_latency[NET_TIMINGS];



int
CL_CalcNet (void)
{
	int         lost, a, i;
	frame_t    *frame;

	for (i = cls.netchan.outgoing_sequence - UPDATE_BACKUP + 1;
		 i <= cls.netchan.outgoing_sequence; i++) {
		frame = &cl.frames[i & UPDATE_MASK];
		if (frame->receivedtime == -1)
			packet_latency[i & NET_TIMINGSMASK] = 9999;	// dropped
		else if (frame->receivedtime == -2)
			packet_latency[i & NET_TIMINGSMASK] = 10000;	// choked
		else if (frame->invalid)
			packet_latency[i & NET_TIMINGSMASK] = 9998;	// invalid delta
		else
			packet_latency[i & NET_TIMINGSMASK] =
				(frame->receivedtime - frame->senttime) * 20;
	}

	lost = 0;
	for (a = 0; a < NET_TIMINGS; a++) {
		i = (cls.netchan.outgoing_sequence - a) & NET_TIMINGSMASK;
		if (packet_latency[i] == 9999)
			lost++;
	}
	return lost * 100 / NET_TIMINGS;
}

/*
	CL_CheckOrDownloadFile

	Returns true if the file exists, otherwise it attempts
	to start a download from the server.
*/
qboolean
CL_CheckOrDownloadFile (const char *filename)
{
	VFile      *f;

	if (strstr (filename, "..")) {
		Con_Printf ("Refusing to download a path with ..\n");
		return true;
	}

	if (!snd_initialized && strnequal ("sound/", filename, 6)) {
		// don't bother downloading sownds if we can't play them
		return true;
	}

	COM_FOpenFile (filename, &f);
	if (f) {							// it exists, no need to download
		Qclose (f);
		return true;
	}
	// ZOID - can't download when recording
	if (cls.demorecording) {
		Con_Printf ("Unable to download %s in record mode.\n",
					cls.downloadname);
		return true;
	}
	// ZOID - can't download when playback
	if (cls.demoplayback)
		return true;

	strcpy (cls.downloadname, filename);
	Con_Printf ("Downloading %s...\n", cls.downloadname);

	// download to a temp name, and only rename
	// to the real name when done, so if interrupted
	// a runt file wont be left
	COM_StripExtension (cls.downloadname, cls.downloadtempname);
	strncat (cls.downloadtempname, ".tmp",
			 sizeof (cls.downloadtempname) - strlen (cls.downloadtempname));

	MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
	MSG_WriteString (&cls.netchan.message,
					 va ("download \"%s\"", cls.downloadname));

	cls.downloadnumber++;

	return false;
}

void
Model_NextDownload (void)
{
	char       *s;
	int         i;

	if (cls.downloadnumber == 0) {
		Con_Printf ("Checking models...\n");
		CL_UpdateScreen (realtime);
		cls.downloadnumber = 1;
	}

	cls.downloadtype = dl_model;
	for (; cl.model_name[cls.downloadnumber][0]; cls.downloadnumber++) {
		s = cl.model_name[cls.downloadnumber];
		if (s[0] == '*')
			continue;					// inline brush model
		if (!CL_CheckOrDownloadFile (s))
			return;						// started a download
	}

	for (i = 1; i < MAX_MODELS; i++) {
		char *info_key = 0;

		if (!cl.model_name[i][0])
			break;

		cl.model_precache[i] = Mod_ForName (cl.model_name[i], false);

		if (!cl.model_precache[i]) {
			Con_Printf ("\nThe required model file '%s' could not be found or "
						"downloaded.\n\n", cl.model_name[i]);
			Con_Printf ("You may need to download or purchase a %s client "
						"pack in order to play on this server.\n\n",
						gamedirfile);
			CL_Disconnect ();
			return;
		}

		if (strequal (cl.model_name[i], "progs/player.mdl")
			&& cl.model_precache[i]->type == mod_alias)
			info_key = pmodel_name;
		if (strequal (cl.model_name[i], "progs/eyes.mdl")
			&& cl.model_precache[i]->type == mod_alias)
			info_key = emodel_name;

		if (info_key && cl_model_crcs->int_val) {
			aliashdr_t *ahdr = Cache_Get
				(&cl.model_precache[i]->cache);
			Info_SetValueForKey (cls.userinfo, info_key, va ("%d", ahdr->crc),
								 0);
			MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
			SZ_Print (&cls.netchan.message, va ("setinfo %s %d", info_key,
												ahdr->crc));
			Cache_Release (&cl.model_precache[i]->cache);
		}
	}

	// Something went wrong (probably in the server, probably a TF server)
	// We need to disconnect gracefully.
	if (!cl.model_precache[1]) {
		Con_Printf ("\nThe server has failed to provide the map name.\n\n");
		Con_Printf ("Disconnecting to prevent a crash.\n\n");
		CL_Disconnect ();
		return;
	}

	// all done
	cl.worldmodel = cl.model_precache[1];

	R_NewMap (cl.worldmodel, cl.model_precache, MAX_MODELS);
	Team_NewMap ();
	Con_NewMap ();
	Hunk_Check ();						// make sure nothing is hurt

	// done with modellist, request first of static signon messages
	MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
	MSG_WriteString (&cls.netchan.message,
					 va (prespawn_name, cl.servercount,
						 cl.worldmodel->checksum2));
}

void
Sound_NextDownload (void)
{
	char       *s;
	int         i;

	if (cls.downloadnumber == 0) {
		Con_Printf ("Checking sounds...\n");
		CL_UpdateScreen (realtime);
		cls.downloadnumber = 1;
	}

	cls.downloadtype = dl_sound;
	for (; cl.sound_name[cls.downloadnumber][0];
		 cls.downloadnumber++) {
		s = cl.sound_name[cls.downloadnumber];
		if (!CL_CheckOrDownloadFile (va ("sound/%s", s)))
			return;						// started a download
	}

	for (i = 1; i < MAX_SOUNDS; i++) {
		if (!cl.sound_name[i][0])
			break;
		cl.sound_precache[i] = S_PrecacheSound (cl.sound_name[i]);
	}

	// done with sounds, request models now
	memset (cl.model_precache, 0, sizeof (cl.model_precache));
	cl_playerindex = -1;
	cl_spikeindex = -1;
	cl_flagindex = -1;
	cl_h_playerindex = -1;
	cl_gib1index = cl_gib2index = cl_gib3index = -1;
	MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
	MSG_WriteString (&cls.netchan.message,
					 va (modellist_name, cl.servercount, 0));
}

void
CL_RequestNextDownload (void)
{
	switch (cls.downloadtype) {
		case dl_single:
			break;
		case dl_skin:
			Skin_NextDownload ();
			break;
		case dl_model:
			Model_NextDownload ();
			break;
		case dl_sound:
			Sound_NextDownload ();
			break;
		case dl_none:
		default:
			Con_DPrintf ("Unknown download type.\n");
	}
}

/*
	CL_ParseDownload

	A download message has been received from the server
*/
void
CL_ParseDownload (void)
{
	byte        name[1024];
	int         size, percent, r;

	// read the data
	size = MSG_ReadShort (net_message);
	percent = MSG_ReadByte (net_message);

	if (cls.demoplayback) {
		if (size > 0)
			net_message->readcount += size;
		cls.downloadname[0] = 0;
		return;							// not in demo playback
	}

	if (size == -1) {
		Con_Printf ("File not found.\n");
		if (cls.download) {
			Con_Printf ("cls.download shouldn't have been set\n");
			Qclose (cls.download);
			cls.download = NULL;
		}
		cls.downloadname[0] = 0;
		CL_RequestNextDownload ();
		return;
	}

	if (size == -2) {
		const char *newname = MSG_ReadString (net_message);

		if (strncmp (newname, cls.downloadname, strlen (cls.downloadname))
			|| strstr (newname + strlen (cls.downloadname), "/")) {
			Con_Printf
				("WARNING: server tried to give a strange new name: %s\n",
				 newname);
			CL_RequestNextDownload ();
			return;
		}
		if (cls.download) {
			Qclose (cls.download);
			unlink (cls.downloadname);
		}
		strncpy (cls.downloadname, newname, sizeof (cls.downloadname));
		Con_Printf ("downloading to %s\n", cls.downloadname);
		return;
	}
	// open the file if not opened yet
	if (!cls.download) {
		if (strncmp (cls.downloadtempname, "skins/", 6))
			snprintf (name, sizeof (name), "%s/%s", com_gamedir,
					  cls.downloadtempname);
		else
			snprintf (name, sizeof (name), "%s/%s/%s", fs_userpath->string,
					  fs_skinbase->string, cls.downloadtempname);

		COM_CreatePath (name);

		cls.download = Qopen (name, "wb");
		if (!cls.download) {
			cls.downloadname[0] = 0;
			net_message->readcount += size;
			Con_Printf ("Failed to open %s\n", cls.downloadtempname);
			CL_RequestNextDownload ();
			return;
		}
	}

	Qwrite (cls.download, net_message->message->data + net_message->readcount,
			size);
	net_message->readcount += size;

	if (percent != 100) {
		// request next block
		if (percent != cls.downloadpercent)
			VID_SetCaption (va ("Downloading %s %d%%", cls.downloadname,
								percent));
		cls.downloadpercent = percent;

		MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
		SZ_Print (&cls.netchan.message, "nextdl");
	} else {
		char        oldn[MAX_OSPATH];
		char        newn[MAX_OSPATH];

		Qclose (cls.download);
		VID_SetCaption (va ("Connecting to %s", cls.servername));

		// rename the temp file to it's final name
		if (strcmp (cls.downloadtempname, cls.downloadname)) {
			if (strncmp (cls.downloadtempname, "skins/", 6)) {
				snprintf (oldn, sizeof (oldn), "%s/%s", com_gamedir,
						  cls.downloadtempname);
				snprintf (newn, sizeof (newn), "%s/%s", com_gamedir,
						  cls.downloadname);
			} else {
				snprintf (oldn, sizeof (oldn), "%s/%s/%s", fs_userpath->string,
						  fs_skinbase->string, cls.downloadtempname);
				snprintf (newn, sizeof (newn), "%s/%s/%s", fs_userpath->string,
						  fs_skinbase->string, cls.downloadname);
			}
			r = Qrename (oldn, newn);
			if (r)
				Con_Printf ("failed to rename, %s.\n", strerror (errno));
		}

		cls.download = NULL;
		cls.downloadname[0] = 0;
		cls.downloadpercent = 0;

		// get another file if needed
		CL_RequestNextDownload ();
	}
}

static byte *upload_data;
static int  upload_pos, upload_size;

void
CL_NextUpload (void)
{
	byte        buffer[1024];
	int         percent, size, r;

	if (!upload_data)
		return;

	r = upload_size - upload_pos;
	if (r > 768)
		r = 768;
	memcpy (buffer, upload_data + upload_pos, r);
	MSG_WriteByte (&cls.netchan.message, clc_upload);
	MSG_WriteShort (&cls.netchan.message, r);

	upload_pos += r;
	size = upload_size;
	if (!size)
		size = 1;
	percent = upload_pos * 100 / size;
	MSG_WriteByte (&cls.netchan.message, percent);
	SZ_Write (&cls.netchan.message, buffer, r);

	Con_DPrintf ("UPLOAD: %6d: %d written\n", upload_pos - r, r);

	if (upload_pos != upload_size)
		return;

	Con_Printf ("Upload completed\n");

	free (upload_data);
	upload_data = 0;
	upload_pos = upload_size = 0;
}

void
CL_StartUpload (byte * data, int size)
{
	if (cls.state < ca_onserver)
		return;							// gotta be connected

	// override
	if (upload_data)
		free (upload_data);

	Con_DPrintf ("Upload starting of %d...\n", size);

	upload_data = malloc (size);
	memcpy (upload_data, data, size);
	upload_size = size;
	upload_pos = 0;

	CL_NextUpload ();
}

qboolean
CL_IsUploading (void)
{
	if (upload_data)
		return true;
	return false;
}

void
CL_StopUpload (void)
{
	if (upload_data)
		free (upload_data);
	upload_data = NULL;
}

/* SERVER CONNECTING MESSAGES */

void Draw_ClearCache (void);
void CL_ClearBaselines (void);		// LordHavoc: BIG BUG-FIX!

void
CL_ParseServerData (void)
{
	char        fn[MAX_OSPATH];
	const char *str;
	int         protover;
	qboolean    cflag = false;

	extern char gamedirfile[MAX_OSPATH];

	Con_DPrintf ("Serverdata packet received.\n");

	// wipe the client_state_t struct
	CL_ClearState ();

	// parse protocol version number
	// allow 2.2 and 2.29 demos to play
	protover = MSG_ReadLong (net_message);
	if (protover != PROTOCOL_VERSION &&
		!(cls.demoplayback
		  && (protover <= 26 && protover >= 28)))
			Host_Error ("Server returned version %i, not %i\nYou probably "
						"need to upgrade.\nCheck http://www.quakeworld.net/",
						protover, PROTOCOL_VERSION);

	cl.servercount = MSG_ReadLong (net_message);

	// game directory
	str = MSG_ReadString (net_message);

	if (!strequal (gamedirfile, str)) {
		// save current config
		Host_WriteConfiguration ();
		cflag = true;
		Draw_ClearCache ();
	}

	COM_Gamedir (str);

	// ZOID--run the autoexec.cfg in the gamedir
	// if it exists
	if (cflag) {
		int         cmd_warncmd_val = cmd_warncmd->int_val;

		Cbuf_AddText ("cmd_warncmd 0\n");
		Cbuf_AddText ("exec config.cfg\n");
		Cbuf_AddText ("exec frontend.cfg\n");
		if (cl_autoexec->int_val) {
			Cbuf_AddText ("exec autoexec.cfg\n");
		}
		snprintf (fn, sizeof (fn), "cmd_warncmd %d\n", cmd_warncmd_val);
		Cbuf_AddText (fn);
	}
	// parse player slot, high bit means spectator
	cl.playernum = MSG_ReadByte (net_message);
	if (cl.playernum & 128) {
		cl.spectator = true;
		cl.playernum &= ~128;
	}

// FIXME: evil hack so NQ and QW can share sound code
	cl.viewentity = cl.playernum + 1;
	viewentity = cl.playernum + 1;

	// get the full level name
	str = MSG_ReadString (net_message);
	strncpy (cl.levelname, str, sizeof (cl.levelname) - 1);

	// get the movevars
	movevars.gravity = MSG_ReadFloat (net_message);
	movevars.stopspeed = MSG_ReadFloat (net_message);
	movevars.maxspeed = MSG_ReadFloat (net_message);
	movevars.spectatormaxspeed = MSG_ReadFloat (net_message);
	movevars.accelerate = MSG_ReadFloat (net_message);
	movevars.airaccelerate = MSG_ReadFloat (net_message);
	movevars.wateraccelerate = MSG_ReadFloat (net_message);
	movevars.friction = MSG_ReadFloat (net_message);
	movevars.waterfriction = MSG_ReadFloat (net_message);
	movevars.entgravity = MSG_ReadFloat (net_message);

	// separate the printfs so the server message can have a color
	Con_Printf ("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36"
				"\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n");
	Con_Printf ("%c%s\n", 2, str);

	// ask for the sound list next
	memset (cl.sound_name, 0, sizeof (cl.sound_name));
	MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
	MSG_WriteString (&cls.netchan.message,
					 va (soundlist_name, cl.servercount, 0));

	// now waiting for downloads, etc
	CL_SetState (ca_onserver);

	CL_ClearBaselines ();
}

// LordHavoc: BIG BUG-FIX!  Clear baselines each time it connects...
void
CL_ClearBaselines (void)
{
	int         i;

	memset (cl_baselines, 0, sizeof (cl_baselines));
	for (i = 0; i < MAX_EDICTS; i++) {
		cl_baselines[i].colormod = 255;
		cl_baselines[i].alpha = 255;
		cl_baselines[i].scale = 16;
		cl_baselines[i].glow_size = 0;
		cl_baselines[i].glow_color = 254;
	}
}

void
CL_ParseSoundlist (void)
{
	const char *str;
	int         numsounds, n;

	// precache sounds
//	memset (cl.sound_precache, 0, sizeof(cl.sound_precache));

	numsounds = MSG_ReadByte (net_message);

	for (;;) {
		str = MSG_ReadString (net_message);
		if (!str[0])
			break;
		numsounds++;
		if (numsounds == MAX_SOUNDS)
			Host_Error ("Server sent too many sound_precache");
		strcpy (cl.sound_name[numsounds], str);
	}

	n = MSG_ReadByte (net_message);

	if (n) {
		MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
		MSG_WriteString (&cls.netchan.message,
						 va (soundlist_name, cl.servercount, n));
		return;
	}

	cls.downloadnumber = 0;
	cls.downloadtype = dl_sound;
	Sound_NextDownload ();
}

void
CL_ParseModellist (void)
{
	int         nummodels, n;
	const char *str;

	// precache models and note certain default indexes
	nummodels = MSG_ReadByte (net_message);

	for (;;) {
		str = MSG_ReadString (net_message);
		if (!str[0])
			break;
		nummodels++;
		if (nummodels == MAX_MODELS)
			Host_Error ("Server sent too many model_precache");
		strcpy (cl.model_name[nummodels], str);

		if (!strcmp (cl.model_name[nummodels], "progs/spike.mdl"))
			cl_spikeindex = nummodels;
		else if (!strcmp (cl.model_name[nummodels], "progs/player.mdl"))
			cl_playerindex = nummodels;
		else if (!strcmp (cl.model_name[nummodels], "progs/flag.mdl"))
			cl_flagindex = nummodels;
		// for deadbodyfilter & gibfilter
		else if (!strcmp (cl.model_name[nummodels], "progs/h_player.mdl"))
			cl_h_playerindex = nummodels;
		else if (!strcmp (cl.model_name[nummodels], "progs/gib1.mdl"))
			cl_gib1index = nummodels;
		else if (!strcmp (cl.model_name[nummodels], "progs/gib2.mdl"))
			cl_gib2index = nummodels;
		else if (!strcmp (cl.model_name[nummodels], "progs/gib3.mdl"))
			cl_gib3index = nummodels;
	}

	n = MSG_ReadByte (net_message);

	if (n) {
		MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
		MSG_WriteString (&cls.netchan.message,
						 va (modellist_name, cl.servercount, n));
		return;
	}

	cls.downloadnumber = 0;
	cls.downloadtype = dl_model;
	Model_NextDownload ();
}

void
CL_ParseBaseline (entity_state_t *es)
{
	es->modelindex = MSG_ReadByte (net_message);
	es->frame = MSG_ReadByte (net_message);
	es->colormap = MSG_ReadByte (net_message);
	es->skinnum = MSG_ReadByte (net_message);

	MSG_ReadCoordAngleV (net_message, es->origin, es->angles);

	// LordHavoc: set up baseline to for new effects (alpha, colormod, etc)
	es->colormod = 255;
	es->alpha = 255;
	es->scale = 16;
	es->glow_size = 0;
	es->glow_color = 254;
}

/*
	CL_ParseStatic

	Static entities are non-interactive world objects
	like torches
*/
void
CL_ParseStatic (void)
{
	entity_t   *ent;
	entity_state_t es;

	CL_ParseBaseline (&es);

	if (cl.num_statics >= MAX_STATIC_ENTITIES)
		Host_Error ("Too many static entities");
	ent = &cl_static_entities[cl.num_statics++];
	CL_Init_Entity (ent);

	// copy it to the current state
	ent->model = cl.model_precache[es.modelindex];
	ent->frame = es.frame;
	ent->skinnum = es.skinnum;

	VectorCopy (es.origin, ent->origin);
	VectorCopy (es.angles, ent->angles);

	R_AddEfrags (ent);
}

void
CL_ParseStaticSound (void)
{
	int         sound_num, vol, atten;
	vec3_t      org;

	MSG_ReadCoordV (net_message, org);
	sound_num = MSG_ReadByte (net_message);
	vol = MSG_ReadByte (net_message);
	atten = MSG_ReadByte (net_message);

	S_StaticSound (cl.sound_precache[sound_num], org, vol, atten);
}

/* ACTION MESSAGES */

void
CL_ParseStartSoundPacket (void)
{
	float       attenuation;
	int         channel, ent, sound_num, volume;
	vec3_t      pos;

	channel = MSG_ReadShort (net_message);

	if (channel & SND_VOLUME)
		volume = MSG_ReadByte (net_message);
	else
		volume = DEFAULT_SOUND_PACKET_VOLUME;

	if (channel & SND_ATTENUATION)
		attenuation = MSG_ReadByte (net_message) / 64.0;
	else
		attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;

	sound_num = MSG_ReadByte (net_message);

	MSG_ReadCoordV (net_message, pos);

	ent = (channel >> 3) & 1023;
	channel &= 7;

	if (ent > MAX_EDICTS)
		Host_Error ("CL_ParseStartSoundPacket: ent = %i", ent);

	S_StartSound (ent, channel, cl.sound_precache[sound_num], pos,
				  volume / 255.0, attenuation);
}

/*
	CL_ParseClientdata

	Server information pertaining to this client only, sent every frame
*/
void
CL_ParseClientdata (void)
{
	float       latency;
	frame_t    *frame;
	int         i;

	// calculate simulated time of message
	oldparsecountmod = parsecountmod;

	i = cls.netchan.incoming_acknowledged;
	cl.parsecount = i;
	i &= UPDATE_MASK;
	parsecountmod = i;
	frame = &cl.frames[i];
	parsecounttime = cl.frames[i].senttime;

	frame->receivedtime = realtime;

	// calculate latency
	latency = frame->receivedtime - frame->senttime;

	if (!(latency < 0 || latency > 1.0)) {
		// drift the average latency towards the observed latency
		if (latency < cls.latency)
			cls.latency = latency;
		else
			cls.latency += 0.001;		// drift up, so correction is needed
	}
}

void
CL_ProcessUserInfo (int slot, player_info_t *player)
{
	char        skin[512];
	const char *s;

	s = Info_ValueForKey (player->userinfo, "skin");
	COM_StripExtension (s, skin);   // FIXME: buffer overflow
	if (!strequal (s, skin))
		Info_SetValueForKey (player->userinfo, "skin", skin, 1);
	strncpy (player->name, Info_ValueForKey (player->userinfo, "name"),
			 sizeof (player->name) - 1);
	player->_topcolor = player->_bottomcolor = -1;
	player->topcolor = atoi (Info_ValueForKey (player->userinfo, "topcolor"));
	player->bottomcolor =
		atoi (Info_ValueForKey (player->userinfo, "bottomcolor"));

	while (!(player->team = Hash_Find (player->userinfo->tab, "team")))
			Info_SetValueForKey (player->userinfo, "team", "", 1);
	while (!(player->skinname = Hash_Find (player->userinfo->tab, "skin")))
			Info_SetValueForKey (player->userinfo, "skin", "", 1);

	if (Info_ValueForKey (player->userinfo, "*spectator")[0])
		player->spectator = true;
	else
		player->spectator = false;

	if (cls.state == ca_active)
		Skin_Find (player);

	Sbar_Changed ();
}

void
CL_UpdateUserinfo (void)
{
	int         slot;
	player_info_t *player;
	int         uid;
	const char *info;

	slot = MSG_ReadByte (net_message);
	if (slot >= MAX_CLIENTS)
		Host_Error
			("CL_ParseServerMessage: svc_updateuserinfo > MAX_SCOREBOARD");

	player = &cl.players[slot];
	if (player->userinfo)
		Info_Destroy (player->userinfo);
	uid = MSG_ReadLong (net_message);
	info = MSG_ReadString (net_message);
	if (*info) {
		// a totally empty userinfo string should not be possible
		player->userid = uid;
		player->userinfo = Info_ParseString (info, MAX_INFO_STRING);
		CL_ProcessUserInfo (slot, player);
	} else {
		// the server dropped the client
		memset (player, 0, sizeof (*player));
	}
}

void
CL_SetInfo (void)
{
	char        key[MAX_MSGLEN], value[MAX_MSGLEN];
	int         slot;
	int         flags;
	player_info_t *player;

	slot = MSG_ReadByte (net_message);
	if (slot >= MAX_CLIENTS)
		Host_Error ("CL_ParseServerMessage: svc_setinfo > MAX_SCOREBOARD");

	player = &cl.players[slot];

	strncpy (key, MSG_ReadString (net_message), sizeof (key) - 1);
	key[sizeof (key) - 1] = 0;
	strncpy (value, MSG_ReadString (net_message), sizeof (value) - 1);
	key[sizeof (value) - 1] = 0;

	Con_DPrintf ("SETINFO %s: %s=%s\n", player->name, key, value);

	if (!player->userinfo)
		player->userinfo = Info_ParseString ("", MAX_INFO_STRING);

	flags = !strequal (key, "name");
	flags |= strequal (key, "team") << 1;
	Info_SetValueForKey (player->userinfo, key, value, flags);

	CL_ProcessUserInfo (slot, player);
}

void
CL_ServerInfo (void)
{
	char        key[MAX_MSGLEN], value[MAX_MSGLEN];

	strncpy (key, MSG_ReadString (net_message), sizeof (key) - 1);
	key[sizeof (key) - 1] = 0;
	strncpy (value, MSG_ReadString (net_message), sizeof (value) - 1);
	key[sizeof (value) - 1] = 0;

	Con_DPrintf ("SERVERINFO: %s=%s\n", key, value);

	Info_SetValueForKey (cl.serverinfo, key, value, 0);
	if (strequal (key, "chase")) {
		cl.chase = atoi (value);
	} else if (strequal (key, "cshifts")) {
		cl.sv_cshifts = atoi (value);
	} else if (strequal (key, "no_pogo_stick")) {
		cl.no_pogo_stick = atoi (value);
	} else if (strequal (key, "teamplay")) {
		cl.teamplay = atoi (value);
	} else if (strequal (key, "watervis")) {
		cl.watervis = atoi (value);
	}
}

void
CL_SetStat (int stat, int value)
{
	int         j;

	if (stat < 0 || stat >= MAX_CL_STATS)
//		Sys_Error ("CL_SetStat: %i is invalid", stat);
		Host_Error ("CL_SetStat: %i is invalid", stat);

	Sbar_Changed ();

	switch (stat) {
		case STAT_ITEMS:				// set flash times
			Sbar_Changed ();
			for (j = 0; j < 32; j++)
				if ((value & (1 << j)) && !(cl.stats[stat] & (1 << j)))
					cl.item_gettime[j] = cl.time;
			break;
		case STAT_HEALTH:
			if (value <= 0)
				Team_Dead ();
			break;
	}

	cl.stats[stat] = value;
}

void
CL_MuzzleFlash (void)
{
	dlight_t   *dl;
	int         i;
	player_state_t *pl;
	vec3_t      fv, rv, uv;

	i = MSG_ReadShort (net_message);

	if ((unsigned int) (i - 1) >= MAX_CLIENTS)
		return;

	pl = &cl.frames[parsecountmod].playerstate[i - 1];

	dl = R_AllocDlight (i);
	if (!dl)
		return;

	if (i - 1 == cl.playernum)
		AngleVectors (cl.viewangles, fv, rv, uv);
	else
		AngleVectors (pl->viewangles, fv, rv, uv);

	VectorMA (pl->origin, 18, fv, dl->origin);
	dl->radius = 200 + (rand () & 31);
	dl->die = cl.time + 0.1;
	dl->minlight = 32;
	dl->color[0] = 0.2;
	dl->color[1] = 0.1;
	dl->color[2] = 0.05;
}

#define SHOWNET(x) if (cl_shownet->int_val == 2) Con_Printf ("%3i:%s\n", net_message->readcount-1, x);

int         received_framecount;

void Sbar_LogFrags(void);

void
CL_ParseServerMessage (void)
{
	const char *s;
	static dstring_t *stuffbuf;
	int         cmd, i, j;

	received_framecount = host_framecount;
	cl.last_servermessage = realtime;
	CL_ClearProjectiles ();

	// if recording demos, copy the message out
	if (cl_shownet->int_val == 1)
		Con_Printf ("%i ", net_message->message->cursize);
	else if (cl_shownet->int_val == 2)
		Con_Printf ("------------------\n");

	CL_ParseClientdata ();

	// parse the message
	while (1) {
		if (net_message->badread) {
			Host_Error ("CL_ParseServerMessage: Bad server message");
			break;
		}

		cmd = MSG_ReadByte (net_message);

		if (cmd == -1) {
			net_message->readcount++;	// so the EOM showner has the right
										// value
			SHOWNET ("END OF MESSAGE");
			break;
		}

		SHOWNET (svc_strings[cmd]);

		// other commands
		switch (cmd) {
			default:
				Host_Error ("CL_ParseServerMessage: Illegible server message");
				break;

			case svc_nop:
				break;

			case svc_disconnect:
				if (cls.state == ca_connected)
					Host_EndGame ("Server disconnected\n"
								  "Server version may not be compatible");
				else
					Host_EndGame ("Server disconnected");
				break;

			case svc_print: {
 				char p[2048];
				i = MSG_ReadByte (net_message);
				s = MSG_ReadString (net_message);
				if (i == PRINT_CHAT) {
					// TODO: cl_nofake 2 -- accept fake messages from teammates

					if (cl_nofake->int_val) {
						char *c;
						strncpy (p, s, sizeof (p));
						p[sizeof (p) - 1] = 0;
						for (c = p; *c; c++) {
							if (*c == '\r')
								*c = '#';
						}
						s = p;
					}
					Con_SetOrMask (128);
					S_LocalSound ("misc/talk.wav");
					Team_ParseChat(s);
				}
				Con_Printf ("%s", s);
				Con_SetOrMask (0);
				break;
			}
			case svc_centerprint:
				SCR_CenterPrint (MSG_ReadString (net_message));
				break;

			case svc_stufftext:
				s = MSG_ReadString (net_message);
				if (s[strlen (s) - 1] == '\n') {
					if (stuffbuf && stuffbuf->str[0]) {
						Con_DPrintf ("stufftext: %s%s\n", stuffbuf->str, s);
						Cbuf_AddTextTo (cmd_legacybuffer, stuffbuf->str);
						dstring_clearstr (stuffbuf);
					} else {
						Con_DPrintf ("stufftext: %s\n", s);
					}
					Cbuf_AddTextTo (cmd_legacybuffer, s);
				} else {
					Con_DPrintf ("partial stufftext: %s\n", s);
					if (!stuffbuf)
						stuffbuf = dstring_newstr();
					dstring_appendstr (stuffbuf, s);
				}
				break;

			case svc_damage:
				V_ParseDamage ();
				break;

			case svc_serverdata:
				Cbuf_Execute ();		// make sure any stuffed commands are 
										// done
				CL_ParseServerData ();
				vid.recalc_refdef = true;	// leave full screen intermission
				break;

			case svc_setangle:
				MSG_ReadAngleV (net_message, cl.viewangles);
//				cl.viewangles[PITCH] = cl.viewangles[ROLL] = 0;
				break;

			case svc_lightstyle:
				i = MSG_ReadByte (net_message);
				if (i >= MAX_LIGHTSTYLES)
//					Sys_Error ("svc_lightstyle > MAX_LIGHTSTYLES");
					Host_Error ("svc_lightstyle > MAX_LIGHTSTYLES");
				strcpy (r_lightstyle[i].map, MSG_ReadString (net_message));
				r_lightstyle[i].length = strlen (r_lightstyle[i].map);
				break;

			case svc_sound:
				CL_ParseStartSoundPacket ();
				break;

			case svc_stopsound:
				i = MSG_ReadShort (net_message);
				S_StopSound (i >> 3, i & 7);
				break;

			case svc_updatefrags:
				Sbar_Changed ();
				i = MSG_ReadByte (net_message);
				if (i >= MAX_CLIENTS)
					Host_Error ("CL_ParseServerMessage: svc_updatefrags > "
								"MAX_SCOREBOARD");
				cl.players[i].frags = MSG_ReadShort (net_message);
				break;

			case svc_updateping:
				i = MSG_ReadByte (net_message);
				if (i >= MAX_CLIENTS)
					Host_Error ("CL_ParseServerMessage: svc_updateping > "
								"MAX_SCOREBOARD");
				cl.players[i].ping = MSG_ReadShort (net_message);
				break;

			case svc_updatepl:
				i = MSG_ReadByte (net_message);
				if (i >= MAX_CLIENTS)
					Host_Error ("CL_ParseServerMessage: svc_updatepl > "
								"MAX_SCOREBOARD");
				cl.players[i].pl = MSG_ReadByte (net_message);
				break;

			case svc_updateentertime:
				// time is sent over as seconds ago
				i = MSG_ReadByte (net_message);
				if (i >= MAX_CLIENTS)
					Host_Error ("CL_ParseServerMessage: svc_updateentertime "
								"> MAX_SCOREBOARD");
				cl.players[i].entertime = realtime - MSG_ReadFloat
					(net_message);
				break;

			case svc_spawnbaseline:
				i = MSG_ReadShort (net_message);
				CL_ParseBaseline (&cl_baselines[i]);
				break;
			case svc_spawnstatic:
				CL_ParseStatic ();
				break;
			case svc_temp_entity:
				CL_ParseTEnt ();
				break;

			case svc_killedmonster:
				cl.stats[STAT_MONSTERS]++;
				break;

			case svc_foundsecret:
				cl.stats[STAT_SECRETS]++;
				break;

			case svc_updatestat:
				i = MSG_ReadByte (net_message);
				j = MSG_ReadByte (net_message);
				CL_SetStat (i, j);
				break;
			case svc_updatestatlong:
				i = MSG_ReadByte (net_message);
				j = MSG_ReadLong (net_message);
				CL_SetStat (i, j);
				break;

			case svc_spawnstaticsound:
				CL_ParseStaticSound ();
				break;

			case svc_cdtrack:
				cl.cdtrack = MSG_ReadByte (net_message);
				CDAudio_Play ((byte) cl.cdtrack, true);
				break;

			case svc_intermission:
				Con_DPrintf ("svc_intermission\n");

				cl.intermission = 1;
				cl.completed_time = realtime;
				vid.recalc_refdef = true;	// go to full screen
				Con_DPrintf ("intermission simorg: ");
				MSG_ReadCoordV (net_message, cl.simorg);
				for (i = 0; i < 3; i++)
					Con_DPrintf ("%f ", cl.simorg[i]);
				Con_DPrintf ("\nintermission simangles: ");
				MSG_ReadAngleV (net_message, cl.simangles);
				for (i = 0; i < 3; i++)
					Con_DPrintf ("%f ", cl.simangles[i]);
				Con_DPrintf ("\n");
				VectorCopy (vec3_origin, cl.simvel);

				/*
					automatic fraglogging (by elmex)
					XXX: Should this _really_ called here?
				*/
				if (!cls.demoplayback)
					Sbar_LogFrags();
				break;

			case svc_finale:
				Con_Printf("svc_finale\n");
				cl.intermission = 2;
				cl.completed_time = realtime;
				vid.recalc_refdef = true;	// go to full screen
				SCR_CenterPrint (MSG_ReadString (net_message));
				break;

			case svc_sellscreen:
				Cmd_ExecuteString ("help", src_command);
				break;

			case svc_smallkick:
				cl.punchangle = -2;
				break;
			case svc_bigkick:
				cl.punchangle = -4;
				break;

			case svc_muzzleflash:
				CL_MuzzleFlash ();
				break;

			case svc_updateuserinfo:
				CL_UpdateUserinfo ();
				break;

			case svc_setinfo:
				CL_SetInfo ();
				break;

			case svc_serverinfo:
				CL_ServerInfo ();
				break;

			case svc_download:
				CL_ParseDownload ();
				break;

			case svc_playerinfo:
				CL_ParsePlayerinfo ();
				break;

			case svc_nails:
				CL_ParseProjectiles ();
				break;

			case svc_chokecount:		// some preceding packets were choked
				i = MSG_ReadByte (net_message);
				for (j = 0; j < i; j++)
					cl.
						frames[(cls.netchan.incoming_acknowledged - 1 - j) &
							   UPDATE_MASK].receivedtime = -2;
				break;

			case svc_modellist:
				CL_ParseModellist ();
				break;

			case svc_soundlist:
				CL_ParseSoundlist ();
				break;

			case svc_packetentities:
				CL_ParsePacketEntities (false);
				break;

			case svc_deltapacketentities:
				CL_ParsePacketEntities (true);
				break;

			case svc_maxspeed:
				movevars.maxspeed = MSG_ReadFloat (net_message);
				break;

			case svc_entgravity:
				movevars.entgravity = MSG_ReadFloat (net_message);
				break;

			case svc_setpause:
				cl.paused = MSG_ReadByte (net_message);
				if (cl.paused)
					CDAudio_Pause ();
				else
					CDAudio_Resume ();
				break;

		}
	}

	CL_SetSolidEntities ();
}

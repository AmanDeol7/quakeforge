/*
	client.h

	Client definitions

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

#ifndef _CLIENT_H
#define _CLIENT_H

#include "QF/info.h"
#include "QF/vfs.h"
#include "QF/vid.h"
#include "QF/zone.h"

#include "net.h"
#include "protocol.h"
#include "r_local.h"
#include "QF/render.h"


/*
  player_state_t is the information needed by a player entity
  to do move prediction and to generate a drawable entity
*/
typedef struct player_state_s {
	int			messagenum;		// all player's won't be updated each frame

	double		state_time;		// not the same as the packet time,
								// because player commands come asyncronously
	usercmd_t	command;		// last command for prediction

	vec3_t		origin;
	vec3_t		viewangles;		// only for demos, not from server
	vec3_t		velocity;
	int			weaponframe;

	int 		number;
	int			modelindex;
	int			frame;
	int			skinnum;
	int			effects;

	int			flags;			// dead, gib, etc

	float		waterjumptime;
	int			onground;		// -1 = in air, else pmove entity number
	int			oldbuttons;

	// QSG2
	byte		glow_size;
	byte		glow_color;
} player_state_t;

#undef MAX_SCOREBOARDNAME
#define	MAX_SCOREBOARDNAME	16

typedef struct player_info_s
{
	int		userid;
	struct info_s	*userinfo;

	// scoreboard information
	char	name[MAX_SCOREBOARDNAME];
	float	entertime;
	int		frags;
	int		ping;
	byte	pl;

	// skin information
	int		topcolor;
	int		bottomcolor;
	struct info_key_s *skinname;
	struct info_key_s *team;

	int		_topcolor;
	int		_bottomcolor;

	int		spectator;
	byte	translations[4*VID_GRADES*256];	// space for colormap32
	int		translationcolor[256];
	struct skin_s	*skin;
} player_info_t;


typedef struct
{
	// generated on client side
	usercmd_t	cmd;		// cmd that generated the frame
	double		senttime;	// time cmd was sent off
	int			delta_sequence;		// sequence number to delta from, -1 = full update

	// received from server
	double		receivedtime;	// time message was received, or -1
	player_state_t	playerstate[MAX_CLIENTS];	// message received that
												// reflects performing the
												// usercmd
	packet_entities_t	packet_entities;
	qboolean	invalid;		// true if the packet_entities delta was invalid
} frame_t;


#define	MAX_DEMOS		8
#define	MAX_DEMONAME	16


typedef enum {
	ca_disconnected, 	// full screen console with no connection
	ca_demostart,		// starting up a demo
	ca_connected,		// netchan_t established, waiting for svc_serverdata
	ca_onserver,		// processing data lists, donwloading, etc
	ca_active			// everything is in, so frames can be rendered
} cactive_t;

typedef enum {
	dl_none,
	dl_model,
	dl_sound,
	dl_skin,
	dl_single
} dltype_t;		// download type

/*
  the client_static_t structure is persistant through an arbitrary number
  of server connections
*/
typedef struct
{
// connection information
	cactive_t	state;
	
// network stuff
	netchan_t	netchan;

// private userinfo for sending to masterless servers
	struct info_s	*userinfo;

	char		servername[MAX_OSPATH];	// name of server from original connect
	netadr_t	server_addr;			// address of server

	int			qport;

// file transfer from server
	VFile		*download;
	char		downloadtempname[MAX_OSPATH];
	char		downloadname[MAX_OSPATH];
	int			downloadnumber;
	dltype_t	downloadtype;
	int			downloadpercent;

// demo loop control
	int			demonum;		// -1 = don't play demos
	char		demos[MAX_DEMOS][MAX_DEMONAME];		// when not playing

// demo recording info must be here, because record is started before
// entering a map (and clearing client_state_t)
	qboolean	demorecording;
	qboolean	demoplayback;
	qboolean	timedemo;
	VFile		*demofile;
	float		td_lastframe;		// to meter out one message a frame
	int			td_startframe;		// host_framecount at start
	float		td_starttime;		// realtime at second frame of timedemo

	int			challenge;

	float		latency;		// rolling average
} client_static_t;

extern client_static_t	cls;

/*
  the client_state_t structure is wiped completely at every server signon
*/
typedef struct
{
	int			servercount;	// server identification for prespawns

	struct info_s	*serverinfo;

	int			parsecount;		// server message counter
	int			validsequence;	// this is the sequence number of the last good
								// packetentity_t we got.  If this is 0, we can't
								// render a frame yet
	int			movemessages;	// since connecting to this server
								// throw out the first couple, so the player
								// doesn't accidentally do something the 
								// first frame

	int			spectator;

	double		last_ping_request;	// while showing scoreboard
	double		last_servermessage;

// sentcmds[cl.netchan.outgoing_sequence & UPDATE_MASK] = cmd
	frame_t		frames[UPDATE_BACKUP];

// information for local display
	int			stats[MAX_CL_STATS];	// health, etc
	float		item_gettime[32];	// cl.time of aquiring item, for blinking
	float		faceanimtime;		// use anim frame if cl.time < this

	cshift_t	cshifts[NUM_CSHIFTS];	// color shifts for damage, powerups
	cshift_t	prev_cshifts[NUM_CSHIFTS];	// and content types

// the client maintains its own idea of view angles, which are sent to the
// server each frame.  And only reset at level change and teleport times
	vec3_t		viewangles;

// the client simulates or interpolates movement to get these values
	double		time;			// this is the time value that the client
								// is rendering at.  always <= realtime
	vec3_t		simorg;
	vec3_t		simvel;
	vec3_t		simangles;

// pitch drifting vars
	float		pitchvel;
	qboolean	nodrift;
	float		driftmove;
	double		laststop;

	int			onground;		// -1 when in air
	float		crouch;			// local amount for smoothing stepups

	qboolean	paused;			// send over by server

	float		punchangle;		// temporar yview kick from weapon firing
	
	int			intermission;	// don't change view angle, full screen, etc
	int			completed_time;	// latched ffrom time at intermission start
	
/* information that is static for the entire time connected to a server */

	char		model_name[MAX_MODELS][MAX_QPATH];
	char		sound_name[MAX_SOUNDS][MAX_QPATH];

	struct model_s		*model_precache[MAX_MODELS];
	struct sfx_s		*sound_precache[MAX_SOUNDS];

	char		levelname[40];	// for display on solo scoreboard
	int			playernum;
	int			viewentity;
	float		stdver;

	// serverinfo mirrors
	int			chase;
	int			sv_cshifts;
	int			no_pogo_stick;
	int			teamplay;
	int			watervis;

// refresh related state
	struct model_s	*worldmodel;	// cl_entitites[0].model
	int			num_entities;	// stored bottom up in cl_entities array
	int			num_statics;	// stored top down in cl_entitiers

	int			cdtrack;		// cd audio

	entity_t	viewent;		// weapon model

// all player information
	player_info_t	players[MAX_CLIENTS];
} client_state_t;


/*
  cvars
*/
extern	struct cvar_s	*cl_upspeed;
extern	struct cvar_s	*cl_forwardspeed;
extern	struct cvar_s	*cl_backspeed;
extern	struct cvar_s	*cl_sidespeed;

extern	struct cvar_s	*cl_movespeedkey;

extern	struct cvar_s	*cl_yawspeed;
extern	struct cvar_s	*cl_pitchspeed;

extern	struct cvar_s	*cl_anglespeedkey;

extern	struct cvar_s	*cl_shownet;
extern	struct cvar_s	*cl_sbar;
extern	struct cvar_s	*cl_sbar_separator;
extern	struct cvar_s	*cl_hudswap;

extern	struct cvar_s	*cl_pitchdriftspeed;
extern	struct cvar_s	*lookspring;

extern	struct cvar_s	*m_pitch;
extern	struct cvar_s	*m_yaw;
extern	struct cvar_s	*m_forward;
extern	struct cvar_s	*m_side;

extern	struct cvar_s	*cl_name;

extern	struct cvar_s	*cl_model_crcs;

extern	struct cvar_s	*rate;

extern	struct cvar_s	*show_ping;
extern	struct cvar_s	*show_pl;

extern	struct cvar_s	*skin;


#define	MAX_STATIC_ENTITIES	128			// torches, etc

extern	client_state_t	cl;

// FIXME, allocate dynamically
extern	entity_state_t	cl_baselines[MAX_EDICTS];
extern	entity_t		cl_static_entities[MAX_STATIC_ENTITIES];

extern	qboolean	nomaster;
extern char	*server_version;	// version of server we connected to

extern	double		realtime;

extern struct cbuf_s *cl_cbuf;


void Cvar_Info (struct cvar_s *var);

void CL_NetGraph (void);
void CL_UpdateScreen (double realtime);

void CL_SetState (cactive_t state);

void V_ParseDamage (void);

void V_PrepBlend (void);

void CL_Cmd_ForwardToServer (void);
void CL_Cmd_Init (void);

#define RSSHOT_WIDTH 320
#define RSSHOT_HEIGHT 200

#endif // _CLIENT_H

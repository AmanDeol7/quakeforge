/*
	sv_phys.c

	@description@

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

#include "QF/console.h"
#include "QF/cvar.h"
#include "QF/sys.h"

#include "host.h"
#include "server.h"
#include "sv_progs.h"
#include "world.h"

#define sv_frametime host_frametime

/*
	pushmove objects do not obey gravity, and do not interact with each
	other or trigger fields, but block normal movement and push normal
	objects when they move.

	onground is set for toss objects when they come to a complete rest.  it
	is set for steping or walking objects

	doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
	bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
	corpses are SOLID_NOT and MOVETYPE_TOSS
	crates are SOLID_BBOX and MOVETYPE_TOSS
	walking monsters are SOLID_SLIDEBOX and MOVETYPE_STEP
	flying/floating monsters are SOLID_SLIDEBOX and MOVETYPE_FLY

	solid_edge items only clip against bsp models.
*/

cvar_t     *sv_friction;
cvar_t     *sv_gravity;
cvar_t     *sv_stopspeed;
cvar_t     *sv_maxvelocity;
cvar_t     *sv_nostep;

#define	MOVE_EPSILON	0.01

void        SV_Physics_Toss (edict_t *ent);

void
SV_CheckAllEnts (void)
{
	edict_t    *check;
	int         e;

	// see if any solid entities are inside the final position
	check = NEXT_EDICT (&sv_pr_state, sv.edicts);
	for (e = 1; e < sv.num_edicts;
		 e++, check = NEXT_EDICT (&sv_pr_state, check)) {
		if (check->free)
			continue;
		if (SVfloat (check, movetype) == MOVETYPE_PUSH
			|| SVfloat (check, movetype) == MOVETYPE_NONE
			|| SVfloat (check, movetype) == MOVETYPE_NOCLIP)
			continue;

		if (SV_TestEntityPosition (check))
			Con_Printf ("entity in invalid position\n");
	}
}

void
SV_CheckVelocity (edict_t *ent)
{
#if 0
	float       wishspeed;
#endif
	int         i;

	// bound velocity
	for (i = 0; i < 3; i++) {
		if (IS_NAN (SVvector (ent, velocity)[i])) {
			Con_Printf ("Got a NaN velocity on %s\n",
						PR_GetString (&sv_pr_state, SVstring (ent,
															  classname)));
			SVvector (ent, velocity)[i] = 0;
		}
		if (IS_NAN (SVvector (ent, origin)[i])) {
			Con_Printf ("Got a NaN origin on %s\n",
						PR_GetString (&sv_pr_state, SVstring (ent,
															  classname)));
			SVvector (ent, origin)[i] = 0;
		}
#if 1
		if (SVvector (ent, velocity)[i] > sv_maxvelocity->value)
			SVvector (ent, velocity)[i] = sv_maxvelocity->value;
		else if (SVvector (ent, velocity)[i] < -sv_maxvelocity->value)
			SVvector (ent, velocity)[i] = -sv_maxvelocity->value;
#endif
	}
#if 0
	wishspeed = Length (SVvector (ent, velocity));
	if (wishspeed > sv_maxvelocity->value) {
		VectorScale (SVvector (ent, velocity), sv_maxvelocity->value /
					 wishspeed, SVvector (ent, velocity));
	}
#endif
}

/*
  SV_RunThink

  Runs thinking code if time.  There is some play in the exact time the think
  function will be called, because it is called before any movement is done
  in a frame.  Not used for pushmove objects, because they must be exact.
  Returns false if the entity removed itself.
*/
qboolean
SV_RunThink (edict_t *ent)
{
	float       thinktime;

	do {
		thinktime = SVfloat (ent, nextthink);
		if (thinktime <= 0 || thinktime > sv.time + sv_frametime)
			return true;

		if (thinktime < sv.time)
			thinktime = sv.time;		// don't let things stay in the past.
										// it is possible to start that way
										// by a trigger with a local time.
		SVfloat (ent, nextthink) = 0;
		*sv_globals.time = thinktime;
		*sv_globals.self = EDICT_TO_PROG (&sv_pr_state, ent);
		*sv_globals.other = EDICT_TO_PROG (&sv_pr_state, sv.edicts);
		PR_ExecuteProgram (&sv_pr_state, SVfunc (ent, think));

		if (ent->free)
			return false;
	} while (0);

	return true;
}

/*
	SV_Impact

	Two entities have touched, so run their touch functions
*/
void
SV_Impact (edict_t *e1, edict_t *e2)
{
	int         old_self, old_other;

	old_self = *sv_globals.self;
	old_other = *sv_globals.other;

	*sv_globals.time = sv.time;
	if (SVfunc (e1, touch) && SVfloat (e1, solid) != SOLID_NOT) {
		*sv_globals.self = EDICT_TO_PROG (&sv_pr_state, e1);
		*sv_globals.other = EDICT_TO_PROG (&sv_pr_state, e2);
		PR_ExecuteProgram (&sv_pr_state, SVfunc (e1, touch));
	}

	if (SVfunc (e2, touch) && SVfloat (e2, solid) != SOLID_NOT) {
		*sv_globals.self = EDICT_TO_PROG (&sv_pr_state, e2);
		*sv_globals.other = EDICT_TO_PROG (&sv_pr_state, e1);
		PR_ExecuteProgram (&sv_pr_state, SVfunc (e2, touch));
	}

	*sv_globals.self = old_self;
	*sv_globals.other = old_other;
}

#define	STOP_EPSILON	0.1

/*
  ClipVelocity

  Slide off of the impacting object
  returns the blocked flags (1 = floor, 2 = step / wall)
*/
int
ClipVelocity (vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
	float       backoff, change;
	int         i, blocked;

	blocked = 0;
	if (normal[2] > 0)
		blocked |= 1;					// floor
	if (!normal[2])
		blocked |= 2;					// step

	backoff = DotProduct (in, normal) * overbounce;

	for (i = 0; i < 3; i++) {
		change = normal[i] * backoff;
		out[i] = in[i] - change;
		if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
			out[i] = 0;
	}

	return blocked;
}

#define	MAX_CLIP_PLANES	5

/*
  SV_FlyMove

  The basic solid body movement clip that slides along multiple planes
  Returns the clipflags if the velocity was modified (hit something solid)
  1 = floor
  2 = wall / step
  4 = dead stop
  If steptrace is not NULL, the trace of any vertical wall hit will be stored
*/
int
SV_FlyMove (edict_t *ent, float time, trace_t *steptrace)
{
	float       d, time_left;
	int         blocked, bumpcount, numbumps, numplanes, i, j;
	trace_t     trace;
	vec3_t      dir, end;
	vec3_t      planes[MAX_CLIP_PLANES];
	vec3_t      primal_velocity, original_velocity, new_velocity;

	numbumps = 4;

	blocked = 0;
	VectorCopy (SVvector (ent, velocity), original_velocity);
	VectorCopy (SVvector (ent, velocity), primal_velocity);
	numplanes = 0;

	time_left = time;

	for (bumpcount = 0; bumpcount < numbumps; bumpcount++) {
		if (VectorIsZero (SVvector (ent, velocity)))
			break;

		VectorMA (SVvector (ent, origin), time_left, SVvector (ent, velocity),
				  end);

		trace = SV_Move (SVvector (ent, origin), SVvector (ent, mins),
						 SVvector (ent, maxs), end, false, ent);

		if (trace.allsolid) {			// entity is trapped in another solid
			VectorCopy (vec3_origin, SVvector (ent, velocity));
			return 3;
		}

		if (trace.fraction > 0) {		// actually covered some distance
			VectorCopy (trace.endpos, SVvector (ent, origin));
			VectorCopy (SVvector (ent, velocity), original_velocity);
			numplanes = 0;
		}

		if (trace.fraction == 1)
			break;						// moved the entire distance

		if (!trace.ent)
			Sys_Error ("SV_FlyMove: !trace.ent");

		if (trace.plane.normal[2] > 0.7) {
			blocked |= 1;				// floor
			if (SVfloat (trace.ent, solid) == SOLID_BSP) {
				SVfloat (ent, flags) = (int) SVfloat (ent, flags) |
					FL_ONGROUND;
				SVentity (ent, groundentity) = EDICT_TO_PROG (&sv_pr_state,
															  trace.ent);
			}
		}
		if (!trace.plane.normal[2]) {
			blocked |= 2;				// step
			if (steptrace)
				*steptrace = trace;		// save for player extrafriction
		}

		// run the impact function
		SV_Impact (ent, trace.ent);
		if (ent->free)
			break;						// removed by the impact function

		time_left -= time_left * trace.fraction;

		// cliped to another plane
		if (numplanes >= MAX_CLIP_PLANES) {	// this shouldn't really happen
			VectorCopy (vec3_origin, SVvector (ent, velocity));
			return 3;
		}

		VectorCopy (trace.plane.normal, planes[numplanes]);
		numplanes++;

		// modify original_velocity so it parallels all of the clip planes
		for (i = 0; i < numplanes; i++) {
			ClipVelocity (original_velocity, planes[i], new_velocity, 1);
			for (j = 0; j < numplanes; j++)
				if (j != i) {
					if (DotProduct (new_velocity, planes[j]) < 0)
						break;			// not ok
				}
			if (j == numplanes)
				break;
		}

		if (i != numplanes) {			// go along this plane
			VectorCopy (new_velocity, SVvector (ent, velocity));
		} else {						// go along the crease
			if (numplanes != 2) {
				VectorCopy (vec3_origin, SVvector (ent, velocity));
				return 7;
			}
			CrossProduct (planes[0], planes[1], dir);
			d = DotProduct (dir, SVvector (ent, velocity));
			VectorScale (dir, d, SVvector (ent, velocity));
		}

		// if original velocity is against the original velocity, stop dead
		// to avoid tiny occilations in sloping corners
		if (DotProduct (SVvector (ent, velocity), primal_velocity) <= 0) {
			VectorCopy (vec3_origin, SVvector (ent, velocity));
			return blocked;
		}
	}

	return blocked;
}

void
SV_AddGravity (edict_t *ent)
{
	float       ent_gravity;

	pr_type_t  *val;

	val = GetEdictFieldValue (&sv_pr_state, ent, "gravity");
	if (val && val->float_var)
		ent_gravity = val->float_var;
	else
		ent_gravity = 1.0;
	SVvector (ent, velocity)[2] -= ent_gravity * sv_gravity->value * sv_frametime;
}

/* PUSHMOVE */

/*
	SV_PushEntity

	Does not change the entities velocity at all
*/
trace_t
SV_PushEntity (edict_t *ent, vec3_t push)
{
	trace_t     trace;
	vec3_t      end;

	VectorAdd (SVvector (ent, origin), push, end);

	if (SVfloat (ent, movetype) == MOVETYPE_FLYMISSILE)
		trace = SV_Move (SVvector (ent, origin), SVvector (ent, mins),
						 SVvector (ent, maxs), end, MOVE_MISSILE, ent);
	else if (SVfloat (ent, solid) == SOLID_TRIGGER || SVfloat (ent, solid) ==
			 SOLID_NOT)
		// only clip against bmodels
		trace = SV_Move (SVvector (ent, origin), SVvector (ent, mins),
						 SVvector (ent, maxs), end, MOVE_NOMONSTERS, ent);
	else
		trace = SV_Move (SVvector (ent, origin), SVvector (ent, mins),
						 SVvector (ent, maxs), end, MOVE_NORMAL, ent);

	VectorCopy (trace.endpos, SVvector (ent, origin));
	SV_LinkEdict (ent, true);

	if (trace.ent)
		SV_Impact (ent, trace.ent);

	return trace;
}

qboolean
SV_Push (edict_t *pusher, vec3_t move)
{
	float       solid_save;
	int         num_moved, i, e;
	edict_t    *check, *block;
	edict_t    *moved_edict[MAX_EDICTS];
	vec3_t      entorig;
	vec3_t      mins, maxs, pushorig;
	vec3_t      moved_from[MAX_EDICTS];

	VectorAdd (SVvector (pusher, absmin), move, mins);
	VectorAdd (SVvector (pusher, absmax), move, maxs);

	VectorCopy (SVvector (pusher, origin), pushorig);

	// move the pusher to it's final position
	VectorAdd (SVvector (pusher, origin), move, SVvector (pusher, origin));
	SV_LinkEdict (pusher, false);

	// see if any solid entities are inside the final position
	num_moved = 0;
	check = NEXT_EDICT (&sv_pr_state, sv.edicts);
	for (e = 1; e < sv.num_edicts;
		 e++, check = NEXT_EDICT (&sv_pr_state, check)) {
		if (check->free)
			continue;
		if (SVfloat (check, movetype) == MOVETYPE_PUSH
			|| SVfloat (check, movetype) == MOVETYPE_NONE
			|| SVfloat (check, movetype) == MOVETYPE_NOCLIP)
			continue;

		// if the entity is standing on the pusher, it will definately be moved
		if (!(((int) SVfloat (check, flags) & FL_ONGROUND)
			  && PROG_TO_EDICT (&sv_pr_state,
								SVentity (check, groundentity)) == pusher)) {
			// check is NOT standing on pusher
			if (SVvector (check, absmin)[0] >= maxs[0]
				|| SVvector (check, absmin)[1] >= maxs[1]
				|| SVvector (check, absmin)[2] >= maxs[2]
				|| SVvector (check, absmax)[0] <= mins[0]
				|| SVvector (check, absmax)[1] <= mins[1]
				|| SVvector (check, absmax)[2] <= mins[2])
				continue;

			// see if the ent's bbox is inside the pusher's final position
			if (!SV_TestEntityPosition (check))
				continue;
		}
		// remove the onground flag for non-players
		if (SVfloat (check, movetype) != MOVETYPE_WALK)
			SVfloat (check, flags) = (int) SVfloat (check, flags) &
				~FL_ONGROUND;

		VectorCopy (SVvector (check, origin), entorig);
		VectorCopy (SVvector (check, origin), moved_from[num_moved]);
		moved_edict[num_moved] = check;
		num_moved++;

		// try moving the contacted entity
		solid_save = SVfloat (pusher, solid);
		SVfloat (pusher, solid) = SOLID_NOT;
		SV_PushEntity (check, move);
		SVfloat (pusher, solid) = solid_save;

		block = SV_TestEntityPosition (check);
		if (!block) {
			continue;
		}
		// if it is still inside the pusher, block
		if (SVvector (check, mins)[0] == SVvector (check, maxs)[0]) {
			continue;
		}
		if (SVfloat (check, solid) == SOLID_NOT
			|| SVfloat (check, solid) == SOLID_TRIGGER) {	// corpse
			SVvector (check, mins)[0] = SVvector (check, mins)[1] = 0;
			VectorCopy (SVvector (check, mins), SVvector (check, maxs));
			continue;
		}

		VectorCopy (entorig, SVvector (check, origin));
		SV_LinkEdict (check, true);

		VectorCopy (pushorig, SVvector (pusher, origin));
		SV_LinkEdict (pusher, false);

		// if the pusher has a "blocked" function, call it
		// otherwise, just stay in place until the obstacle is gone
		if (SVfunc (pusher, blocked)) {
			*sv_globals.self = EDICT_TO_PROG (&sv_pr_state, pusher);
			*sv_globals.other = EDICT_TO_PROG (&sv_pr_state, check);
			PR_ExecuteProgram (&sv_pr_state, SVfunc (pusher, blocked));
		}
		// move back any entities we already moved
		for (i = 0; i < num_moved; i++) {
			VectorCopy (moved_from[i], SVvector (moved_edict[i], origin));
			SV_LinkEdict (moved_edict[i], false);
		}
		return false;
	}
	return true;
}

void
SV_PushMove (edict_t *pusher, float movetime)
{
	vec3_t      move;

	if (VectorIsZero (SVvector (pusher, velocity))) {
		SVfloat (pusher, ltime) += movetime;
		return;
	}

	VectorScale (SVvector (pusher, velocity), movetime, move);

	if (SV_Push (pusher, move))
		SVfloat (pusher, ltime) += movetime;
}

void
SV_Physics_Pusher (edict_t *ent)
{
	float       movetime, oldltime, thinktime;

	oldltime = SVfloat (ent, ltime);

	thinktime = SVfloat (ent, nextthink);
	if (thinktime < SVfloat (ent, ltime) + sv_frametime) {
		movetime = thinktime - SVfloat (ent, ltime);
		if (movetime < 0)
			movetime = 0;
	} else
		movetime = sv_frametime;

	if (movetime) {
		SV_PushMove (ent, movetime);	// advances SVfloat (ent, ltime) if not
										// blocked
	}

	if (thinktime > oldltime && thinktime <= SVfloat (ent, ltime)) {
		SVfloat (ent, nextthink) = 0;
		*sv_globals.time = sv.time;
		*sv_globals.self = EDICT_TO_PROG (&sv_pr_state, ent);
		*sv_globals.other = EDICT_TO_PROG (&sv_pr_state, sv.edicts);
		PR_ExecuteProgram (&sv_pr_state, SVfunc (ent, think));
		if (ent->free)
			return;
	}

}

/*
	SV_Physics_None

	Non moving objects can only think
*/
void
SV_Physics_None (edict_t *ent)
{
	// regular thinking
	SV_RunThink (ent);
	SV_LinkEdict (ent, false);
}

/*
	SV_Physics_Noclip

	A moving object that doesn't obey physics
*/
void
SV_Physics_Noclip (edict_t *ent)
{
	// regular thinking
	if (!SV_RunThink (ent))
		return;

	VectorMA (SVvector (ent, angles), sv_frametime,
			  SVvector (ent, avelocity), SVvector (ent, angles));
	VectorMA (SVvector (ent, origin), sv_frametime, SVvector (ent, velocity),
			  SVvector (ent, origin));

	SV_LinkEdict (ent, false);
}

/* TOSS / BOUNCE */

void
SV_CheckWaterTransition (edict_t *ent)
{
	int         cont;

	cont = SV_PointContents (SVvector (ent, origin));

	if (!SVfloat (ent, watertype)) {			// just spawned here
		SVfloat (ent, watertype) = cont;
		SVfloat (ent, waterlevel) = 1;
		return;
	}

	if (cont <= CONTENTS_WATER) {
		if (SVfloat (ent, watertype) == CONTENTS_EMPTY) {
			// just crossed into water
			SV_StartSound (ent, 0, "misc/h2ohit1.wav", 255, 1);
		}
		SVfloat (ent, watertype) = cont;
		SVfloat (ent, waterlevel) = 1;
	} else {
		if (SVfloat (ent, watertype) != CONTENTS_EMPTY) {
			// just crossed into water
			SV_StartSound (ent, 0, "misc/h2ohit1.wav", 255, 1);
		}
		SVfloat (ent, watertype) = CONTENTS_EMPTY;
		SVfloat (ent, waterlevel) = cont;
	}
}

/*
	SV_Physics_Toss

	Toss, bounce, and fly movement.  When onground, do nothing.
*/
void
SV_Physics_Toss (edict_t *ent)
{
	float       backoff;
	trace_t     trace;
	vec3_t      move;

	// regular thinking
	if (!SV_RunThink (ent))
		return;

	// if onground, return without moving
	if (((int) SVfloat (ent, flags) & FL_ONGROUND))
		return;

	SV_CheckVelocity (ent);

	// add gravity
	if (SVfloat (ent, movetype) != MOVETYPE_FLY
		&& SVfloat (ent, movetype) != MOVETYPE_FLYMISSILE)
		SV_AddGravity (ent);

	// move angles
	VectorMA (SVvector (ent, angles), sv_frametime,
			  SVvector (ent, avelocity), SVvector (ent, angles));

	// move origin
	VectorScale (SVvector (ent, velocity), sv_frametime, move);
	trace = SV_PushEntity (ent, move);
	if (trace.fraction == 1)
		return;
	if (ent->free)
		return;

	if (SVfloat (ent, movetype) == MOVETYPE_BOUNCE)
		backoff = 1.5;
	else
		backoff = 1;

	ClipVelocity (SVvector (ent, velocity), trace.plane.normal,
				  SVvector (ent, velocity), backoff);

	// stop if on ground
	if (trace.plane.normal[2] > 0.7) {
		if (SVvector (ent, velocity)[2] < 60 || SVfloat (ent, movetype) !=
			MOVETYPE_BOUNCE) {
			SVfloat (ent, flags) = (int) SVfloat (ent, flags) | FL_ONGROUND;
			SVentity (ent, groundentity) = EDICT_TO_PROG (&sv_pr_state,
														  trace.ent);
			VectorCopy (vec3_origin, SVvector (ent, velocity));
			VectorCopy (vec3_origin, SVvector (ent, avelocity));
		}
	}
	// check for in water
	SV_CheckWaterTransition (ent);
}

/* STEPPING MOVEMENT */

/*
	SV_Physics_Step

	Monsters freefall when they don't have a ground entity, otherwise
	all movement is done with discrete steps.

	This is also used for objects that have become still on the ground, but
	will fall if the floor is pulled out from under them.
*/
void
SV_Physics_Step (edict_t *ent)
{
	qboolean    hitsound;

	// freefall if not on ground
	if (!((int) SVfloat (ent, flags) & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
		if (SVvector (ent, velocity)[2] < sv_gravity->value * -0.1)
			hitsound = true;
		else
			hitsound = false;

		SV_AddGravity (ent);
		SV_CheckVelocity (ent);
		SV_FlyMove (ent, sv_frametime, NULL);
		SV_LinkEdict (ent, true);

		if ((int) SVfloat (ent, flags) & FL_ONGROUND)	// just hit ground
		{
			if (hitsound)
				SV_StartSound (ent, 0, "demon/dland2.wav", 255, 1);
		}
	}
	// regular thinking
	SV_RunThink (ent);

	SV_CheckWaterTransition (ent);
}

void
SV_ProgStartFrame (void)
{
	// let the progs know that a new frame has started
	*sv_globals.self = EDICT_TO_PROG (&sv_pr_state, sv.edicts);
	*sv_globals.other = EDICT_TO_PROG (&sv_pr_state, sv.edicts);
	*sv_globals.time = sv.time;
	PR_ExecuteProgram (&sv_pr_state, sv_funcs.StartFrame);
}

void
SV_RunEntity (edict_t *ent)
{
	switch ((int) SVfloat (ent, movetype)) {
		case MOVETYPE_PUSH:
			SV_Physics_Pusher (ent);
			break;
		case MOVETYPE_NONE:
			SV_Physics_None (ent);
			break;
		case MOVETYPE_NOCLIP:
			SV_Physics_Noclip (ent);
			break;
		case MOVETYPE_STEP:
			SV_Physics_Step (ent);
			break;
		case MOVETYPE_TOSS:
		case MOVETYPE_BOUNCE:
		case MOVETYPE_FLY:
		case MOVETYPE_FLYMISSILE:
			SV_Physics_Toss (ent);
			break;
		default:
			Sys_Error ("SV_Physics: bad movetype %i",
					   (int) SVfloat (ent, movetype));
	}
}

void
SV_Physics (void)
{
	edict_t    *ent;
	int         i;

	SV_ProgStartFrame ();

	// treat each object in turn
	// even the world gets a chance to think
	ent = sv.edicts;
	for (i = 0; i < sv.num_edicts; i++, ent = NEXT_EDICT (&sv_pr_state, ent)) {
		if (ent->free)
			continue;

		if (*sv_globals.force_retouch) {
			SV_LinkEdict (ent, true);	// force retouch even for stationary
		}

		if (i > 0 && i <= svs.maxclients) {
			SV_Physics_Client (ent, i);
			continue;
		}
		SV_RunEntity (ent);
	}

	if (*sv_globals.force_retouch)
		(*sv_globals.force_retouch)--;

	sv.time += sv_frametime;
}

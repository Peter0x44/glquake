/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// cl_main.c  -- client main loop

#include "quakedef.h"

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

// these two are not intended to be set directly
cvar_t	waypoint_mode = {"waypoint_mode", "0", false};// waypoint mode active
cvar_t	autosave_waypoint = {"autosave_waypoint", "0", false};// waypoint mode active
cvar_t	cl_name = {"_cl_name", "player", true};
cvar_t	cl_color = {"_cl_color", "0", true};

cvar_t	cl_shownet = {"cl_shownet","0"};	// can be 0, 1, or 2
cvar_t	cl_nolerp = {"cl_nolerp","0"};

cvar_t	lookspring = {"lookspring","0", true};
cvar_t	lookstrafe = {"lookstrafe","0", true};
cvar_t	sensitivity = {"sensitivity","8", true};
cvar_t	in_tolerance = {"tolerance","0.25", true};
cvar_t	in_acceleration = {"acceleration","1.0", true};
cvar_t	in_analog_strafe = {"in_analog_strafe", "0", true};

cvar_t	m_pitch = {"m_pitch","-0.22", true};
cvar_t	m_yaw = {"m_yaw","0", true};
cvar_t	m_forward = {"m_forward","0", true};
cvar_t	m_side = {"m_side","0", true};


client_static_t	cls;
client_state_t	cl;
// FIXME: put these on hunk?

modelindex_t		cl_modelindex[NUM_MODELINDEX]; 
char				*cl_modelnames[NUM_MODELINDEX];
												   
efrag_t			cl_efrags[MAX_EFRAGS];
entity_t		cl_entities[MAX_EDICTS];
entity_t		cl_static_entities[MAX_STATIC_ENTITIES];
lightstyle_t	cl_lightstyle[MAX_LIGHTSTYLES];
dlight_t		cl_dlights[MAX_DLIGHTS];

int				cl_numvisedicts;
entity_t		*cl_visedicts[MAX_VISEDICTS];
int 			cl_numstaticbrushmodels;
entity_t 		*cl_staticbrushmodels[MAX_VISEDICTS];

/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState (void)
{
	int			i;

	if (!sv.active)
		Host_ClearMemory ();

// wipe the entire cl structure
	memset (&cl, 0, sizeof(cl));

	SZ_Clear (&cls.message);

// clear other arrays	
	memset (cl_efrags, 0, sizeof(cl_efrags));
	memset (cl_entities, 0, sizeof(cl_entities));
	memset (cl_dlights, 0, sizeof(cl_dlights));
	memset (cl_lightstyle, 0, sizeof(cl_lightstyle));
	memset (cl_temp_entities, 0, sizeof(cl_temp_entities));
	memset (cl_beams, 0, sizeof(cl_beams));

//
// allocate the efrags and chain together into a free list
//
	cl.free_efrags = cl_efrags;
	for (i=0 ; i<MAX_EFRAGS-1 ; i++)
		cl.free_efrags[i].entnext = &cl.free_efrags[i+1];
	cl.free_efrags[i].entnext = NULL;
}

/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect (void)
{
// stop sounds (especially looping!)
	S_StopAllSounds (true);
	
// bring the console down and fade the colors back to normal
//	SCR_BringDownConsole ();

// if running a local server, shut it down
	if (cls.demoplayback)
		CL_StopPlayback ();
	else if (cls.state == ca_connected)
	{
		if (cls.demorecording)
			CL_Stop_f ();

		Con_DPrintf ("Sending clc_disconnect\n");
		SZ_Clear (&cls.message);
		MSG_WriteByte (&cls.message, clc_disconnect);
		NET_SendUnreliableMessage (cls.netcon, &cls.message);
		SZ_Clear (&cls.message);
		NET_Close (cls.netcon);

		cls.state = ca_disconnected;
		if (sv.active)
			Host_ShutdownServer(false);
	}

	cls.demoplayback = cls.timedemo = false;
	cls.signon = 0;
}

void CL_Disconnect_f (void)
{
	CL_Disconnect ();
	if (sv.active)
		Host_ShutdownServer (false);
}




/*
=====================
CL_EstablishConnection

Host should be either "local" or a net address to be passed on
=====================
*/
void CL_EstablishConnection (char *host)
{
	if (cls.state == ca_dedicated)
		return;

	if (cls.demoplayback)
		return;

	CL_Disconnect ();

	cls.netcon = NET_Connect (host);
	if (!cls.netcon)
		Host_Error ("CL_Connect: connect failed\n");
	Con_DPrintf ("CL_EstablishConnection: connected to %s\n", host);
	
	cls.demonum = -1;			// not in the demo loop now
	cls.state = ca_connected;
	cls.signon = 0;				// need all the signon messages before playing
}

/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply (void)
{
	char 	str[8192];

Con_DPrintf ("CL_SignonReply: %i\n", cls.signon);

	switch (cls.signon)
	{
	case 1:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "prespawn");
		break;
		
	case 2:		
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("name \"%s\"\n", cl_name.string));
	
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va("color %i %i\n", ((int)cl_color.value)>>4, ((int)cl_color.value)&15));
	
		MSG_WriteByte (&cls.message, clc_stringcmd);
		sprintf (str, "spawn %s", cls.spawnparms);
		MSG_WriteString (&cls.message, str);
		break;
		
	case 3:	
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "begin");
		Cache_Report ();		// print remaining memory
		break;
		
	case 4:
		SCR_EndLoadingPlaque ();		// allow normal screen updates
		break;
	}
}

/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void CL_NextDemo (void)
{
	char	str[1024];

	if (cls.demonum == -1)
		return;		// don't play demos

	SCR_BeginLoadingPlaque ();

	if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS)
	{
		cls.demonum = 0;
		if (!cls.demos[cls.demonum][0])
		{
			Con_Printf ("No demos listed with startdemos\n");
			cls.demonum = -1;
			return;
		}
	}

	sprintf (str,"playdemo %s\n", cls.demos[cls.demonum]);
	Cbuf_InsertText (str);
	cls.demonum++;
}

/*
==============
CL_PrintEntities_f
==============
*/
void CL_PrintEntities_f (void)
{
	entity_t	*ent;
	int			i;
	
	for (i=0,ent=cl_entities ; i<cl.num_entities ; i++,ent++)
	{
		Con_Printf ("%3i:",i);
		if (!ent->model)
		{
			Con_Printf ("EMPTY\n");
			continue;
		}
		Con_Printf ("%s:%2i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n"
		,ent->model->name,ent->frame, ent->origin[0], ent->origin[1], ent->origin[2], ent->angles[0], ent->angles[1], ent->angles[2]);
	}
}


/*
===============
SetPal

Debugging tool, just flashes the screen
===============
*/
void SetPal (int i)
{
#if 0
	static int old;
	byte	pal[768];
	int		c;
	
	if (i == old)
		return;
	old = i;

	if (i==0)
		VID_SetPalette (host_basepal);
	else if (i==1)
	{
		for (c=0 ; c<768 ; c+=3)
		{
			pal[c] = 0;
			pal[c+1] = 255;
			pal[c+2] = 0;
		}
		VID_SetPalette (pal);
	}
	else
	{
		for (c=0 ; c<768 ; c+=3)
		{
			pal[c] = 0;
			pal[c+1] = 0;
			pal[c+2] = 255;
		}
		VID_SetPalette (pal);
	}
#endif
}

/*
===============
CL_AllocDlight

===============
*/
dlight_t *CL_AllocDlight (int key)
{
	int		i;
	dlight_t	*dl;

// first look for an exact key match
	if (key)
	{
		dl = cl_dlights;
		for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
		{
			if (dl->key == key)
			{
				memset (dl, 0, sizeof(*dl));
				dl->key = key;
				dl->color[0] = dl->color[1] = dl->color[2] = 1; //johnfitz -- lit support via lordhavoc
				return dl;
			}
		}
	}

// then look for anything else
	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time)
		{
			memset (dl, 0, sizeof(*dl));
			dl->key = key;
			dl->color[0] = dl->color[1] = dl->color[2] = 1; //johnfitz -- lit support via lordhavoc
			return dl;
		}
	}

	dl = &cl_dlights[0];
	memset (dl, 0, sizeof(*dl));
	dl->key = key;
	dl->color[0] = dl->color[1] = dl->color[2] = 1; //johnfitz -- lit support via lordhavoc
	return dl;
}

void CL_NewDlight (int key, vec3_t origin, float radius, float time, int type)
{
	dlight_t	*dl;

	dl = CL_AllocDlight (key);
	VectorCopy (origin, dl->origin);
	dl->radius = radius;
	dl->die = cl.time + time;
	dl->type = type;

}

dlighttype_t SetDlightColor (float f, dlighttype_t def, qboolean random)
{
	dlighttype_t	colors[NUM_DLIGHTTYPES-4] = {lt_red, lt_blue, lt_redblue, lt_green};

	if ((int)f == 1)
		return lt_red;
	else if ((int)f == 2)
		return lt_blue;
	else if ((int)f == 3)
		return lt_redblue;
	else if ((int)f == 4)
		return lt_green;
	else if (((int)f == NUM_DLIGHTTYPES - 3) && random)
		return colors[rand()%(NUM_DLIGHTTYPES-4)];
	else
		return def;
}

/*
===============
CL_DecayLights

===============
*/
void CL_DecayLights (void)
{
	int			i;
	dlight_t	*dl;
	float		time;
	
	time = cl.time - cl.oldtime;

	dl = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
	{
		if (dl->die < cl.time || !dl->radius)
			continue;
		
		dl->radius -= time*dl->decay;
		if (dl->radius < 0)
			dl->radius = 0;
	}
}


/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
float	CL_LerpPoint (void)
{
	float	f, frac;

	f = cl.mtime[0] - cl.mtime[1];
	
	if (!f || cl_nolerp.value || cls.timedemo || sv.active)
	{
		cl.ctime = cl.time = cl.mtime[0];
		return 1;
	}
		
	if (f > 0.1)
	{	// dropped packet, or start of demo
		cl.mtime[1] = cl.mtime[0] - 0.1;
		f = 0.1;
	}
	frac = (cl.ctime - cl.mtime[1]) / f;
//Con_Printf ("frac: %f\n",frac);
	if (frac < 0)
	{
		if (frac < -0.01)
		{
SetPal(1);
			cl.ctime = cl.time = cl.mtime[1];
//				Con_Printf ("low frac\n");
		}
		frac = 0;
	}
	else if (frac > 1)
	{
		if (frac > 1.01)
		{
SetPal(2);
			cl.ctime = cl.time = cl.mtime[0];
//				Con_Printf ("high frac\n");
		}
		frac = 1;
	}
	else
		SetPal(0);
		
	return frac;
}


/*
===============
CL_RelinkEntities
===============
*/
void CL_RelinkEntities (void)
{
	entity_t	*ent;
	int			i, j;
	float		frac, f, d;
	vec3_t		delta;
	float		bobjrotate;
	vec3_t		oldorg;
	dlight_t	*dl;

// determine partial update time	
	frac = CL_LerpPoint ();

	cl_numvisedicts = 0;
	cl_numstaticbrushmodels = 0;

//
// interpolate player info
//
	for (i=0 ; i<3 ; i++)
		cl.velocity[i] = cl.mvelocity[1][i] + 
			frac * (cl.mvelocity[0][i] - cl.mvelocity[1][i]);

	if (cls.demoplayback)
	{
	// interpolate the angles	
		for (j=0 ; j<3 ; j++)
		{
			d = cl.mviewangles[0][j] - cl.mviewangles[1][j];
			if (d > 180)
				d -= 360;
			else if (d < -180)
				d += 360;
			cl.viewangles[j] = cl.mviewangles[1][j] + frac*d;
		}
	}
	
	bobjrotate = anglemod(100*cl.time);
	
// start on the entity after the world
	for (i=1,ent=cl_entities+1 ; i<cl.num_entities ; i++,ent++)
	{
		if (!ent->model)
		{	// empty slot
			if (ent->forcelink)
				R_RemoveEfrags (ent);	// just became empty
			continue;
		}

// if the object wasn't included in the last packet, remove it
		if (ent->msgtime != cl.mtime[0])
		{
			ent->model = NULL;
			continue;
		}

		// shpuld: if brush model is at 0 with no angle changes, we can draw it with world
		if (ent->model->type == mod_brush && cl_numstaticbrushmodels < MAX_VISEDICTS)
		{
			if (ent->msg_origins[0][0] == 0 &&
				ent->msg_origins[0][1] == 0 &&
				ent->msg_origins[0][2] == 0 &&
				ent->msg_angles[0][0] == 0 &&
				ent->msg_angles[0][1] == 0 &&
				ent->msg_angles[0][2] == 0) {
				cl_staticbrushmodels[cl_numstaticbrushmodels] = ent;
				cl_numstaticbrushmodels++;
				continue;
			}
		}

		VectorCopy (ent->origin, oldorg);

		if (ent->forcelink)
		{	// the entity was not updated in the last message
			// so move to the final spot
			VectorCopy (ent->msg_origins[0], ent->origin);
			VectorCopy (ent->msg_angles[0], ent->angles);
		}
		else
		{	// if the delta is large, assume a teleport and don't lerp
			f = frac;
			for (j=0 ; j<3 ; j++)
			{
				delta[j] = ent->msg_origins[0][j] - ent->msg_origins[1][j];
				if (delta[j] > 100 || delta[j] < -100)
					f = 1;		// assume a teleportation, not a motion
			}

		// interpolate the origin and angles
			for (j=0 ; j<3 ; j++)
			{
				ent->origin[j] = ent->msg_origins[1][j] + f*delta[j];

				d = ent->msg_angles[0][j] - ent->msg_angles[1][j];
				if (d > 180)
					d -= 360;
				else if (d < -180)
					d += 360;
				ent->angles[j] = ent->msg_angles[1][j] + f*d;
			}
			
		}

// rotate binary objects locally
		if (ent->model->flags & EF_ROTATE)
			ent->angles[1] = bobjrotate;

		if (ent->effects & EF_MUZZLEFLASH)
		{
			if (i == cl.viewentity && qmb_initialized && r_part_muzzleflash.value)
			{
				vec3_t		start, smokeorg, v_forward, v_right, v_up;
				vec3_t tempangles;
				float forward_offset, up_offset, right_offset;

				VectorAdd(cl.viewangles,CWeaponRot,tempangles);
				VectorAdd(tempangles,cl.gun_kick,tempangles);


				AngleVectors (tempangles, v_forward, v_right, v_up);
				VectorCopy (cl_entities[cl.viewentity].origin, smokeorg);
				smokeorg[2] += 32;
				VectorCopy(smokeorg,start);

				right_offset	 = sv_player->v.Flash_Offset[0];
				up_offset		 = sv_player->v.Flash_Offset[1];
				forward_offset 	 = sv_player->v.Flash_Offset[2];
				
				right_offset	= right_offset/1000;
				up_offset		= up_offset/1000;
				forward_offset  = forward_offset/1000;
				
				VectorMA (start, forward_offset, v_forward ,smokeorg);
				VectorMA (smokeorg, up_offset, v_up ,smokeorg);
				VectorMA (smokeorg, right_offset, v_right ,smokeorg);
				VectorAdd(smokeorg,CWeaponOffset,smokeorg);

				if (sv_player->v.weapon != W_RAY && sv_player->v.weapon != W_PORTER) {
					QMB_MuzzleFlash (smokeorg);
				} else {
					QMB_RayFlash(smokeorg, sv_player->v.weapon);
				}
			}
		}

		if (ent->effects & EF_BLUELIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->die = cl.time + 0.001;
			dl->radius = 100;
			dl->color[0] = 0.25;
			dl->color[1] = 0.25;
			dl->color[2] = 2;
		}

		if (ent->effects & EF_REDLIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->die = cl.time + 0.001;
			dl->radius = 100;
			dl->color[0] = 2;
			dl->color[1] = 0.25;
			dl->color[2] = 0.25;
		}

		if (ent->effects & EF_ORANGELIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->die = cl.time + 0.001;
			dl->radius = 100;
			dl->color[0] = 2;
			dl->color[1] = 1;
			dl->color[2] = 0;
		}

		if (ent->effects & EF_GREENLIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->die = cl.time + 0.001;
			dl->radius = 100;
			dl->color[0] = 0.25;
			dl->color[1] = 2;
			dl->color[2] = 0.25;
		}
		
		if (ent->effects & EF_PURPLELIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin,  dl->origin);
			dl->die = cl.time + 0.001;
			dl->radius = 100;
			dl->color[0] = 2;
			dl->color[1] = 0.25;
			dl->color[2] = 2;
		}

		if (ent->effects & EF_CYANLIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->die = cl.time + 0.001;
			dl->radius = 100;
			dl->color[0] = 0.765;
			dl->color[1] = 1.40;
			dl->color[2] = 1.95;
		}

		if (ent->effects & EF_PINKLIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->die = cl.time + 0.001;
			dl->radius = 100;
			dl->color[0] = 2.37;
			dl->color[1] = 1.35;
			dl->color[2] = 1.35;
		}

		if (ent->effects & EF_LIMELIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->die = cl.time + 0.001;
			dl->radius = 100;
			dl->color[0] = 1;
			dl->color[1] = 2;
			dl->color[2] = 1;
		}

		if (ent->effects & EF_YELLOWLIGHT)
		{
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->die = cl.time + 0.001;
			dl->radius = 100;
			dl->color[0] = 2;
			dl->color[1] = 2;
			dl->color[2] = 1;
		}

		if (ent->effects & EF_RAYGREEN)
		{
			R_RocketTrail (oldorg, ent->origin, 12);
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 25;
			dl->die = cl.time + 0.01;
	        dl->color[0] = 0;
			dl->color[1] = 255;
			dl->color[2] = 0;
	        dl->type = SetDlightColor (2, lt_rocket, true);
		}

		if (ent->effects & EF_RAYRED)
		{
			R_RocketTrail (oldorg, ent->origin, 13);
			dl = CL_AllocDlight (i);
			VectorCopy (ent->origin, dl->origin);
			dl->radius = 25;
			dl->die = cl.time + 0.01;
	        dl->color[0] = 255;
			dl->color[1] = 0;
			dl->color[2] = 0;
	        dl->type = SetDlightColor (2, lt_rocket, true);
		}

		if (!strcmp(ent->model->name, "progs/flame2.mdl"))
		{
			if (qmb_initialized && r_part_flames.value)
			{
				//QMB_BigTorchFlame (ent->origin);
				if (qmb_initialized && r_part_trails.value)
					R_RocketTrail (oldorg, ent->origin, LAVA_TRAIL);
			}
		}

		if ((!strcmp(ent->model->name, "progs/s_spike.mdl"))||(!strcmp(ent->model->name, "progs/spike.mdl")))
		{
			if (qmb_initialized && r_part_trails.value)
			{
				R_RocketTrail (oldorg, ent->origin, NAIL_TRAIL);
			}
		}

        if (ent->model->flags)
		{
			if (ent->model->flags & EF_GIB)
				R_RocketTrail (oldorg, ent->origin, 2);
			else if (ent->model->flags & EF_ZOMGIB)
				R_RocketTrail (oldorg, ent->origin, 4);
			else if (ent->model->flags & EF_TRACER)
				R_RocketTrail (oldorg, ent->origin, 3);
			else if (ent->model->flags & EF_TRACER2)
				R_RocketTrail (oldorg, ent->origin, 5);
			else if (ent->model->flags & EF_ROCKET)
			{
				R_RocketTrail (oldorg, ent->origin, 0);
				dl = CL_AllocDlight (i);
				VectorCopy (ent->origin, dl->origin);
				dl->radius = 200;
				dl->die = cl.time + 0.01;
	            dl->color[0] = 0.2;
				dl->color[1] = 0.1;
				dl->color[2] = 0.5;
	            dl->type = SetDlightColor (2, lt_rocket, true);
			}
			else if (ent->model->flags & EF_GRENADE)
				R_RocketTrail (oldorg, ent->origin, 1);
			else if (ent->model->flags & EF_TRACER3)
				R_RocketTrail (oldorg, ent->origin, 6);
		}

		// Tomaz - QC Glow Begin
        if (ISLMPOINT(ent))
        {
	        dl = CL_AllocDlight (i);
	        VectorCopy (ent->origin, dl->origin);
	        dl->radius = 150;
	        dl->die = cl.time + 0.001;
	        dl->color[0] = ent->rendercolor[0];
	        dl->color[1] = ent->rendercolor[1];
	        dl->color[2] = ent->rendercolor[2];
        }
		// Tomaz - QC Glow End


		ent->forcelink = false;

		if (i == cl.viewentity && !chase_active.value)
			continue;

#ifdef QUAKE2
		if ( ent->effects & EF_NODRAW )
			continue;
#endif
		if (cl_numvisedicts < MAX_VISEDICTS)
		{
			cl_visedicts[cl_numvisedicts] = ent;
			cl_numvisedicts++;
		}
	}

}


/*
===============
CL_ReadFromServer

Read all incoming data from the server
===============
*/
int CL_ReadFromServer (void)
{
	int		ret;

	cl.oldtime = cl.time;
	cl.time += host_frametime;
	
	do
	{
		ret = CL_GetMessage ();
		if (ret == -1)
			Host_Error ("CL_ReadFromServer: lost server connection");
		if (!ret)
			break;
		
		cl.last_received_message = realtime;
		CL_ParseServerMessage ();
	} while (ret && cls.state == ca_connected);
	
	if (cl_shownet.value)
		Con_Printf ("\n");

	CL_RelinkEntities ();
	CL_UpdateTEnts ();

//
// bring the links up to date
//
	return 0;
}

/*
=================
CL_SendCmd
=================
*/
void CL_SendCmd (void)
{
	usercmd_t		cmd;

	if (cls.state != ca_connected)
		return;

	if (cls.signon == SIGNONS)
	{
	// get basic movement from keyboard
		CL_BaseMove (&cmd);
	
	// allow mice or other external controllers to add to the move
		IN_Move (&cmd);
	
	// send the unreliable message
		CL_SendMove (&cmd);
	
	}

	if (cls.demoplayback)
	{
		SZ_Clear (&cls.message);
		return;
	}
	
// send the reliable message
	if (!cls.message.cursize)
		return;		// no message at all
	
	if (!NET_CanSendMessage (cls.netcon))
	{
		Con_DPrintf ("CL_WriteToServer: can't send\n");
		return;
	}

	if (NET_SendMessage (cls.netcon, &cls.message) == -1)
		Host_Error ("CL_WriteToServer: lost server connection");

	SZ_Clear (&cls.message);
}

/*
=================
CL_Init
=================
*/
void CL_Init (void)
{	
	SZ_Alloc (&cls.message, 1024);

	CL_InitInput ();
	CL_InitTEnts ();
	
//
// register our commands
//
	Cvar_RegisterVariable (&cl_name);
	Cvar_RegisterVariable (&cl_color);
	Cvar_RegisterVariable (&cl_upspeed);
	//Cvar_RegisterVariable (&cl_backspeed);
	//Cvar_RegisterVariable (&cl_forwardspeed);
	Cvar_RegisterVariable (&cl_movespeedkey);
	Cvar_RegisterVariable (&cl_yawspeed);
	Cvar_RegisterVariable (&cl_pitchspeed);
	Cvar_RegisterVariable (&cl_anglespeedkey);
	Cvar_RegisterVariable (&cl_shownet);
	Cvar_RegisterVariable (&cl_nolerp);
	Cvar_RegisterVariable (&lookspring);
	Cvar_RegisterVariable (&lookstrafe);
	Cvar_RegisterVariable (&sensitivity);
	Cvar_RegisterVariable (&in_tolerance);
	Cvar_RegisterVariable (&in_acceleration);
	Cvar_RegisterVariable (&in_aimassist);

	Cvar_RegisterVariable (&m_pitch);
	Cvar_RegisterVariable (&m_yaw);
	Cvar_RegisterVariable (&m_forward);
	Cvar_RegisterVariable (&m_side);
	Cvar_RegisterVariable (&in_mlook);

//	Cvar_RegisterVariable (&cl_autofire);
	
	Cmd_AddCommand ("entities", CL_PrintEntities_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("record", CL_Record_f);
	Cmd_AddCommand ("stop", CL_Stop_f);
	Cmd_AddCommand ("playdemo", CL_PlayDemo_f);
	Cmd_AddCommand ("timedemo", CL_TimeDemo_f);
}


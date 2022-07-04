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

// screen.c -- master for refresh, status bar, console, chat, notify, etc

#include "quakedef.h"

/*

background clear
rendering
turtle/net/ram icons
sbar
centerprint / slow centerprint
notify lines
intermission / finale overlay
loading plaque
console
menu

required background clears
required update regions


syncronous draw mode or async
One off screen buffer, with updates either copied or xblited
Need to double buffer?


async draw will require the refresh area to be cleared, because it will be
xblited, but sync draw can just ignore it.

sync
draw

CenterPrint ()
SlowPrint ()
Screen_Update ();
Con_Printf ();

net 
turn off messages option

the refresh is allways rendered, unless the console is full screen


console is:
	notify lines
	half
	full
	

*/


int			glx, gly, glwidth, glheight;

// only the refresh window will be updated unless these variables are flagged 
int			scr_copytop;
int			scr_copyeverything;

float		scr_con_current;
float		scr_conlines;		// lines of console to display

float		oldscreensize, oldfov;

cvar_t		scr_viewsize = {"viewsize","100", true};
cvar_t		scr_fov = {"fov","80"};	// 10 - 170
cvar_t		scr_conspeed = {"scr_conspeed","300"};
cvar_t		scr_centertime = {"scr_centertime","2"};
cvar_t		scr_showram = {"showram","1"};
cvar_t		scr_showturtle = {"showturtle","0"};
cvar_t		scr_showpause = {"showpause","1"};
cvar_t		scr_printspeed = {"scr_printspeed","8"};
cvar_t 		scr_showfps = {"scr_showfps", "0"};
cvar_t		scr_loadscreen = {"scr_loadscreen","1"};
cvar_t		gl_triplebuffer = {"gl_triplebuffer", "1", true };

extern	cvar_t	crosshair;

qboolean	scr_initialized;		// ready to draw

qpic_t      *hitmark;
qpic_t 		*lscreen;

int			scr_fullupdate;

int			loadingScreen;
int			ShowBlslogo;

qboolean 	loadscreeninit;

char* 		loadname2;
char* 		loadnamespec;

int			clearconsole;
int			clearnotify;

int			sb_lines;

viddef_t	vid;				// global video state

vrect_t		scr_vrect;

qboolean	scr_disabled_for_loading;
qboolean	scr_drawloading;
float		scr_disabled_time;

qboolean	block_drawing;

void SCR_ScreenShot_f (void);

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

char		scr_centerstring[1024];
float		scr_centertime_start;	// for slow victory printing
float		scr_centertime_off;
int			scr_center_lines;
int			scr_erase_lines;
int			scr_erase_center;

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint (char *str)
{
	strncpy (scr_centerstring, str, sizeof(scr_centerstring)-1);
	scr_centertime_off = scr_centertime.value;
	scr_centertime_start = cl.time;

// count the number of lines for centering
	scr_center_lines = 1;
	while (*str)
	{
		if (*str == '\n')
			scr_center_lines++;
		str++;
	}
}


void SCR_DrawCenterString (void)
{
	char	*start;
	int		l;
	int		j;
	int		x, y;
	int		remaining;

// the finale prints the characters one at a time
	if (cl.intermission)
		remaining = scr_printspeed.value * (cl.time - scr_centertime_start);
	else
		remaining = 9999;

	scr_erase_center = 0;
	start = scr_centerstring;

	if (scr_center_lines <= 4)
		y = vid.height*0.35;
	else
		y = 48;

	do	
	{
	// scan the width of the line
		for (l=0 ; l<40 ; l++)
			if (start[l] == '\n' || !start[l])
				break;
		x = (vid.width - l*8)/2;
		for (j=0 ; j<l ; j++, x+=8)
		{
			Draw_Character (x, y, start[j]);	
			if (!remaining--)
				return;
		}
			
		y += 8;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;
		start++;		// skip the \n
	} while (1);
}

void SCR_CheckDrawCenterString (void)
{
	scr_copytop = 1;
	if (scr_center_lines > scr_erase_lines)
		scr_erase_lines = scr_center_lines;

	scr_centertime_off -= host_frametime;
	
	if (scr_centertime_off <= 0 && !cl.intermission)
		return;
	if (key_dest != key_game)
		return;

	SCR_DrawCenterString ();
}

/*
===============================================================================

Press somthing printing

===============================================================================
*/

char		scr_usestring[1024];
float		scr_usetime_off = 0.0f;
int			button_pic_x;
extern qpic_t 		*b_abutton;
extern qpic_t 		*b_bbutton;
extern qpic_t 		*b_ybutton;
extern qpic_t 		*b_xbutton;
extern qpic_t 		*b_left;
extern qpic_t 		*b_right;
extern qpic_t 		*b_up;
extern qpic_t 		*b_down;
extern qpic_t 		*b_lt;
extern qpic_t 		*b_rt;
extern qpic_t 		*b_start;
extern qpic_t 		*b_select;
extern qpic_t		*b_zlt;
extern qpic_t 		*b_zrt;

/*
==============
SCR_UsePrint

Similiar to above, but will also print the current button for the action.
==============
*/

qpic_t *GetButtonIcon (char *buttonname)
{
	int		j;
	int		l;
	char	*b;
	l = strlen(buttonname);

	for (j=0 ; j<256 ; j++)
	{
		b = keybindings[j];
		if (!b)
			continue;
		if (!strncmp (b, buttonname, l) )
		{
			// naievil -- need to fix these
			if (!strcmp(Key_KeynumToString(j), "PADUP"))
				return b_up;
			else if (!strcmp(Key_KeynumToString(j), "PADDOWN"))
				return b_down;
			else if (!strcmp(Key_KeynumToString(j), "PADLEFT"))
				return b_left;
			else if (!strcmp(Key_KeynumToString(j), "PADRIGHT"))
				return b_right;
			else if (!strcmp(Key_KeynumToString(j), "SELECT"))
				return b_select;
			else if (!strcmp(Key_KeynumToString(j), "ABUTTON"))
				return b_abutton;
			else if (!strcmp(Key_KeynumToString(j), "BBUTTON"))
				return b_bbutton;
			else if (!strcmp(Key_KeynumToString(j), "XBUTTON"))
				return b_xbutton;
			else if (!strcmp(Key_KeynumToString(j), "YBUTTON"))
				return b_ybutton;
			else if (!strcmp(Key_KeynumToString(j), "LTRIGGER"))
				return b_lt;
			else if (!strcmp(Key_KeynumToString(j), "RTRIGGER"))
				return b_rt;
			else if (!strcmp(Key_KeynumToString(j), "ZLTRIGGER"))
				return b_zlt;
			else if (!strcmp(Key_KeynumToString(j), "ZRTRIGGER"))
				return b_zrt;
		}
	}
	return b_abutton;
}

char *GetUseButtonL ()
{
	int		j;
	int		l;
	char	*b;
	l = strlen("+use");

	for (j=0 ; j<256 ; j++)
	{
		b = keybindings[j];
		if (!b)
			continue;
		if (!strncmp (b, "+use", l) )
		{
			if (!strcmp(Key_KeynumToString(j), "SELECT") ||
				!strcmp(Key_KeynumToString(j), "LTRIGGER") ||
				!strcmp(Key_KeynumToString(j), "RTRIGGER") ||
				!strcmp(Key_KeynumToString(j), "HOME"))
				return "  ";
			else
				return " ";
		}
	}
	return " ";
}

char *GetPerkName (int perk)
{
	switch (perk)
	{
		case 1:
			return "Quick Revive";
		case 2:
			return "Juggernog";
		case 3:
			return "Speed Cola";
		case 4:
			return "Double Tap";
		case 5:
			return "Stamin-Up";
		case 6:
			return "PhD Flopper";
		case 7:
			return "Deadshot Daiquiri";
		case 8:
			return "Mule Kick";
		default:
			return "NULL";
	}
}

void SCR_UsePrint (int type, int cost, int weapon)
{
	//naievil -- fixme
    char s[128];

    switch (type)
	{
		case 0://clear
			strcpy(s, "");
			break;
		case 1://door
			strcpy(s, va("Hold %s to open Door [Cost:%i]\n", GetUseButtonL(), cost));
			button_pic_x = 5;
			break;
		case 2://debris
			strcpy(s, va("Hold %s to remove Debris [Cost:%i]\n", GetUseButtonL(), cost));
			button_pic_x = 5;
			break;
		case 3://ammo
			strcpy(s, va("Hold %s to buy Ammo for %s [Cost:%i]\n", GetUseButtonL(), pr_strings+sv_player->v.Weapon_Name_Touch, cost));
			button_pic_x = 5;
			break;
		case 4://weapon
			strcpy(s, va("Hold %s to buy %s [Cost:%i]\n", GetUseButtonL(), pr_strings+sv_player->v.Weapon_Name_Touch, cost));
			button_pic_x = 5;
			break;
		case 5://window
			strcpy(s, va("Hold %s to Rebuild Barrier\n", GetUseButtonL()));
			button_pic_x = 5;
			break;
		case 6://box
			strcpy(s, va("Hold %s to buy a Random Weapon [cost:%i]\n", GetUseButtonL(), cost));
			button_pic_x = 5;
			break;
		case 7://box take
			strcpy(s, va("Press %s to take Weapon\n", GetUseButtonL()));
			button_pic_x = 6;
			break;
		case 8://power
			strcpy(s, "The Power must be Activated first\n");
			button_pic_x = 100;
			break;
		case 9://perk
			strcpy(s, va("Hold %s to buy %s [Cost:%i]\n", GetUseButtonL(), GetPerkName(weapon), cost));
			button_pic_x = 5;
			break;
		case 10://turn on power
			strcpy(s, va("Hold %s to Turn On the Power\n", GetUseButtonL()));
			button_pic_x = 5;
			break;
		case 11://turn on trap
			strcpy(s, va("Hold %s to Activate the Trap [Cost:%i]\n", GetUseButtonL(), cost));
			button_pic_x = 5;
			break;
		case 12://PAP
			strcpy(s, va("Hold %s to Pack-a-Punch [Cost:%i]\n", GetUseButtonL(), cost));
			button_pic_x = 5;
			break;
		case 13://revive
			strcpy(s, va("Hold %s to Fix your Code.. :)\n", GetUseButtonL()));
			button_pic_x = 5;
			break;
		case 14://use teleporter (free)
			strcpy(s, va("Hold %s to use Teleporter\n", GetUseButtonL()));
			button_pic_x = 5;
			break;
		case 15://use teleporter (cost)
			strcpy(s, va("Hold %s to use Teleporter [Cost:%i]\n", GetUseButtonL(), cost));
			button_pic_x = 5;
			break;
		case 16://tp cooldown
			strcpy(s, "Teleporter is cooling down\n");
			button_pic_x = 100;
			break;
		case 17://link
			strcpy(s, va("Hold %s to initiate link to pad\n", GetUseButtonL()));
			button_pic_x = 5;
			break;
		case 18://no link
			strcpy(s, "Link not active\n");
			button_pic_x = 100;
			break;
		case 19://finish link
			strcpy(s, va("Hold %s to link pad with core\n", GetUseButtonL()));
			button_pic_x = 5;
			break;
		case 20://buyable ending
			strcpy(s, va("Hold %s to End the Game [Cost:%i]\n", GetUseButtonL(), cost));
			button_pic_x = 5;
			break;
		default:
			Con_Printf ("No type defined in engine for useprint\n");
			break;
	}

	strncpy (scr_usestring, va(s), sizeof(scr_usestring)-1);
	scr_usetime_off = 0.1;
}


void SCR_DrawUseString (void)
{
	int		l;
	int		x, y;

	if (cl.stats[STAT_HEALTH] < 0)
		return;
// the finale prints the characters one at a time

	y = vid.height*0.65;
	l = strlen (scr_usestring);
    x = (vid.width - l*8)/2;

    // naievil -- fixme the picture does not show...
    Draw_String (x, y, scr_usestring);
	Draw_Pic (x + button_pic_x*8, y - 4, GetButtonIcon("+use"));
}

void SCR_CheckDrawUseString (void)
{
	scr_copytop = 1;

	scr_usetime_off -= host_frametime;

	if (scr_usetime_off <= 0 && !cl.intermission)
		return;
	if (key_dest != key_game)
		return;
    if (cl.stats[STAT_HEALTH] <= 0)
        return;

	SCR_DrawUseString ();
}

//=============================================================================

/*
====================
CalcFov
====================
*/
float CalcFov (float fov_x, float width, float height)
{
        float   a;
        float   x;

        if (fov_x < 1 || fov_x > 179)
                Sys_Error ("Bad fov: %f", fov_x);

        x = width/tanf(fov_x/360*M_PI);

        a = atanf (height/x);

        a = a*360/M_PI;

        return a;
}

/*
=================
SCR_CalcRefdef

Must be called whenever vid changes
Internal use only
=================
*/
static void SCR_CalcRefdef (void)
{
	vrect_t		vrect;
	float		size;
	int		h;
	qboolean		full = false;


	scr_fullupdate = 0;		// force a background redraw
	vid.recalc_refdef = 0;

// force the status bar to redraw
	Sbar_Changed ();

//========================================
	
// bound viewsize
	if (scr_viewsize.value < 30)
		Cvar_Set ("viewsize","30");
	if (scr_viewsize.value > 120)
		Cvar_Set ("viewsize","120");

// bound field of view
	if (scr_fov.value < 10)
		Cvar_Set ("fov","10");
	if (scr_fov.value > 170)
		Cvar_Set ("fov","170");

// intermission is always full screen	
	if (cl.intermission)
		size = 120;
	else
		size = scr_viewsize.value;

	if (size >= 120)
		sb_lines = 0;		// no status bar at all
	else if (size >= 110)
		sb_lines = 24;		// no inventory
	else
		sb_lines = 24+16+8;

	if (scr_viewsize.value >= 100.0) {
		full = true;
		size = 100.0;
	} else
		size = scr_viewsize.value;
	if (cl.intermission)
	{
		full = true;
		size = 100;
		sb_lines = 0;
	}
	size /= 100.0;

	h = vid.height - sb_lines;

	r_refdef.vrect.width = vid.width * size;
	if (r_refdef.vrect.width < 96)
	{
		size = 96.0 / r_refdef.vrect.width;
		r_refdef.vrect.width = 96;	// min for icons
	}

	r_refdef.vrect.height = vid.height * size;
	if (r_refdef.vrect.height > vid.height - sb_lines)
		r_refdef.vrect.height = vid.height - sb_lines;
	if (r_refdef.vrect.height > vid.height)
			r_refdef.vrect.height = vid.height;
	r_refdef.vrect.x = (vid.width - r_refdef.vrect.width)/2;
	if (full)
		r_refdef.vrect.y = 0;
	else 
		r_refdef.vrect.y = (h - r_refdef.vrect.height)/2;

	r_refdef.fov_x = scr_fov.value;
	r_refdef.fov_y = CalcFov (r_refdef.fov_x, r_refdef.vrect.width, r_refdef.vrect.height);

	scr_vrect = r_refdef.vrect;
}


/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
void SCR_SizeUp_f (void)
{
	Cvar_SetValue ("viewsize",scr_viewsize.value+10);
	vid.recalc_refdef = 1;
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
void SCR_SizeDown_f (void)
{
	Cvar_SetValue ("viewsize",scr_viewsize.value-10);
	vid.recalc_refdef = 1;
}

//============================================================================

/*
==================
SCR_Init
==================
*/
void SCR_Init (void)
{

	Cvar_RegisterVariable (&scr_fov);
	Cvar_RegisterVariable (&scr_viewsize);
	Cvar_RegisterVariable (&scr_conspeed);
	Cvar_RegisterVariable (&scr_showram);
	Cvar_RegisterVariable (&scr_showturtle);
	Cvar_RegisterVariable (&scr_showpause);
	Cvar_RegisterVariable (&scr_centertime);
	Cvar_RegisterVariable (&scr_printspeed);
	Cvar_RegisterVariable (&scr_showfps);
	Cvar_RegisterVariable (&scr_loadscreen);

	Cvar_RegisterVariable (&gl_triplebuffer);

//
// register our commands
//
	Cmd_AddCommand ("screenshot",SCR_ScreenShot_f);
	Cmd_AddCommand ("sizeup",SCR_SizeUp_f);
	Cmd_AddCommand ("sizedown",SCR_SizeDown_f);

	hitmark = Draw_CachePic("gfx/hud/hit_marker");


	scr_initialized = true;
}

//============================================================================

/*
==============
SCR_DrawFPS -- johnfitz
==============
*/
void SCR_DrawFPS (void)
{
	static double	oldtime = 0;
	static double	lastfps = 0;
	static int	oldframecount = 0;
	double	elapsed_time;
	int	frames;

	elapsed_time = realtime - oldtime;
	frames = r_framecount - oldframecount;

	if (elapsed_time < 0 || frames < 0)
	{
		oldtime = realtime;
		oldframecount = r_framecount;
		return;
	}
	// update value every 3/4 second
	if (elapsed_time > 0.75)
	{
		lastfps = frames / elapsed_time;
		oldtime = realtime;
		oldframecount = r_framecount;
	}

	if (scr_showfps.value)
	{
		char	st[16];
		int	x, y;
		sprintf (st, "%4.0f fps", lastfps);
		x = 320 - (strlen(st)<<3);
		y = 200 - 8;

		Draw_String (0, 0, st);
	}
}


//=============================================================================

/*
==============
SCR_DrawLoading
==============
*/
void SCR_DrawLoading (void)
{
	qpic_t	*pic;

	if (!scr_drawloading)
		return;

	pic = Draw_CachePic ("gfx/loading.lmp");
	Draw_Pic ( (vid.width - pic->width)/2,
		(vid.height - 48 - pic->height)/2, pic);
}

int Random_Int (int max_int)
{
	float	f;
	f = (rand ()&0x7fff) / ((float)0x7fff) * max_int;
	if (f > 0)
		return (int)(f + 0.5) + 1;
	else
		return (int)(f - 0.5) + 1;
}
/*
==============
SCR_DrawLoadScreen
==============
*/

/*
	Creds to the following people from the 2020
	Loading Screen Hint Submission/Contest:

	* BCDeshiG
	* Derped_Crusader
	* Aidan
	* yasen
	* greg
	* Asher
	* Bernerd
	* Omar Alejandro
	* TheSmashers
*/
double loadingtimechange;
int loadingdot;
int loadingtextwidth;
char *lodinglinetext;
qpic_t *awoo;
char *ReturnLoadingtex (void)
{
    int StringNum = Random_Int(55);
    switch(StringNum)
    {
        case 1:
			return  "Released in 1996, Quake is now over 25 years old!";
            break;
        case 2:
            return  "Use the Kar98-k to be the hero we want you to be!";
            break;
        case 3:
            return  "There is a huge number of modern engines based on Quake!";
            break;
        case 4:
            return  "Development for NZ:P officially began on September 27, 2009";
            break;
        case 5:
            return  "NZ:P was first released on December 25, 2010!";
            break;
        case 6:
            return  "The 1.1 release of NZ:P has over 90,000 downloads!";
            break;
        case 7:
            return  "NZ:P has been downloaded over 400,000 times!";
            break;
        case 8:
            return  "The original NZP was made mainly by 3 guys around the world.";
            break;
        case 9:
            return  "Blubswillrule: known as \"blubs\", is from the USA.";
            break;
        case 10:
            return  "Jukki is from Finland.";
            break;
        case 11:
            return  "Ju[s]tice, or \"tom\" is from Lithuania.";
            break;
        case 12:
            return  "This game is the reason that we have bad sleeping habits!";
            break;
        case 13:
            return  "We had a lot of fun making this game.";
            break;
        case 14:
            return  "Did you know you can make your own Custom Map?";
            break;
        case 15:
            return  "Try Retro Mode, it's in the Graphics Settings!";
            break;
        case 16:
			return  "Tired of the base maps? Make your own or try some online!";
            break;
        case 17:
            return  "Slay zombies & be grateful.";
            break;
        case 18:
            return  "Custom maps, CUSTOM MAPS!";
            break;
        case 19:
            return  "Go outside & build a snowman!";
            break;
        case 20:
            return  "Please surround yourself with zombies!";
            break;
        case 21:
            return  "Don't play for too long, or zombies will eat you.";
            break;
        case 22:
            return  "That was epic... EPIC FOR THE WIIIN!"; //why
            break;
        case 23:
            return  "Citra is an awesome 3DS emulator!";
            break;
        case 24:
            return  "You dead yet?";
            break;
        case 25:
            return  "Now 21% cooler!";
            break;
        case 26:
            return  "your lg is nothink on the lan"; //what
            break;
        case 27:
            return  "I'm not your chaotic on dm6!"; 
            break;
        case 28:
            return  "Shoot zombies to kill them. Or knife them. You choose.";
            break;
        case 29:
            return 	"How many people forgot to Compile today?";
            break;
        case 30:
            return  "ggnore";
            break;
        case 31:
            return  "Have you tried NZ:P on PC or NX?";
            break;
        case 32:
            return  "Submerge your device in water for godmode!";
            break;
        case 33:
            return  "10/10/10 was a good day.";
            break;
        case 34:
            return  "Also check out \"No Bugs Allowed\" for the PSP!";
            break;
        case 35:
            return 	"MotoLegacy, or \"Ian\", is from the USA.";
            break;
        case 36:
            return  "Zombies don't like bullets.";
            break;
        case 37:
            return  "Thanks for being an awesome fan!";
            break;
		case 38:
			return 	"Removed Herobrine";
			break;
		case 39:
			return 	"Pack-a-Punch the Kar98k to get to round infinity.";
			break;
		case 40:
			return 	"I feel like I'm being gaslit.";
			break;
		case 41:
			return 	"Heads up! You will die if you are killed!";
			break;
		case 42:
			return 	"Zombies legally can't kill you if you say no!";
			break;
		case 43:
			return 	"Please help me find the meaning of   . Thanks.";
			break;
		case 44:
			return  "NZ:P Discord is ONLY for Thomas the Tank Engine Roleplay!";
			break;
		case 45:
			return 	"Get rid of the 21% cooler tip, it's an MLP reference.";
			break;
		case 46:
			return 	"You're playing on a 3DS!";
			break;
		case 47:
			return 	"Don't leak the beta!";
			break;
		case 48:
			return  "Jugger-Nog increases your health!";
			break;
		case 49:
			return  "greg was here";
			break;
		case 50:
			return  "Where the hell is the Mystery Box?!";
			break;
		case 51:
			return  "Zombies like getting shot.. I think.";
			break;
		case 52:
			return  "pro tip: aiming helps";
			break;
		case 53:
			return  "If a Nazi Zombie bites you, are you a Nazi, or a Zombie?";
			break;
		case 54:
			return  "Play some Custom Maps!";
			break;
    }
    return "wut wut";
}
qboolean load_screen_exists;
void SCR_DrawLoadScreen (void)
{

	if (developer.value) {
		return;
	}
	if (!con_forcedup) {
	    return;
	}

	if (loadingScreen) {
		if (!loadscreeninit) {
			load_screen_exists = qfalse;

			char* lpath;
			lpath = (char*)Z_Malloc(sizeof(char)*32);
			strcpy(lpath, "gfx/lscreen/");
			strcat(lpath, loadname2);

			lscreen = Draw_CachePic(lpath);
			awoo = Draw_CachePic("gfx/menu/awoo");

			if (lscreen != NULL)
				load_screen_exists = qtrue;

			loadscreeninit = qtrue;
		}

		if (load_screen_exists == qtrue)
			Draw_Pic(scr_vrect.x, scr_vrect.y, lscreen);

		Draw_FillByColor(0, 0, 480, 24, 0, 0, 0, 150);
		Draw_FillByColor(0, 248, 480, 24, 0, 0, 0, 150);

		Draw_ColoredString(2, 4, loadnamespec, 255, 255, 0, 255, 2);
	}

	if (loadingtimechange < Sys_FloatTime ())
	{
        lodinglinetext = ReturnLoadingtex();
		loadingtextwidth = strlen(lodinglinetext)*8;
        loadingtimechange = Sys_FloatTime () + 5;
	}

	if (key_dest == key_game) {
		Draw_ColoredString(vid.width/2 - loadingtextwidth/2, 225, lodinglinetext, 255, 255, 255, 255, 1);

		if (strcmp(lodinglinetext, "Please help me find the meaning of   . Thanks.") == 0) {
			Draw_Pic(280, 225, awoo);
		}
	}
}


/*
==================
SCR_SetUpToDrawConsole
==================
*/
void SCR_SetUpToDrawConsole (void)
{
	Con_CheckResize ();
	
	if (scr_drawloading)
		return;		// never a console with loading plaque
		
// decide on the height of the console
	con_forcedup = !cl.worldmodel || cls.signon != SIGNONS;

	if (con_forcedup)
	{
		scr_conlines = vid.height;		// full screen
		scr_con_current = scr_conlines;
	}
	else if (key_dest == key_console)
		scr_conlines = vid.height/2;	// half screen
	else
		scr_conlines = 0;				// none visible
	
	if (scr_conlines < scr_con_current)
	{
		scr_con_current -= scr_conspeed.value*host_frametime;
		if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;

	}
	else if (scr_conlines > scr_con_current)
	{
		scr_con_current += scr_conspeed.value*host_frametime;
		if (scr_conlines < scr_con_current)
			scr_con_current = scr_conlines;
	}

	if (clearconsole++ < vid.numpages)
	{
		Sbar_Changed ();
	}
	else if (clearnotify++ < vid.numpages)
	{
	}
	else
		con_notifylines = 0;
}
	
/*
==================
SCR_DrawConsole
==================
*/
void SCR_DrawConsole (void)
{
	if (scr_con_current)
	{
		scr_copyeverything = 1;
		Con_DrawConsole (scr_con_current, true);
		clearconsole = 0;
	}
	else
	{
		if (key_dest == key_game || key_dest == key_message)
			Con_DrawNotify ();	// only draw notify in game
	}
}


/* 
============================================================================== 
 
						SCREEN SHOTS 
 
============================================================================== 
*/ 

typedef struct _TargaHeader {
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} TargaHeader;


/* 
================== 
SCR_ScreenShot_f
================== 
*/  
void SCR_ScreenShot_f (void) 
{
	byte		*buffer;
	char		pcxname[80]; 
	char		checkname[MAX_OSPATH];
	int			i, c, temp;
// 
// find a file name to save it to 
// 
	strcpy(pcxname,"quake00.tga");
		
	for (i=0 ; i<=99 ; i++) 
	{ 
		pcxname[5] = i/10 + '0'; 
		pcxname[6] = i%10 + '0'; 
		sprintf (checkname, "%s/%s", com_gamedir, pcxname);
		if (Sys_FileTime(checkname) == -1)
			break;	// file doesn't exist
	} 
	if (i==100) 
	{
		Con_Printf ("SCR_ScreenShot_f: Couldn't create a PCX file\n"); 
		return;
 	}


	buffer = malloc(glwidth*glheight*3 + 18);
	memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = glwidth&255;
	buffer[13] = glwidth>>8;
	buffer[14] = glheight&255;
	buffer[15] = glheight>>8;
	buffer[16] = 24;	// pixel size

	glReadPixels (glx, gly, glwidth, glheight, GL_RGB, GL_UNSIGNED_BYTE, buffer+18 ); 

	// swap rgb to bgr
	c = 18+glwidth*glheight*3;
	for (i=18 ; i<c ; i+=3)
	{
		temp = buffer[i];
		buffer[i] = buffer[i+2];
		buffer[i+2] = temp;
	}
	COM_WriteFile (pcxname, buffer, glwidth*glheight*3 + 18 );

	free (buffer);
	Con_Printf ("Wrote %s\n", pcxname);
} 


//=============================================================================


/*
===============
SCR_BeginLoadingPlaque

================
*/
void SCR_BeginLoadingPlaque (void)
{
	S_StopAllSounds (true);

	if (cls.state != ca_connected)
		return;
	if (cls.signon != SIGNONS)
		return;
	
// redraw with no console and the loading plaque
	Con_ClearNotify ();
	scr_centertime_off = 0;
	scr_con_current = 0;

	scr_drawloading = true;
	scr_fullupdate = 0;
	Sbar_Changed ();
	SCR_UpdateScreen ();
	scr_drawloading = false;

	scr_disabled_for_loading = true;
	scr_disabled_time = realtime;
	scr_fullupdate = 0;
}

/*
===============
SCR_EndLoadingPlaque

================
*/
void SCR_EndLoadingPlaque (void)
{
	scr_disabled_for_loading = false;
	scr_fullupdate = 0;
	Con_ClearNotify ();
}

//=============================================================================

char	*scr_notifystring;
qboolean	scr_drawdialog;

void SCR_DrawNotifyString (void)
{
	char	*start;
	int		l;
	int		j;
	int		x, y;

	start = scr_notifystring;

	y = vid.height*0.35;

	do	
	{
	// scan the width of the line
		for (l=0 ; l<40 ; l++)
			if (start[l] == '\n' || !start[l])
				break;
		x = (vid.width - l*8)/2;
		for (j=0 ; j<l ; j++, x+=8)
			Draw_Character (x, y, start[j]);	
			
		y += 8;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;
		start++;		// skip the \n
	} while (1);
}

/*
==================
SCR_ModalMessage

Displays a text string in the center of the screen and waits for a Y or N
keypress.  
==================
*/
int SCR_ModalMessage (char *text)
{
	if (cls.state == ca_dedicated)
		return true;

	scr_notifystring = text;
 
// draw a fresh screen
	scr_fullupdate = 0;
	scr_drawdialog = true;
	SCR_UpdateScreen ();
	scr_drawdialog = false;
	
	S_ClearBuffer ();		// so dma doesn't loop current sound

	do
	{
		key_count = -1;		// wait for a key down and up
		Sys_SendKeyEvents ();
	} while (key_lastpress != 'y' && key_lastpress != 'n' && key_lastpress != K_ESCAPE);

	scr_fullupdate = 0;
	SCR_UpdateScreen ();

	return key_lastpress == 'y';
}


//=============================================================================

/*
===============
SCR_BringDownConsole

Brings the console down and fades the palettes back to normal
================
*/
void SCR_BringDownConsole (void)
{
	int		i;
	
	scr_centertime_off = 0;
	
	for (i=0 ; i<20 && scr_conlines != scr_con_current ; i++)
		SCR_UpdateScreen ();

	cl.cshifts[0].percent = 0;		// no area contents palette on next frame
	VID_SetPalette (host_basepal);
}

void SCR_TileClear (void)
{
	if (r_refdef.vrect.x > 0) {
		// left
		Draw_TileClear (0, 0, r_refdef.vrect.x, vid.height - sb_lines);
		// right
		Draw_TileClear (r_refdef.vrect.x + r_refdef.vrect.width, 0, 
			vid.width - r_refdef.vrect.x + r_refdef.vrect.width, 
			vid.height - sb_lines);
	}
	if (r_refdef.vrect.y > 0) {
		// top
		Draw_TileClear (r_refdef.vrect.x, 0, 
			r_refdef.vrect.x + r_refdef.vrect.width, 
			r_refdef.vrect.y);
		// bottom
		Draw_TileClear (r_refdef.vrect.x,
			r_refdef.vrect.y + r_refdef.vrect.height, 
			r_refdef.vrect.width, 
			vid.height - sb_lines - 
			(r_refdef.vrect.height + r_refdef.vrect.y));
	}
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.

WARNING: be very careful calling this from elsewhere, because the refresh
needs almost the entire 256k of stack space!
==================
*/

int GetWeaponZoomAmmount (void)
{
    switch (cl.stats[STAT_ACTIVEWEAPON])
    {
        case W_MP40:
        case W_357:
        case W_SAWNOFF:
        case W_TRENCH:
        case W_PANZER:
        case W_RAY:
        case W_THOMPSON:
        case W_COLT:
        case W_PPSH:
        case W_TYPE:
        //case W_TESLA:
            return 5;
            break;
        case W_STG:
        case W_BROWNING:
            return 10;
            break;
        case W_KAR:
        case W_GEWEHR:
        case W_M1:
        case W_M1A1:
        case W_BAR:
        case W_FG:
        case W_KAR_SCOPE:
        case W_PTRS:
        //case W_HEADCRACKER:
        //case W_PENETRATOR:
            return 20;
            break;
        default:
            return 5;
            break;
    }
}
float zoomin_time;
int original_fov;
void SCR_UpdateScreen (void)
{
	static float	oldscr_viewsize;
	vrect_t		vrect;

	if (block_drawing)
		return;

	vid.numpages = 2 + gl_triplebuffer.value;

	scr_copytop = 0;
	scr_copyeverything = 0;

	if (scr_disabled_for_loading)
	{
		if (realtime - scr_disabled_time > 60)
		{
			scr_disabled_for_loading = false;
			Con_Printf ("load failed.\n");
		}
		else
			return;
	}

	if (!scr_initialized || !con_initialized)
		return;				// not initialized yet


	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
	//
	// determine size of refresh window
	//
	if (cl.stats[STAT_ZOOM] == 1)
	{
		/*if (zoomin_time == 0 && !original_fov)
		{
		    original_fov = scr_fov.value;
		    zoomin_time = Sys_FloatTime () +  0.01;
		    Cvar_SetValue ("fov", scr_fov.value - 2.5);
	
		}
		else if (zoomin_time < Sys_FloatTime ())
		{
		    int tempfloat = original_fov - GetWeaponZoomAmmount();
		    if (scr_fov.value > tempfloat)
		    {
			zoomin_time = Sys_FloatTime () + 0.01;
			Cvar_SetValue ("fov", scr_fov.value - 2.5);
		    }
				else
				{
			Cvar_SetValue ("fov", tempfloat);
					zoomin_time = 0;
				}
		}*/
		if(!original_fov)
			original_fov = scr_fov.value;
		if(scr_fov.value > (GetWeaponZoomAmmount() + 1))//+1 for accounting for floating point inaccurraces
		{
			scr_fov.value += ((original_fov - GetWeaponZoomAmmount()) - scr_fov.value) * 0.25;
			Cvar_SetValue("fov",scr_fov.value);
		}
	}
	else if (cl.stats[STAT_ZOOM] == 2)
	{
		Cvar_SetValue ("fov", 30);
		zoomin_time = 0;
	}
	else if (cl.stats[STAT_ZOOM] == 3)
	{
		if(!original_fov)
			original_fov = scr_fov.value;
		//original_fov = scr_fov.value;
		scr_fov.value += (original_fov - 10 - scr_fov.value) * 0.3;
		Cvar_SetValue("fov",scr_fov.value);
	}
	else if (cl.stats[STAT_ZOOM] == 0 && original_fov != 0)
	{
		/*zoomin_time = Sys_FloatTime () + 0.01;
		Cvar_SetValue ("fov", scr_fov.value + 2.5);
		if (scr_fov.value == original_fov)
		{
		    zoomin_time = 0;
		    original_fov = 0;
		}*/
		if(scr_fov.value < (original_fov + 1))//+1 for accounting for floating point inaccuracies
		{
			scr_fov.value += (original_fov - scr_fov.value) * 0.25;
			Cvar_SetValue("fov",scr_fov.value);
		}
		else
		{
			original_fov = 0;
		}
	}

	if (oldfov != scr_fov.value)
	{
		oldfov = scr_fov.value;
		vid.recalc_refdef = qtrue;
	}

	if (oldscreensize != scr_viewsize.value)
	{
		oldscreensize = scr_viewsize.value;
		vid.recalc_refdef = qtrue;
	}

	if (vid.recalc_refdef)
		SCR_CalcRefdef ();

//
// do 3D refresh drawing, and then update the screen
//
	SCR_SetUpToDrawConsole ();

	V_RenderView ();

	GL_Set2D ();

	Draw_Crosshair ();

	//muff - to show FPS on screen
	SCR_DrawFPS ();
	SCR_CheckDrawCenterString ();
	SCR_CheckDrawUseString ();
	HUD_Draw ();
	SCR_DrawConsole ();
	M_Draw ();

	// naievil -- fixme
	if(scr_loadscreen.value) {
		SCR_DrawLoadScreen();
	}

	// naievil -- fixme
	//Draw_LoadingFill();

	V_UpdatePalette ();

	GL_EndRendering ();
}


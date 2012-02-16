#include <stdlib.h>
#include <wiiuse/wpad.h>
#include <dirent.h>
#include <ogc/lwp_watchdog.h>

#include "wiiload/wiiload.h"
#include "globals.h"
#include "bin2o.h"
#include "network.h"
#include "sys/errno.h"
#include "http.h"
#include "ios.h"
#include "identify.h"
#include "gui.h"
#include "neek.h"
#include "mystring.h"
#include "cfg.h"

#define CHNMAX 1024

/*

This allow to browse for applications in games folder of mounted drive

*/
extern s_grlibSettings grlibSettings;
static s_gameConfig gameConf;

static int browse = 0;
static int scanned = 0;

#define CATN 32
#define CATMAX 32

static char *flt[CATN] = { 
"Action",
"Adventure",
"Sport",
"Racing",
"Rhythm",
"Simulation",
"Platformer",
"Party",

"Music",
"Puzzle",
"Fighting",
"Shooter",
"Strategy",
"RPG",
"FPS",
"TPS",

"Online",
"DS Connect",
"ESRB M 3+",
"ESRB E 6+",
"ESRB E 10+",
"ESRB T 13+",
"ESRB M 17+",
"ESRB A 18+",

"uDraw", 		
"Bal. board",    
"Instrument", 	
"Zapper", 		
"Microphone", 	
"Motion+",		
"Wheel",
"Dance Pad"
};

static s_game *games;
static int gamesCnt;
static int games2Disp;
static int page; // Page to be displayed
static int pageMax; // 
static int gamesSelected = -1;	// Current selected app with wimote

static int browserRet = 0;
static int showHidden = 0;

static u8 redraw = 1;
static bool redrawIcons = true;

static int refreshPng = 0;

static s_grlib_iconSetting is;

static void Redraw (void);
static int GameBrowse (int forcescan);

char* neek_GetGames (void);
void neek_KillDIConfig (void);
static bool IsFiltered (int ai);

static s_cfg *cfg;

#define ICONW 80
#define ICONH 150

#define XMIDLEINFO 320
#define INIT_X (30 + ICONW / 2)
#define INIT_Y (60 + ICONH / 2)
#define INC_X ICONW+22
#define INC_Y ICONH+25

static void InitializeGui (void)
	{
	// Prepare black box
	int i, il;
	int x = INIT_X;
	int y = INIT_Y;

	gui_Init();

	gui.spotsIdx = 0;
	gui.spotsXpage = 12;
	gui.spotsXline = 6;

	grlib_IconSettingInit (&is);

	is.themed = theme.ok;
	is.border = 5.0;
	is.magX = 1.0;
	is.magY = 1.0;
	is.magXSel = 1.2;
	is.magYSel = 1.2;
	is.iconFake = vars.tex[TEX_NONE];
	is.bkgTex = theme.frameBack;
	is.fgrSelTex = theme.frameSel;
	is.fgrTex = theme.frame;
	is.bkgColor = RGBA (0,0,0,255);
	is.borderSelColor = RGBA (255,255,255,255); 	// Border color
	is.borderColor = RGBA (128,128,128,255); 		// Border color
	is.fontReverse = 0; 		// Border color
	
	Debug ("theme.frameBack = 0x%X 0x%X", theme.frameBack, is.bkgTex);

	il = 0;
	for (i = 0; i < gui.spotsXpage; i++)
		{
		grlib_IconInit (&gui.spots[i].ico, NULL);
		
		gui.spots[i].ico.x = x;
		gui.spots[i].ico.y = y;
		gui.spots[i].ico.w = ICONW;
		gui.spots[i].ico.h = ICONH;

		il++;
		if (il == gui.spotsXline)
			{
			x = INIT_X;
			y += INC_Y;
			il = 0;
			}
		else
			{
			x+=INC_X;
			}
		}
	}

static bool DownloadCovers_Get (char *path, char *buff)
	{
	u8* outbuf=NULL;
	u32 outlen=0;
	
	outbuf = downloadfile (buff, &outlen, NULL);
		
	if (IsPngBuff (outbuf, outlen))
		{
		//suficientes bytes
		FILE *f;
		
		f = fopen (path, "wb");
		if (f)
			{
			fwrite (outbuf, outlen, 1, f);
			fclose (f);
			}
		
		free(outbuf);
		return (TRUE);
		}
		
	return (FALSE);
	}

void MakeCoverPath (int ai, char *path)
	{
	char asciiId[10];
	
	strcpy (asciiId, games[ai].asciiId);

	if (config.gameMode == GM_DML)
		asciiId[6] = 0;

	sprintf (path, "%s://ploader/covers/%s.png", vars.defMount, asciiId);
	}

static void FeedCoverCache (void)
	{
	Debug ("AppsSort: begin");
	
	char path[128];
	CoverCache_Pause (true);

	int i;
	int ai;	// Application index (corrected by the offset)

	for (i = -gui.spotsXpage; i < gui.spotsXpage*2; i++)
		{
		ai = (page * gui.spotsXpage) + i;
		
		if (ai >= 0 && ai < gamesCnt)
			{
			MakeCoverPath (ai, path);
			CoverCache_Add (path, false);
			}
		}
	
	CoverCache_Pause (false);
	
	Debug ("AppsSort: end");
	}

static void DownloadCovers (void)
	{
	int ia, stop;
	char buff[300];
	char path[PATHMAX];

	Redraw ();
	grlib_PushScreen ();
	
	Video_WaitPanel (TEX_HGL, "Stopping wiiload thread");
	WiiLoad_Pause ();
	
	stop = 0;

	FILE *f = NULL;
	if (IsDevValid (DEV_SD))
		{
		sprintf (path, "%s://missgame.txt", vars.mount[DEV_SD]);
		f = fopen (path, "wb");
		}
	
	for (ia = 0; ia < gamesCnt; ia++)
		{
		Video_WaitPanel (TEX_HGL, "Downloading %s.png (%d of %d)|(B) Stop", games[ia].asciiId, ia, gamesCnt);
		sprintf (path, "%s://ploader/covers/%s.png", vars.defMount, games[ia].asciiId);
		
		if (!fsop_FileExist(path))
			{
			bool ret = FALSE;
			
			if (!ret)
				{
				sprintf (buff, "http://www.postloader.freehosting.com/png/%s.png", games[ia].asciiId);
				ret = DownloadCovers_Get (path, buff);
				}
			if (!ret)
				{
				sprintf (buff, "http://art.gametdb.com/wii/cover/US/%s.png", games[ia].asciiId);
				ret = DownloadCovers_Get (path, buff);
				}
			if (!ret)
				{
				sprintf (buff, "http://art.gametdb.com/wii/cover/EN/%s.png", games[ia].asciiId);
				ret = DownloadCovers_Get (path, buff);
				}
			if (!ret)
				{
				sprintf (buff, "http://art.gametdb.com/wii/cover/JA/%s.png", games[ia].asciiId);
				ret = DownloadCovers_Get (path, buff);
				}
				
			if (!ret && f)
				{
				sprintf (buff, "%s:%s\n", games[ia].asciiId, games[ia].name);
				fwrite (buff, 1, strlen(buff), f);
				}
				
			if (grlib_GetUserInput () == WPAD_BUTTON_B)
				{
				stop = 1;
				break;
				}
			}
					
		if (stop) break;
		}
		
	if (f) fclose (f);
		
	WiiLoad_Resume ();
	
	GameBrowse (0);
	}

static void WriteGameConfig (int ia)
	{
	if (ia < 0) return;
	
	strcpy (gameConf.asciiId, games[ia].asciiId);
	gameConf.hidden = games[ia].hidden;
	gameConf.priority = games[ia].priority;
	gameConf.category = games[ia].category;
	gameConf.playcount = games[ia].playcount;
	
	char *buff = Bin2HexAscii (&gameConf, sizeof (s_gameConfig), 0);
	cfg_SetString (cfg, games[ia].asciiId, buff);
	free (buff);
	}

static int ReadGameConfig (int ia)
	{
	char buff[1024];
	bool valid;
	
	//ReadGameConfig (
	
	valid = cfg_GetString (cfg, games[ia].asciiId, buff);
	
	if (valid)
		{
		if (HexAscii2Bin (buff, &gameConf) != sizeof (s_gameConfig))
			{
			valid = false;
			}
		}
	
	if (!valid)
		{
		gameConf.priority = 5;
		gameConf.hidden = 0;
		gameConf.playcount = 0;
		gameConf.category = 0;
		
		gameConf.ios = 0; 		// use current
		gameConf.vmode = 0;
		gameConf.language = -1;
		gameConf.vpatch = 0;
		gameConf.ocarina = 0;
		gameConf.hook = 0;
		gameConf.loader = config.gameDefaultLoader;

		/*CONF_GetRegion() == CONF_REGION_EU*/ 
		if (games[ia].asciiId[3] == 'E' || games[ia].asciiId[3] == 'J' || games[ia].asciiId[3] == 'N')
			gameConf.dmlvideomode = DMLVIDEOMODE_NTSC;
		else
			gameConf.dmlvideomode = DMLVIDEOMODE_PAL;
		}

	games[ia].category = gameConf.category;
	games[ia].hidden = gameConf.hidden;
	games[ia].priority = gameConf.priority;
	games[ia].playcount = gameConf.playcount;
	
	return valid;
	}

static void StructFree (void)
	{
	int i;
	
	for (i = 0; i < gamesCnt; i++)
		{
		games[i].png = NULL;
		
		if (games[i].name != NULL) 
			{
			free (games[i].name);
			games[i].name = NULL;
			}
		}
		
	gamesCnt = 0;
	}
	
#define SKIP 10
static void AppsSort (void)
	{
	Debug ("AppsSort: begin");
	
	int i;
	int mooved;
	s_game app;
	
	// Apply filters
	games2Disp = 0;
	for (i = 0; i < gamesCnt; i++)
		{
		games[i].filterd = IsFiltered (i);
		if (games[i].filterd && (!games[i].hidden || showHidden)) games2Disp++;
		}
	
	// Sort by filter, use stupid algorithm...
	do
		{
		mooved = 0;
		
		for (i = 0; i < gamesCnt - 1; i++)
			{
			if (games[i].filterd < games[i+1].filterd)
				{
				// swap
				memcpy (&app, &games[i+1], sizeof(s_game));
				memcpy (&games[i+1], &games[i], sizeof(s_game));
				memcpy (&games[i], &app, sizeof(s_game));
				mooved = 1;
				}
			}
		}
	while (mooved);

	// Sort by hidden, use stupid algorithm...
	do
		{
		mooved = 0;
		
		for (i = 0; i < gamesCnt - 1; i++)
			{
			if (games[i].hidden > games[i+1].hidden)
				{
				// swap
				memcpy (&app, &games[i+1], sizeof(s_game));
				memcpy (&games[i+1], &games[i], sizeof(s_game));
				memcpy (&games[i], &app, sizeof(s_game));
				mooved = 1;
				}
			}
		}
	while (mooved);

	// Sort by name, use stupid algorithm...
	do
		{
		mooved = 0;
		
		for (i = 0; i < games2Disp - 1; i++)
			{
			if (games[i].name && games[i+1].name && ms_strcmp (games[i+1].name, games[i].name) < 0)
				{
				// swap
				memcpy (&app, &games[i+1], sizeof(s_game));
				memcpy (&games[i+1], &games[i], sizeof(s_game));
				memcpy (&games[i], &app, sizeof(s_game));
				mooved = 1;
				}
			}
		}
	while (mooved);

	// Sort by priority
	if (config.gameSort == 0)
		{
		do
			{
			mooved = 0;
			
			for (i = 0; i < games2Disp - 1; i++)
				{
				if (games[i+1].priority > games[i].priority)
					{
					// swap
					memcpy (&app, &games[i+1], sizeof(s_game));
					memcpy (&games[i+1], &games[i], sizeof(s_game));
					memcpy (&games[i], &app, sizeof(s_game));
					mooved = 1;
					}
				}
			}
		while (mooved);
		}

	// Sort by priority
	if (config.gameSort == 2)
		{
		do
			{
			mooved = 0;
			
			for (i = 0; i < games2Disp - 1; i++)
				{
				if (games[i+1].playcount > games[i].playcount)
					{
					// swap
					memcpy (&app, &games[i+1], sizeof(s_game));
					memcpy (&games[i+1], &games[i], sizeof(s_game));
					memcpy (&games[i], &app, sizeof(s_game));
					mooved = 1;
					}
				}
			}
		while (mooved);
		}

	// Sort by priority
	if (config.gameMode == GM_DML)
		{
		do
			{
			mooved = 0;
			
			for (i = 0; i < games2Disp - 1; i++)
				{
				if (games[i+1].slot < games[i].slot)
					{
					// swap
					memcpy (&app, &games[i+1], sizeof(s_game));
					memcpy (&games[i+1], &games[i], sizeof(s_game));
					memcpy (&games[i], &app, sizeof(s_game));
					mooved = 1;
					}
				}
			}
		while (mooved);
		}

	pageMax = (games2Disp-1) / gui.spotsXpage;
	refreshPng = 1;
	
	Debug ("AppsSort: end");
	}
	
static void UpdateTitlesFromTxt (void)
	{
	Debug ("UpdateTitlesFromTxt: begin");
	LoadTitlesTxt ();
	if (titlestxt == NULL) return;

	int i;
	char buff[1024];
	for (i = 0; i < gamesCnt; i++)
		{
		if (cfg_GetString (titlestxt, games[i].asciiId, buff))
			{
			//Debug ("UpdateTitlesFromTxt: '%s' -> '%s'", games[i].name, buff);
			free (games[i].name);
			games[i].name = ms_utf8_to_ascii (buff);
			}
		
		if (i % 20 == 0) Video_WaitPanel (TEX_HGL, "Please wait...|Parsing...");
		}
	Debug ("UpdateTitlesFromTxt: end");
	}

static int GameBrowse (int forcescan)
	{
	char slot[8];
	Debug ("begin GameBrowse");
	
	gui.spotsIdx = 0;
	gui_Clean ();
	StructFree ();

	int i;
	char *titles;
	char *p;
	
	Video_WaitPanel (TEX_HGL, "Please wait...");
	CoverCache_Pause (true);
	if (config.gameMode == GM_WII)
		{
		if (vars.neek != NEEK_NONE) // use neek interface to build up game listing
			titles = neek_GetGames ();
		else
			titles = WBFSSCanner (forcescan);
		}
	else
		{
		titles = DMLScanner (forcescan);
		}
	CoverCache_Pause (false);	
	if (!titles) return 0;
	
	p = titles;
	i = 0;
	
	Debug ("GameBrowse [begin]");
			
	do
		{
		if (*p != '\0' && strlen(p))
			{
			// Add name
			games[i].name = malloc (strlen(p));
			strcpy (games[i].name, p);
			p += (strlen(p) + 1);
			
			// Add id
			strcpy (games[i].asciiId, p);
			p += (strlen(p) + 1);

			if (config.gameMode == GM_WII)
				{
				// Setup slot
				if (vars.neek != NEEK_NONE)
					games[i].slot = i;
				else
					{
					// Add slot
					strcpy (slot, p);
					p += (strlen(p) + 1);

					games[i].slot = atoi (slot); // PArtition number
					}
				}
			else
				{
				strcpy (slot, p);
				p += (strlen(p) + 1);

				games[i].slot = atoi (slot); // sd = 0 / usb = 1 ?
				}
			
			if (i % 20 == 0) Video_WaitPanel (TEX_HGL, "Please wait...|Loading game configuration");
			ReadGameConfig (i);
			
			i ++;
			}
		else
			break;
		}
	while (TRUE);
	
	gamesCnt = i;
	
	free (titles);

	scanned = 1;

	Debug ("end GameBrowse");

	UpdateTitlesFromTxt ();
	AppsSort ();
	FeedCoverCache ();

	return gamesCnt;
	}

static int FindSpot (void)
	{
	int i;
	static time_t t = 0;
	char info[300];
	
	gamesSelected = -1;
	
	for (i = 0; i < gui.spotsIdx; i++)
		{
		if (grlib_irPos.x > gui.spots[i].ico.rx1 && grlib_irPos.x < gui.spots[i].ico.rx2 && grlib_irPos.y > gui.spots[i].ico.ry1 && grlib_irPos.y < gui.spots[i].ico.ry2)
			{
			// Ok, we have the point
			gamesSelected = gui.spots[i].id;

			gui.spots[i].ico.sel = true;
			grlib_IconDraw (&is, &gui.spots[i].ico);

			strcpy (info, games[gamesSelected].name);
			
			if (vars.neek == NEEK_NONE) // only on real nand
				{
				char part[64];
				
				if (config.gameMode == GM_WII)
					{
					if (games[gamesSelected].slot < 10)
						sprintf (part, " (FAT%d)", games[gamesSelected].slot + 1);
					else
						sprintf (part, " (NTFS%d)", games[gamesSelected].slot - 10 + 1);
					}
				else
					{
					if (games[gamesSelected].slot == DEV_SD)
						sprintf (part, " (SD)");
					else
						sprintf (part, " (USB)");
					}
					
				strcat (info, part);
				}
			
			grlib_SetFontBMF (fonts[FNTNORM]);
			grlib_printf (XMIDLEINFO, theme.line2Y, GRLIB_ALIGNCENTER, 0, info);

			grlib_SetFontBMF (fonts[FNTSMALL]);
			
			*info = '\0';
			strcat (info, "(");
			strcat (info, games[gamesSelected].asciiId);
			strcat (info, ")");
			if (config.gameMode == GM_DML && strlen (games[gamesSelected].asciiId) == 7)
				{
				char b[32];
				sprintf (b, " DISC %s", &games[gamesSelected].asciiId[6]);
				
				strcat (info, b);
				}

			grlib_printf (XMIDLEINFO, theme.line1Y, GRLIB_ALIGNCENTER, 0, info);
			
			t = time(NULL);
			break;
			}
		}
	
	grlib_SetFontBMF (fonts[FNTNORM]);
	if (!grlib_irPos.valid)
		{
		if (gamesSelected == -1) grlib_printf (XMIDLEINFO, theme.line2Y, GRLIB_ALIGNCENTER, 0, "Point an icon with the wiimote or use a GC controller!");
		}
	else
		if (time(NULL) - t > 0 && gamesSelected == -1)
			{
			grlib_printf (XMIDLEINFO, theme.line2Y, GRLIB_ALIGNCENTER, 0, "(A) Execute (B) Settings (D-Pad) Switch mode (1) goto page (2) Filters");
			}
	
	return gamesSelected;
	}
	
static void ShowGameFilterMenu (int idx)
	{
	char title[128];
	char buff[512];
	u8 f[CATN];
	int i, item;

	for (i = 0; i <CATN; i++)
		f[i] = 0;
	
	for (i = 0; i < CATMAX; i++)
		f[i] = (games[idx].category & (1 << i)) ? 1:0;
	
	do
		{
		buff[0] = '\0';
		for (i = 0; i < CATMAX; i++) // Do not show uncat flag (always the last)
			{
			if (i == 8 || i == 16 || i == 24) grlib_menuAddColumn (buff);
			grlib_menuAddCheckItem (buff, 100 + i, f[i], flt[i]);
			}
			
		sprintf (title, "Category: %s\nPress (B) to close, (+) Select all, (-) Deselect all", games[idx].name);
		item = grlib_menu (title, buff);

		if (item == MNUBTN_PLUS)
			{
			int i; 	for (i = 0; i < CATN; i++) f[i] = 1;
			}

		if (item == MNUBTN_MINUS)
			{
			int i; 	for (i = 0; i < CATN; i++) f[i] = 0;
			}
		
		if (item >= 100)
			{
			int i = item - 100;
			f[i] = !f[i];
			}
		}
	while (item != -1);
	
	games[idx].category = 0;
	for (i = 0; i < CATN; i++)
		if (f[i]) games[idx].category |= (1 << i);
	}

static bool IsFiltered (int ai)
	{
	int i,j;
	bool ret = false;
	char f[128];
	
	if (config.gameFilter == 0) return true;
	
	memset (f, 0, sizeof(f));

	j = 0;
	for (i = 0; i < CATMAX-1; i++)
		{
		f[j++] = (config.gameFilter & (1 << i)) ? '1':'0';
		f[j++] = (games[ai].category & (1 << i)) ? '1':'0';
		f[j++] = ' ';
		
		if ((config.gameFilter & (1 << i)) && (games[ai].category & (1 << i)))
			{
			ret = true;
			}
		}
		
	return ret;
	}
	
static void GetCatString (int idx, char *buff)
	{
	int i, first = 1;

	*buff = '\0';
	
	for (i = 0; i < CATN; i++)
		{
		if ((games[idx].category & (1 << i)))
			{
			if (!first)
				strcat (buff, ", ");
				
			strcat (buff, flt[i]);
			first = 0;
			}
		}
	}

static void ShowFilterMenu (void)
	{
	char buff[512];
	u8 f[CATN];
	int i, item;

	for (i = 0; i <CATN; i++)
		f[i] = 0;
	
	for (i = 0; i < CATMAX; i++)
		f[i] = (config.gameFilter & (1 << i)) ? 1:0;

	do
		{
		buff[0] = '\0';
		for (i = 0; i < CATMAX; i++)
			{
			if (i == 8 || i == 16 || i == 24) grlib_menuAddColumn (buff);
			grlib_menuAddCheckItem (buff, 100 + i, f[i], flt[i]);
			}
		
		item = grlib_menu ("Filter menu\nPress (B) to close, (+) Select all, (-) Deselect all (shown all games)", buff);

		if (item == MNUBTN_PLUS)
			{
			int i; 	for (i = 0; i < CATN; i++) f[i] = 1;
			}

		if (item == MNUBTN_MINUS)
			{
			int i; 	for (i = 0; i < CATN; i++) f[i] = 0;
			}
		
		if (item >= 100)
			{
			int i = item - 100;
			f[i] = !f[i];
			}
		}
	while (item != -1);
	
	config.gameFilter = 0;
	for (i = 0; i < CATN; i++)
		if (f[i]) config.gameFilter |= (1 << i);
	
	GameBrowse (0);
	AppsSort ();
	}

#define CHOPT_IOS 7
#define CHOPT_VID 8
#define CHOPT_VIDP 4
#define CHOPT_LANG 11
#define CHOPT_HOOK 8
#define CHOPT_OCA 4
#define CHOPT_NAND 5
#define CHOPT_LOADER 3

static void ShowAppMenu (int ai)
	{
	char buff[1024];
	char b[64];
	int item;
	
	int opt[8] = {CHOPT_IOS, CHOPT_VID, CHOPT_VIDP, CHOPT_LANG, CHOPT_HOOK, CHOPT_OCA, CHOPT_NAND, CHOPT_LOADER};

	char *ios[CHOPT_IOS] = { "249", "250" , "222", "223", "248", "251", "252"};
	char *nand[CHOPT_NAND] = { "Default", "USA" , "EURO", "JAP", "Korean"};
	char *loader[CHOPT_NAND] = { "CFG", "GX", "WiiFlow"};
	/*
	char *videooptions[CHOPT_VID] = { "Default Video Mode", "Force NTSC480i", "Force NTSC480p", "Force PAL480i", "Force PAL480p", "Force PAL576i", "Force MPAL480i", "Force MPAL480p" };
	char *videopatchoptions[CHOPT_VIDP] = { "No Video patches", "Smart Video patching", "More Video patching", "Full Video patching" };
	char *languageoptions[CHOPT_LANG] = { "Default Language", "Japanese", "English", "German", "French", "Spanish", "Italian", "Dutch", "S. Chinese", "T. Chinese", "Korean" };
	char *hooktypeoptions[CHOPT_HOOK] = { "No Ocarina&debugger", "Hooktype: VBI", "Hooktype: KPAD", "Hooktype: Joypad", "Hooktype: GXDraw", "Hooktype: GXFlush", "Hooktype: OSSleepThread", "Hooktype: AXNextFrame" };
	char *ocarinaoptions[CHOPT_OCA] = { "No Ocarina", "Ocarina from NAND", "Ocarina from SD", "Ocarina from USB" };
	*/
	start:
	
	grlib_SetFontBMF(fonts[FNTNORM]);

	ReadGameConfig (ai);
	gameConf.language ++; // umph... language in triiforce start at -1... not index friendly
	do
		{
		
		buff[0] = '\0';

		sprintf (b, "Played %d times##6|", games[ai].playcount);
		strcat (buff, b);

		char cat[128];
		GetCatString (ai, cat);
		if (strlen(cat) > 32)
			{
			cat[32] = 0;
			strcat (cat, "...");
			}
		if (strlen (cat) == 0)
			{
			strcpy (cat, "<none>");
			}

		strcat (buff, "Category: "); strcat (buff, cat); strcat (buff, "##5|");
		
		sprintf (b, "Vote this title (%d/10)##4||", games[ai].priority);
		strcat (buff, b);

		if (games[ai].hidden)
			strcat (buff, "Remove hide flag##2|");
		else
			strcat (buff, "Hide this title ##3|");

		if (config.gameMode == GM_WII)
			{
			if (vars.neek != NEEK_NONE)
				{
				strcat (buff, "NAND: "); strcat (buff, nand[gameConf.nand]); strcat (buff, "##106|");
				}
			else
				{
				strcat (buff, "IOS: "); strcat (buff, ios[gameConf.ios]); strcat (buff, "##100|");
				strcat (buff, "Loader: "); strcat (buff, loader[gameConf.loader]); strcat (buff, "##107|");
				}
			}
		else
			{
			if (games[ai].slot == 0)
				grlib_menuAddItem (buff, 7, "Remove from SD");
			}
		/*
		else
			{
			grlib_menuAddCheckItem (buff, 108, 1 - gameConf.dmlvideomode, "NTSC mode");
			grlib_menuAddCheckItem (buff, 109, gameConf.dmlvideomode, "PAL 576i mode");
			}
		*/
		/*

		if (config.chnBrowser.nand != NAND_REAL)
			{
			
			if (gameConf.ios != MICROSNEEK_IOS)
				{
				strcat (buff, "Video: "); strcat (buff, videooptions[gameConf.vmode]); strcat (buff, "##101|");
				strcat (buff, "Video Patch: "); strcat (buff, videopatchoptions[gameConf.vpatch]); strcat (buff, "##102|");
				strcat (buff, "Language: "); strcat (buff, languageoptions[gameConf.language]); strcat (buff, "##103|");
				strcat (buff, "Hook type: "); strcat (buff, hooktypeoptions[gameConf.hook]); strcat (buff, "##104|");
				strcat (buff, "Ocarina: "); strcat (buff, ocarinaoptions[gameConf.ocarina]); strcat (buff, "##105|");
				}
			else
				{
				strcat (buff, "Video: n/a##200|");
				strcat (buff, "Video Patch: n/a##200|");
				strcat (buff, "Language: n/a##200|");
				strcat (buff, "Hook type: n/a##200|");
				strcat (buff, "Ocarina: n/a##200|");
				}
			}
		*/

		strcat (buff, "|");
		strcat (buff, "Close##-1");
		
		grlibSettings.fontNormBMF = fonts[FNTBIG];
		item = grlib_menu (games[ai].name, buff);
		grlibSettings.fontNormBMF = fonts[FNTNORM];

		if (item >= 100)
			{
			int i = item - 100;
			
			if (i == 0) { gameConf.ios ++; if (gameConf.ios >= opt[i]) gameConf.ios = 0; }
			if (i == 1) { gameConf.vmode ++; if (gameConf.vmode >= opt[i]) gameConf.vmode = 0; }
			if (i == 2) { gameConf.vpatch ++; if (gameConf.vpatch >= opt[i]) gameConf.vpatch = 0; }
			if (i == 3) { gameConf.language ++; if (gameConf.language >= opt[i]) gameConf.language = 0; }
			if (i == 4) { gameConf.hook ++; if (gameConf.hook >= opt[i]) gameConf.hook = 0; }
			if (i == 5) { gameConf.ocarina ++; if (gameConf.ocarina >= opt[i]) gameConf.ocarina = 0; }
			if (i == 6) { gameConf.nand ++; if (gameConf.nand >= opt[i]) gameConf.nand = 0; }
			if (i == 7) { gameConf.loader ++; if (gameConf.loader >= opt[i]) gameConf.loader = 0; }
			if (i == 8)	gameConf.dmlvideomode = DMLVIDEOMODE_NTSC;
			if (i == 9)	gameConf.dmlvideomode = DMLVIDEOMODE_PAL;
			}
		else
			break;
		}
	while (TRUE);
	gameConf.language --;
	
	if (item == 5)
		{
		ShowGameFilterMenu (ai);
		WriteGameConfig (ai);
		AppsSort ();

		goto start;
		}

	if (item == 6)
		{
		int item;
		item = grlib_menu ("Reset play count ?", "Yes~No");
		
		if (item == 0)
			games[ai].playcount = 0;
		
		WriteGameConfig (ai);
		AppsSort ();
		
		goto start;
		}

	if (item == 2)
		{
		games[ai].hidden = 0;
		WriteGameConfig (ai);
		AppsSort ();
		}

	if (item == 3)
		{
		games[ai].hidden = 1;
		WriteGameConfig (ai);
		AppsSort ();
		}

	if (item == 7)
		{
		char path[256];
		
		int ret = grlib_menu ("Are you sure you want to delete this game ? ", "   YES   ~NO");
		if (ret == 0)
			{
			sprintf (path, "sd://games/%s", games[ai].asciiId);
			fsop_KillFolderTree (path, NULL);
			GameBrowse (1);
			}
		}

	if (item == 4)
		{
		int item;
		item = grlib_menu ("Vote Game", "10 (Best)|9|8|7|6|5 (Average)|4|3|2|1 (Bad)");
		
		if (item >= 0)
			games[ai].priority = 10-item;
		
		WriteGameConfig (ai);
		AppsSort ();
		}

	WriteGameConfig (ai);
	
	//GameBrowse (0);
	}

// Nand folder can be only root child...
#define MAXNANDFOLDERS 16
static int ScanForNandFolders (char **nf, int idx, char *device)
	{
	DIR *pdir;
	struct dirent *pent;
	char path[300];
	char nand[300];

	sprintf (path, "%s://", device);
	
	pdir=opendir(path);
	if (!pdir) return idx;
	
	while ((pent=readdir(pdir)) != NULL) 
		{
		if (idx == MAXNANDFOLDERS) break;
		
		sprintf (nand, "%s://%s/title/00000001", device, pent->d_name);

		if (fsop_DirExist(nand))
			{
			//grlib_dosm (nand);
			
			sprintf (nand, "%s://%s", device, pent->d_name);
			nf[idx] = malloc (strlen(nand)+1);
			strcpy (nf[idx], nand); // Store to the list
			idx++;
			}
		}
	closedir(pdir);
	
	return idx;
	}

static void ShowNandMenu (void)
	{
	char buff[300];
	char *nandFolders[MAXNANDFOLDERS];
	int nandFodersCnt = 0;

	
	//MountDevices (true);
	
	buff[0] = '\0';
	
	strcat (buff, "Use Real NAND##100|");
	if (vars.neek == NEEK_NONE)
		{
		if (NandExits(DEV_SD))
			grlib_menuAddItem (buff, 101, "sd://");

		if (NandExits(DEV_USB))
			grlib_menuAddItem (buff, 102, "usb://");

		nandFodersCnt = ScanForNandFolders (nandFolders, nandFodersCnt, "sd");
		nandFodersCnt = ScanForNandFolders (nandFolders, nandFodersCnt, "usb");

		int i, id = 200;
		for (i = 0;  i < nandFodersCnt; i++)
			grlib_menuAddItem (buff, id++, nandFolders[i]);
		}
		
	strcat (buff, "|");
	strcat (buff, "Cancel##-1");
		
	Redraw();
	grlib_PushScreen();
	
	int item = grlib_menu ("Select NAND Source", buff);
		
	if (item == 100)
		{
		config.chnBrowser.nand = NAND_REAL;
		browse = 1;
		}

	if (item == 101)
		{
		config.chnBrowser.nand = NAND_EMUSD;
		browse = 1;
		}

	if (item == 102)
		{
		config.chnBrowser.nand = NAND_EMUUSB;
		browse = 1;
		}
	
	config.chnBrowser.nandPath[0] = '\0';
	if (item >= 200)
		{
		int i = item - 200;
		char dev[10];
		
		strcpy (dev, "sd://");
		if (strstr (nandFolders[i], dev))
			{
			config.chnBrowser.nand = NAND_EMUSD;
			strcpy (config.chnBrowser.nandPath, &nandFolders[i][strlen(dev)-1]);
			}
			
		strcpy (dev, "usb://");
		if (strstr (nandFolders[i], dev))
			{
			config.chnBrowser.nand = NAND_EMUUSB;
			strcpy (config.chnBrowser.nandPath, &nandFolders[i][strlen(dev)-1]);
			}
		
		//grlib_dosm ("%d, %s", config.chnBrowser.nand, config.chnBrowser.nandPath);	
		browse = 1;
		}
	
	int i; for (i = 0; i < nandFodersCnt; i++) free (nandFolders[i]);
	}

static void ChangeDefaultLoader (void)
	{
	char buff[1024];
	
	buff[0] = '\0';
	
	// "CFG", "GX", "WiiFlow"
	
	grlib_menuAddItem (buff, 0, "CFG USB Loader");
	grlib_menuAddItem (buff, 1, "Usb Loader GX ");
	grlib_menuAddItem (buff, 2, "WiiFlow");
	
	int item = grlib_menu ("Select your default loader", buff);
	if (item >= 0)
		config.gameDefaultLoader = item;
		
	Redraw();
	grlib_PushScreen();

	int i;
	for (i = 0; i < gamesCnt; i++)
		{
		int ret = ReadGameConfig (i);
		//Debug ("ReadGameConfig %d, %d", i, ret);
		if (ret > 0)
			{
			gameConf.loader = config.gameDefaultLoader;
			Debug ("(update)ReadGameConfig %d, %d", i, ret);
			WriteGameConfig (i);
			}
		
		Video_WaitPanel (TEX_HGL, "Updating configuration files|%d%%", (i * 100)/gamesCnt);
		}
	}
	
static void ShowGamesOptions (void)
	{
	char buff[1024];
	
	//MountDevices (true);
	
	buff[0] = '\0';
	
	strcat (buff, "Download covers...##10|");
	if (config.gameMode == GM_WII)
		{
		strcat (buff, "Set default loader...##15|");
		}
	strcat (buff, "|");
	
	if (vars.neek != NEEK_NONE)
		{
		strcat (buff, "Rebuild game list: postLoader wbfs mode (reboot)...##12|");
		strcat (buff, "Rebuild game list: neek2o offical mode (reboot)...##9||");
		}
	else
		{
		if (config.gameMode == GM_WII)
			strcat (buff, "Rebuild game list (ntfs/fat)...##13|");
		else
			strcat (buff, "Rebuild game list (DML)...##13|");
		}
	
	strcat (buff, "Reset configuration files...##11||");
	
	strcat (buff, "Cancel##-1");
		
	Redraw();
	grlib_PushScreen();
	
	grlibSettings.fontNormBMF = fonts[FNTBIG];
	int item = grlib_menu ("Games Options", buff);
	grlibSettings.fontNormBMF = fonts[FNTNORM];
		
	if (item == 9)
		{
		neek_KillDIConfig ();
		Shutdown (0);
		SYS_ResetSystem(SYS_RETURNTOMENU,0,0);
		}

	if (item == 12)
		{
		neek_CreateCDIConfig ();
		Shutdown (0);
		SYS_ResetSystem(SYS_RETURNTOMENU,0,0);
		}

	if (item == 13)
		{
		GameBrowse (1);
		}
		
	if (item == 10)
		{
		DownloadCovers();
		}

	if (item == 11)
		{
		CleanTitleConfiguration();
		}
	
	if (item == 15)
		{
		ChangeDefaultLoader ();
		}
	}

static void ShowMainMenu (void)
	{
	char buff[512];
	
	start:
	
	buff[0] = '\0';
	
	grlib_menuAddItem (buff, 100, "Browse to...");
	grlib_menuAddItem (buff,  8, "Game options...");

	if (config.gameSort == 0)
		grlib_menuAddItem (buff,  9, "Sort by: vote");
	if (config.gameSort == 1)
		grlib_menuAddItem (buff,  9, "Sort by: name");
	if (config.gameSort == 2)
		grlib_menuAddItem (buff,  9, "Sort by: play count");
	
	grlib_menuAddItem (buff,  3, "Select filters");

	if (showHidden)
		grlib_menuAddItem (buff,  6, "Hide hidden titles");
	else
		grlib_menuAddItem (buff,  7, "Show hidden titles");
		
	grlib_menuAddColumn (buff);
	grlib_menuAddItem (buff,  4, "Run System menu");
	grlib_menuAddItem (buff, 20, "Run BOOTMII");
	grlib_menuAddItem (buff, 21, "Run DISC");
	grlib_menuAddItem (buff,  5, "System options...");
	grlib_menuAddItem (buff, -1, "Close");
		
	Redraw();
	grlib_PushScreen();
	
	grlibSettings.fontNormBMF = fonts[FNTBIG];
	int item = grlib_menu ("Games menu", buff);
	grlibSettings.fontNormBMF = fonts[FNTNORM];
	
	if (item == 9)
		{
		config.gameSort ++;
		if (config.gameSort > 2) config.gameSort = 0;
		AppsSort ();
		goto start;
		}

	if (item == 100)
		{
		browserRet = Menu_SelectBrowsingMode ();
		}
		
	if (item == 3)
		{
		ShowFilterMenu ();
		goto start;
		}
		
	if (item == 4)
		{
		browserRet = INTERACTIVE_RET_HOME;
		}
		
	if (item == 5)
		{
		ShowAboutMenu ();
		}

	if (item == 6)
		{
		showHidden = 0;
		AppsSort ();
		}

	if (item == 7)
		{
		showHidden = 1;
		AppsSort ();
		}
		
	if (item == 20)
		{
		browserRet = INTERACTIVE_RET_BOOTMII;
		}

	if (item == 21)
		{
		browserRet = INTERACTIVE_RET_DISC;
		}

	if (item == 1)
		{
		ShowNandMenu();
		goto start;
		}
	
	if (item == 8)
		{
		ShowGamesOptions();
		if (browserRet) return;
		goto start;
		}
	}

static void RedrawIcons (int xoff, int yoff)
	{
	int i;
	int ai;	// Application index (corrected by the offset)
	char path[128];
	
	// Prepare black box
	for (i = 0; i < gui.spotsXpage; i++)
		{
		// Make sure that icon is not in sel state
		gui.spots[i].ico.sel = false;
		gui.spots[i].ico.title[0] = '\0';
		gui.spots[i].ico.xoff = xoff;
		}
	
	// Draw Icons
	gui.spotsIdx = 0;
	for (i = 0; i < gui.spotsXpage; i++)
		{
		ai = (page * gui.spotsXpage) + i;
		
		if (ai < gamesCnt && ai < games2Disp && gui.spotsIdx < SPOTSMAX)
			{
			MakeCoverPath (ai, path);
			gui.spots[gui.spotsIdx].ico.icon = CoverCache_Get(path);
				
			if (!gui.spots[gui.spotsIdx].ico.icon)
				strcpy (gui.spots[gui.spotsIdx].ico.title, games[ai].name);
			else
				gui.spots[gui.spotsIdx].ico.title[0] = '\0';

			if (config.gameMode == GM_DML)
				{
				if (games[ai].slot)
					gui.spots[gui.spotsIdx].ico.transparency = 128;
				else
					gui.spots[gui.spotsIdx].ico.transparency = 255;
				}
				
			grlib_IconDraw (&is, &gui.spots[gui.spotsIdx].ico);
			
			// Let's add the spot points, to reconize the icon pointed by wiimote
			gui.spots[gui.spotsIdx].id = ai;
			
			gui.spotsIdx++;
			}
		else
			{
			s_grlib_icon ico;
			grlib_IconInit (&ico, &gui.spots[i].ico);

			ico.noIcon = true;
			grlib_IconDraw (&is, &ico);
			}
		}
	}

static void Redraw (void)
	{
	char sdev[64];
	
	Video_DrawBackgroud (1);
	
	if (config.gameMode == GM_WII)
		strcpy (sdev, "WII Games");
	else
		strcpy (sdev, "GC Games(DML)");

	grlib_SetFontBMF(fonts[FNTNORM]);
	char ahpbrot[16];
	if (vars.ahbprot)
		strcpy (ahpbrot,"+");
	else
		strcpy (ahpbrot,"");
	
	if (vars.neek == NEEK_NONE)
		grlib_printf ( 25, 26, GRLIB_ALIGNLEFT, 0, "postLoader"VER" (IOS%d%s) - %s", vars.ios, ahpbrot, sdev);
	else
		{
		char neek[32];
		
		if (vars.neek == NEEK_2o)
			strcpy (neek, "neek2o");
		else
			strcpy (neek, "neek");

		if (strlen(vars.neekName))
			grlib_printf ( 25, 26, GRLIB_ALIGNLEFT, 0, "postLoader"VER" (%s %s) - Games", neek, vars.neekName);
		else
			grlib_printf ( 25, 26, GRLIB_ALIGNLEFT, 0, "postLoader"VER" (%s) - Games", neek);
		}
		
	grlib_printf ( 615, 26, GRLIB_ALIGNRIGHT, 0, "Page %d of %d", page+1, pageMax+1);
	
	if (redrawIcons) RedrawIcons (0,0);

	grlib_SetFontBMF(fonts[FNTNORM]);
	
	if (gamesCnt == 0 && scanned)
		{
		grlib_DrawCenteredWindow ("No games found !", WAITPANWIDTH, 133, 0, NULL);
		Video_DrawIcon (TEX_EXCL, 320, 250);
		}
		
	refreshPng = 0;
	}
	
static void Overlay (void)
	{
	Video_DrawWIFI ();
	return;
	}

static int ChangePage (int next)
	{
	if (next)
		{
		if (page == pageMax) return page;
		page++;
		}
	else
		{
		if (page == 0) return page;
		page--;
		}
		
	FeedCoverCache ();

	redrawIcons = false;
	Redraw ();
	grlib_PushScreen ();
	
	int x = 0, lp;
	
	if (!next)
		{
		do
			{
			x-=20;

			grlib_PopScreen ();

			lp = page;
			RedrawIcons (x + 640,0);
			page = lp+1;
			RedrawIcons (x,0);
			page = lp;
			
			Overlay ();
			grlib_GetUserInput();
			grlib_DrawIRCursor ();
			grlib_Render();
			
			usleep (1);
			}
		while (x > -640);
		}
	else
		{
		do
			{
			x+=20;

			grlib_PopScreen ();

			lp = page;
			RedrawIcons (x - 640,0);
			page = lp-1;
			RedrawIcons (x,0);
			page = lp;
			
			Overlay ();
			grlib_GetUserInput();
			grlib_DrawIRCursor ();
			grlib_Render();
			
			usleep (1);
			}
		while (x < 640);
		}
	
	redrawIcons = true;
	redraw = 1;
	
	return page;
	}

static void cb_filecopy (void)
	{
	u32 mb = (u32)(fsop.multy.bytes/1000000);
	u32 sizemb = (u32)(fsop.multy.size/1000000);
	u32 perc = (mb * 100)/sizemb;
	
	Video_WaitPanel (TEX_HGL, "Please wait: %u%% done|Copying %u of %u Mb (%u Kb/sec)", perc, mb, sizemb, (u32)(fsop.multy.bytes/fsop.multy.elapsed));
	
	if (grlib_GetUserInput() & WPAD_BUTTON_B)
		{
		int ret = grlib_menu ("This will interrupt the copy process... Are you sure", "   Yes   ##0~No##-1");
		if (ret == 0) fsop.breakop = 1;
		}
	}

static size_t GetGCGameUsbKb (int ai)
	{
	char path[300];
	
	sprintf (path, "usb://ngc/%s", games[ai].asciiId);
	return fsop_GetFolderKb (path, NULL);
	}

static bool MoveGCGame (int ai)
	{
	char source[300];
	char target[300];
	
	snd_Pause ();
	DMLResetCache ();

	Debug ("MoveGCGame (%s %s): Started", games[ai].name, games[ai].asciiId);

	sprintf (source, "usb://ngc/%s", games[ai].asciiId);
	sprintf (target, "sd://games/%s", games[ai].asciiId);

	bool ret = fsop_CopyFolder (source, target, cb_filecopy);
	
	if (!ret || fsop.breakop)
		{
		Debug ("MoveGCGame: interrupted... removing partial folder");
		fsop_KillFolderTree (target, NULL);
		}
		
	snd_Resume ();
	
	return ret;
	}

static bool QuerySelection (int ai)
	{
	int i;
	float mag = 1.0;
	int spot = -1;
	int incX = 1, incY = 1;
	int y = 220;
	
	for (i = 0; i < gui.spotsIdx; i++)
		{
		if (ai == gui.spots[i].id)
			spot = i;
		}
	
	if (spot < 0) return false;

	s_grlib_icon ico;
	grlib_IconInit (&ico, &gui.spots[spot].ico);
	ico.sel = true;
	
	s_grlib_iconSetting istemp;
	memcpy (&istemp, &is, sizeof(s_grlib_iconSetting));
	
	s_grlibobj black;
	black.x1 = 0;
	black.y1 = 0;
	black.x2 = grlib_GetScreenW();
	black.y2 = grlib_GetScreenH();
	black.color = RGBA(0,0,0,192);
	black.bcolor = RGBA(0,0,0,192);
	
	while (true)
		{
		grlib_PopScreen ();
		grlib_DrawSquare (&black);

		istemp.magXSel = mag;
		istemp.magYSel = mag;
		
		incX = abs(ico.x - 320);
		if (incX > 10) incX = 10;

		incY = abs(ico.y - y);
		if (incY > 10) incY = 10;

		if (ico.x < 320) ico.x += incX;
		if (ico.x > 320) ico.x -= incX;

		if (ico.y < y) ico.y += incY;
		if (ico.y > y) ico.y -= incY;
		
		grlib_IconDraw (&istemp, &ico);

		Overlay ();
		grlib_GetUserInput();
		grlib_DrawIRCursor ();
		grlib_Render();
		
		if (mag < 2.3) mag += 0.05;
		if (mag >= 2.3 && ico.x == 320 && ico.y == y) break;
		}
	
	int fr = grlibSettings.fontBMF_reverse;
	u32 btn;
	while (true)
		{
		grlib_PopScreen ();
		grlib_DrawSquare (&black);
		grlib_IconDraw (&istemp, &ico);
		Overlay ();
		
		grlibSettings.fontBMF_reverse = 0;
		grlib_printf (XMIDLEINFO, theme.line1Y, GRLIB_ALIGNCENTER, 0, games[ai].name);		
		grlib_printf (XMIDLEINFO, theme.line2Y, GRLIB_ALIGNCENTER, 0, "Press (A) to start, (B) Cancel");
		grlibSettings.fontBMF_reverse = fr;
		
		grlib_GetUserInput();
		grlib_DrawIRCursor ();
		grlib_Render();

		btn = grlib_GetUserInput();
		if (btn & WPAD_BUTTON_A) return true;
		if (btn & WPAD_BUTTON_B) return false;
		}
	return true;
	}

static void Conf (bool open)
	{
	char cfgpath[64];
	sprintf (cfgpath, "%s://ploader/games.conf", vars.defMount);

	if (open)
		{
		cfg = cfg_Alloc (cfgpath, 0);
		}
	else
		{
		cfg_Store (cfg, cfgpath);
		cfg_Free (cfg);
		}
	}
		
int GameBrowser (void)
	{
	u32 btn;

	Debug ("GameBrowser (begin)");
	
	Conf (true);

	scanned = 0;
	browserRet = -1;

	grlibSettings.color_window = RGBA(192,192,192,255);
	grlibSettings.color_windowBg = RGBA(32,32,32,128);
	
	grlib_SetRedrawCallback (Redraw, Overlay);
	
	games = calloc (CHNMAX, sizeof(s_game));
	
	// Immediately draw the screen...
	StructFree ();
	InitializeGui ();
	
	Redraw ();
	grlib_PushScreen ();
	grlib_PopScreen ();
	grlib_Render();  // Render the theme.frame buffer to the TV
	
	page = config.gamePage;
	GameBrowse (0);

	LiveCheck (1);

	if (config.gamePage >= 0 && config.gamePage <= pageMax)
		page = config.gamePage;
	else
		page = 0;
		
	FeedCoverCache ();

	redraw = 1;
	
	// Loop forever
    while (browserRet == -1) 
		{
		if (LiveCheck (0)) redraw = 1;
		
		btn = grlib_GetUserInput();
		
		// If [HOME] was pressed on the first Wiimote, break out of the loop
		if (btn)
			{
			browserRet = ChooseDPadReturnMode (btn);
			if (browserRet != -1) break;
			
			if (btn & WPAD_BUTTON_1) 
				{
				page = GoToPage (page, pageMax);
				FeedCoverCache ();
				redraw = 1;
				}
			
			if (btn & WPAD_BUTTON_A && gamesSelected != -1) 
				{
				if (!QuerySelection (gamesSelected))
					{
					redraw = 1;
					continue;
					}
				if (config.gameMode == GM_WII)
					{
					ReadGameConfig (gamesSelected);
					games[gamesSelected].playcount++;
					WriteGameConfig (gamesSelected);
					
					memcpy (&config.run.game, &gameConf, sizeof(s_gameConfig));
					config.run.neekSlot = games[gamesSelected].slot;
					strcpy (config.run.asciiId, games[gamesSelected].asciiId);
					
					browserRet = INTERACTIVE_RET_GAMESEL;
					}
				else
					{
					bool err = false;
					
					Debug ("gamebrowser: requested dml");

					if (games[gamesSelected].slot)
						{
						int ret = DMLInstall (games[gamesSelected].name, GetGCGameUsbKb(gamesSelected));

						Redraw();
						grlib_PushScreen();

						if (ret)
							err = !MoveGCGame (gamesSelected);
						else
							err = true;
						}
					
					Debug ("gamebrowser: requested dml (err = %d)", err);

					if (!err)
						{
						Debug ("DMLRun");
						ReadGameConfig (gamesSelected);
						config.dmlvideomode = gameConf.dmlvideomode;
						games[gamesSelected].playcount++;
						WriteGameConfig (gamesSelected);
						Conf (false);	// Store configuration on disc
						
						DMLRun (games[gamesSelected].asciiId);
						}
					}
				
				browse = 1;
				}
				
			/////////////////////////////////////////////////////////////////////////////////////////////
			// Select application as default
			if (btn & WPAD_BUTTON_B && gamesSelected != -1)
				{
				ShowAppMenu (gamesSelected);
				redraw = 1;
				}

			if (btn & WPAD_BUTTON_2)
				{
				ShowFilterMenu ();
				redraw = 1;
				}
				
			if (btn & WPAD_BUTTON_HOME)
				{
				ShowMainMenu ();
				redraw = 1;
				}
			
			if (btn & WPAD_BUTTON_MINUS && page > 0)
				{
				page = ChangePage (0);
				}
			if (btn & WPAD_BUTTON_PLUS  && page < pageMax) 
				{
				page = ChangePage (1);
				}
			}
		
		if (CoverCache_IsUpdated ()) redraw = 1;
		
		if (redraw)
			{
			Redraw ();
			grlib_PushScreen ();
			
			redraw = 0;
			}
		
		grlib_PopScreen ();
		FindSpot ();
		Overlay ();
		grlib_DrawIRCursor ();
        grlib_Render();  // Render the theme.frame buffer to the TV
		
		if (browse)
			{
			browse = 0;
			GameBrowse (0);
			redraw = 1;
			}
		
		if (grlibSettings.wiiswitch_poweroff || grlibSettings.wiiswitch_reset)
			{
			browserRet = INTERACTIVE_RET_SHUTDOWN;
			break;
			}

		if (wiiload.status == WIILOAD_HBZREADY)
			{
			WiiloadZipMenu ();
			redraw = 1;
			}
			
		if (wiiload.status == WIILOAD_HBREADY)
			{
			if (WiiloadPostloaderDolMenu())
				browserRet = INTERACTIVE_RET_WIILOAD;
			else
				redraw = 1;
			break;
			}
			
		if (vars.themeReloaded) // Restart the browser
			{
			vars.themeReloaded = 0;
			browserRet = INTERACTIVE_RET_TOGAMES;
			}
		
		usleep (5000);
		}

	// save current page
	config.gamePage = page;
	
	Conf (false);

	// Clean up all data
	StructFree ();
	gui_Clean ();
	free (games);
	
	grlib_SetRedrawCallback (NULL, NULL);
	
	return browserRet;
	}
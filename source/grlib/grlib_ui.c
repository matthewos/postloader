#include <stdarg.h>
#include "grlib.h"
#include <wiiuse/wpad.h>

extern s_grlibSettings grlibSettings;

//DrawOnScreenMessage
int grlib_dosm (const char *text, ...) // ab > 0 show and wait ab second, otherwhise, wait a or b press
	{
	int ret;
	static char mex[1024];
	char *p1,*p2;
	
	int reverse = grlibSettings.fontDef.reverse;
	
	grlibSettings.fontDef.reverse = 0;
	
	if (text != NULL)
		{
		va_list argp;
		va_start(argp, text);
		vsprintf(mex, text, argp);
		va_end(argp);
		}

	// wait for no-button pressed
	do {WPAD_ScanPads();} while (WPAD_ButtonsDown(0));
	
	s_grlibobj b;

	b.x1 = 70;
	b.y1 = 120;
	b.x2 = grlib_GetScreenW()-b.x1;
	b.y2 = grlib_GetScreenH()-b.y1;
	b.color = RGBA (255, 2552, 255, 255);
	b.bcolor = RGBA (0, 0, 0, 192);
	
	grlib_PopScreen ();
	grlib_DrawSquare (&b);
	grlib_DrawBoldEmptySquare (&b);
	
	b.x1 += 10;
	b.y1 += 10;
	
	p1 = mex;
	do
		{
		p2 = strstr (p1, "\n");
		if (p2 != NULL)
			{
			*p2 = 0;
			p2++;
			}
		grlib_printf ( b.x1, b.y1, GRLIB_ALIGNLEFT, 0, p1);
		b.y1 +=15;
		p1 = p2;
		}
	while (p2 != NULL);

	grlib_Render ();
	
	ret = 0;
	do 
		{
		WPAD_ScanPads();
		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_A) ret = 1;
		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_B) ret = 2;
		} 
	while (ret == 0);
	do {WPAD_ScanPads();} while (WPAD_ButtonsDown(0));
	
	grlibSettings.fontDef.reverse = reverse;
	return ret;
	}
	
// if text start with "^+" the string is aligned to left, and [*] is drawn
// if text start with "^-" the string is aligned to left, and [ ] is drawn
void grlib_DrawButton ( s_grlibobj *b, int state)
	{
	int w, h;
	char text[64];
	int toggle = -1;
	
	if (b->text[0] == '�')
		{
		if (b->text[1] == '+') toggle = 1;
		if (b->text[1] == '-') toggle = 0;
		
		if (toggle == 0) sprintf (text, "[%cX] %s", 255, &b->text[2]);
		if (toggle == 1) sprintf (text, "[X] %s", &b->text[2]);
		}
	
	if (toggle == -1) strcpy(text, b->text);

	if (strlen(text) > 0 && grlib_GetFontMetrics(text, NULL, NULL) > (b->x2 - b->x1) - 5)
		{
		int pt3 = grlib_GetFontMetrics("...", NULL, NULL);
		bool cutted = FALSE;
		while (strlen(text) > 0 && grlib_GetFontMetrics(text, NULL, NULL) > ((b->x2 - b->x1) - 5 - pt3))
			{
			text[strlen(text)-1] = 0;
			cutted = TRUE;
			}
		if (cutted) strcat (text, "...");
		}

	if (grlibSettings.theme.enabled)
		{
		if (state == BTNSTATE_SEL && grlibSettings.theme.texButtonSel)
			grlib_DrawSquareThemed ( b, grlibSettings.theme.texButtonSel, NULL, grlibSettings.theme.buttonMagX, grlibSettings.theme.buttonMagY, DSTF_NONE, 255);
		else
			grlib_DrawSquareThemed ( b, grlibSettings.theme.texButton, NULL, grlibSettings.theme.buttonMagX, grlibSettings.theme.buttonMagY, DSTF_NONE, 255);
		}
	else
		grlib_DrawSquare ( b );
	
	grlib_GetFontMetrics (text, &w, &h);
	if (toggle == -1)
		{
		if (b->vAlign == GRLIB_ALIGNBOTTOM)
			{
			grlib_printf (b->x1 + (b->x2 - b->x1) / 2, b->y2 - h - 7 + grlibSettings.theme.buttonsTextOffsetY, GRLIB_ALIGNCENTER, 0, text);
			}
		else
			{
			grlib_printf (b->x1 + (b->x2 - b->x1) / 2, b->y1 + (b->y2 - b->y1) / 2 - (h / 2) + grlibSettings.theme.buttonsTextOffsetY, GRLIB_ALIGNCENTER, 0, text);
			}
		}
	else
		{
		if (b->vAlign == GRLIB_ALIGNBOTTOM)
			{
			grlib_printf (b->x1 + 5, b->y2 - h - 7 + grlibSettings.theme.buttonsTextOffsetY, GRLIB_ALIGNLEFT, 0, text);
			}
		else
			{
			grlib_printf (b->x1 + 5, b->y1 + (b->y2 - b->y1) / 2 - (h / 2) + grlibSettings.theme.buttonsTextOffsetY, GRLIB_ALIGNLEFT, 0, text);
			}
		}
	}
	
void grlib_Message (const char *text, ...) // ab > 0 show and wait ab second, otherwhise, wait a or b press
	{
	char * mex;
	
	mex = calloc (1, strlen(text) + 1024);
	if (!mex) return;
	
	mex[0] = '\0';
	
	if (text != NULL)
		{
		va_list argp;
		va_start(argp, text);
		vsprintf(mex, text, argp);
		va_end(argp);
		}
	
	grlib_menu	(50, mex, "OK");
	
	free (mex);
	}

void grlib_DrawWindow (s_grlibobj go)
	{
	if (grlibSettings.theme.enabled)
		{
		grlib_DrawSquareThemed (&go, grlibSettings.theme.texWindow, grlibSettings.theme.texWindowBk, grlibSettings.theme.windowMagX, grlibSettings.theme.windowMagY, DSTF_BKFILLBORDER, 255);
		}
	else
		{
		grlib_DrawSquare (&go);
		grlib_DrawBoldEmptySquare (&go);
		}
	}

#include <stdio.h>
#include <gccore.h>		/*** Wrapper to include common libogc headers ***/
#include <ogcsys.h>		/*** Needed for console support ***/
#include <ogc/dvd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include "banner.h"

/*** 2D Video Globals ***/
GXRModeObj *vmode;		/*** Graphics Mode Object ***/
u32 *xfb[2] = { NULL, NULL };    /*** Framebuffers ***/
int whichfb = 0;        /*** Frame buffer toggle ***/
void ProperScanPADS(){	PAD_ScanPads(); }

// Wii stuff
#include <ogc/es.h>
#include <ogc/ipc.h>
#include <ogc/ios.h>
#define IOCTL_DI_READID				0x70
#define IOCTL_DI_READ				0x71
#define IOCTL_DI_RESET				0x8A
#define IOCTL_DI_SETAUDIO           0xE4

static int __dvd_fd 		= -1;
static int previously_initd =  0;
static char __di_fs[] ATTRIBUTE_ALIGN(32) = "/dev/di";

u8 dicommand [32]   ATTRIBUTE_ALIGN(32);
u8 dibufferio[32]   ATTRIBUTE_ALIGN(32);
static tikview view ATTRIBUTE_ALIGN(32);

/* Synchronous DVD stuff.. bad! */
/* Open /dev/di */
int WiiDVD_Init() {
	if(!previously_initd) {
		int ret;
		ret = IOS_Open(__di_fs,0);
		if(ret<0) return ret;
		__dvd_fd = ret;
		previously_initd = 1;
	}
	return 0;
}

/* Resets the drive, spins up the media */
void WiiDVD_Reset() {
	memset(dicommand, 0, 32 );
	dicommand[0] = IOCTL_DI_RESET;
	((u32*)dicommand)[1] = 1; //spinup(?)
	IOS_Ioctl(__dvd_fd,dicommand[0],&dicommand,0x20,NULL,0);
}

/* Read the Disc ID */
int WiiDVD_ReadID(void *dst) {
	int ret;
	memset(dicommand, 0, 32 );
	dicommand[0] = IOCTL_DI_READID;
	ret = IOS_Ioctl(__dvd_fd,dicommand[0],&dicommand,0x20,(void*)0x80000000,0x20);
	return ret;
}

int WiiDVD_EnableAudio(int enable) {
	int ret;
	memset(dicommand, 0, 32);
	dicommand[0] = IOCTL_DI_SETAUDIO;
	if (enable) {
		dicommand[7] = 1;
		dicommand[11] = 0xA;
	}
	ret = IOS_Ioctl(__dvd_fd,dicommand[0],&dicommand,0x20,NULL,0);
	return ret;
}

/****************************************************************************
* Initialise Video
*
* Before doing anything in libogc, it's recommended to configure a video
* output.
****************************************************************************/
static void Initialise (void)
{
  VIDEO_Init ();        /*** ALWAYS CALL FIRST IN ANY LIBOGC PROJECT!
                     Not only does it initialise the video
                     subsystem, but also sets up the ogc os
                ***/

  PAD_Init ();            /*** Initialise pads for input ***/

    /*** Try to match the current video display mode
         using the higher resolution interlaced.

         So NTSC/MPAL gives a display area of 640x480
         PAL display area is 640x528
    ***/
  vmode = VIDEO_GetPreferredMode(NULL);
    /*** Let libogc configure the mode ***/
  VIDEO_Configure (vmode);

    /*** Now configure the framebuffer.
         Really a framebuffer is just a chunk of memory
         to hold the display line by line.
    ***/

  xfb[0] = (u32 *) MEM_K0_TO_K1 (SYS_AllocateFramebuffer (vmode));
    /*** I prefer also to have a second buffer for double-buffering.
         This is not needed for the console demo.
    ***/
  xfb[1] = (u32 *) MEM_K0_TO_K1 (SYS_AllocateFramebuffer (vmode));
     /*** Define a console ***/
    		/*			x	y     w   h			*/
  console_init (xfb[0], 50, 180, vmode->fbWidth,480, vmode->fbWidth * 2);
    /*** Clear framebuffer to black ***/
  VIDEO_ClearFrameBuffer (vmode, xfb[0], COLOR_BLACK);
  VIDEO_ClearFrameBuffer (vmode, xfb[1], COLOR_BLACK);

    /*** Set the framebuffer to be displayed at next VBlank ***/
  VIDEO_SetNextFramebuffer (xfb[0]);

    /*** Get the PAD status updated by libogc ***/
  VIDEO_SetPostRetraceCallback (ProperScanPADS);
  VIDEO_SetBlack (0);

    /*** Update the video for next vblank ***/
  VIDEO_Flush ();

  VIDEO_WaitVSync ();        /*** Wait for VBL ***/
  if (vmode->viTVMode & VI_NON_INTERLACE)
    VIDEO_WaitVSync ();
}

void wait_press_A()
{
	printf("Press A to continue..\n");
	while((PAD_ButtonsHeld(0) & PAD_BUTTON_A));
	while(!(PAD_ButtonsHeld(0) & PAD_BUTTON_A));
}

void drawdot(void *xfb, GXRModeObj *rmode, float w, float h, float fx, float fy, u32 color) {
	u32 *fb;
	int px,py;
	int x,y;
	fb = (u32*)xfb;
 
	y = fy * rmode->xfbHeight / h;
	x = fx * rmode->fbWidth / w / 2;
 
	for(py=y-4; py<=(y+4); py++) {
		if(py < 0 || py >= rmode->xfbHeight)
				continue;
		for(px=x-2; px<=(x+2); px++) {
			if(px < 0 || px >= rmode->fbWidth/2)
				continue;
			fb[rmode->fbWidth/VI_DISPLAY_PIX_SZ*py + px] = color;
		}
	}
 
}
// so it's actually arrrrrgggggbbbbb not rrrrrgggggbbbbba... man I wish people would label better.
u32 rgbToyuv(u16 rgba)
{
	
	u16  R = (rgba & 0x7c00) >>8;
	u16  G = (rgba & 0x03e0) >> 3;
	u16  B = (rgba & 0x001F) << 2;
	u8 y = RGB2Y(R, G, B);
	u8 u = RGB2U(R, G, B);
	u8 v = RGB2V(R, G, B);
	return y << 24 |u << 16 | y << 8 | v;  //YUYV
}

// working, Finally!  todo: make it not rely on drawdot.. mke crappy stuff at the bottom.
void drawBanner(u16* src)
{
	 // per-texture constants
  uint tileW = 4;
  uint tileH = 4;
  uint widthInTiles = 24;



	// ok... so the reason why this isn't working is because the data is stored in 4x4 pixel blocks moving from the left to the right. bah./
	int cols = 96;
	int rows = 32;
	int col = 0;
	int row= 0;
	for (row= 0; row < rows; row++)
	{
		for (col= 0;col < cols;col++) // draw from the fb's point of view.
		{
			
			uint tileX = col / tileW;
			uint tileY = row / tileH;
			uint inTileX =col % tileW;
			uint inTileY = row % tileH;
			
			u16 pixel = *(u16*)(src + (tileY * widthInTiles + tileX) * (tileW * tileH)
                + inTileY * tileW
                + inTileX);
			u32 color = rgbToyuv( pixel);
			drawdot(xfb[0], vmode, vmode->fbWidth/2, vmode->xfbHeight/2,col,row+1,color   );
			//xfb[0][vmode->fbWidth*row + col] = color;
			//drawdot(xfb[0], vmode, vmode->fbWidth, vmode->xfbHeight, i,j,rgbToyuv2(0xFFFF));
		}
	}
}


void printBanner(unsigned char banner[])
{
	printf("Short Name: %s\n",&banner[BANNER_GAME_NAME]);
	printf("Short Maker: %s\n",&banner[BANNER_COMPANY]);
	printf("Long Name: %s\n",&banner[BANNER_GAME_TITLE]);
	printf("Long Maker: %s\n",&banner[BANNER_COMPANY_DESC]);
	printf("Description: %s\n",&banner[BANNER_GAME_DESC]);

}


/****************************************************************************
* Main
****************************************************************************/
#define BC 		0x0000000100000100ULL
int main ()
{

	Initialise();

	printf("\n\n\n\n\nGCBooter v1.1.1 - Daddy Edition\n\n\n");
	printBanner(bnr);
	drawBanner(( u16*)&bnr[BANNER_PIC]);
	//drawBanner(( u16*)test);
	printf("Please Insert a Nintendo Gamecube Disk\n\n\n");
	wait_press_A();

	printf("Opening /dev/di/ ..\n");
	int ret = WiiDVD_Init();
	printf("Got %d\n", ret);
	//wait_press_A();

	printf("Resetting DI interface ..\n");
	WiiDVD_Reset();
	//wait_press_A();

	printf("Reading Disc ID ..\n");
	ret = WiiDVD_ReadID((void*)0x80000000);
	printf("Got %d\n", ret);
	//wait_press_A();

	printf("Setting DI audio %s ..\n", *(u8*)0x80000008 ? "enable" : "disable");
	ret = WiiDVD_EnableAudio(*(u8*)0x80000008);
	printf("Got %d\n", ret);
	wait_press_A();

	printf("Launching Game ..\n");
	*(volatile unsigned int *)0xCC003024 |= 7;

	int retval = ES_GetTicketViews(BC, &view, 1);

	if (retval != 0) printf("ES_GetTicketViews fail %d\n",retval);

	VIDEO_SetBlack(1);
	VIDEO_Flush();
	VIDEO_WaitVSync(); VIDEO_WaitVSync();

	retval = ES_LaunchTitle(BC, &view);
	printf("ES_LaunchTitle fail %d\n",retval);
	while(1);
	return 0;
}



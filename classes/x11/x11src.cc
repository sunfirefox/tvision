/* X11 screen routines header.
   Copyright (c) 2001 by Salvador E. Tropea (SET)
   Covered by the GPL license. */
#include <tv/configtv.h>

#if defined(TVOS_UNIX) && defined(HAVE_X11)
#define Uses_stdio
#define Uses_stdlib
#define Uses_string
#define Uses_unistd   // TScreenX11::System
#define Uses_TDisplay
#define Uses_TScreen
#define Uses_TGKey    // For TGKeyX11
#define Uses_TEvent   // For THWMouseX11
#include <tv.h>

// X11 defines their own values
#undef True
#undef False

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <tv/x11scr.h>
#include <tv/x11key.h>
#include <tv/x11mouse.h>

#include <locale.h>
#include <signal.h>
#include <sys/time.h>

const int cursorDelay=300000;

/*****************************************************************************

  TScreenX11 screen stuff.

*****************************************************************************/

Display  *TScreenX11::disp;
ulong     TScreenX11::screen;
Visual   *TScreenX11::visual;
Window    TScreenX11::rootWin;
Window    TScreenX11::mainWin;
Colormap  TScreenX11::cMap;
GC        TScreenX11::gc;
GC        TScreenX11::cursorGC;
XIC       TScreenX11::xic;
XIM       TScreenX11::xim;
Atom      TScreenX11::theProtocols;
ulong     TScreenX11::colorMap[16];
XImage   *TScreenX11::ximgFont[256];    /* Our "font" is just a collection of images */
XImage   *TScreenX11::cursorImage;
int       TScreenX11::fg;
int       TScreenX11::bg;
char      TScreenX11::cursorEnabled=1;
char      TScreenX11::cursorInScreen=0;
uchar     TScreenX11::curAttr;
char     *TScreenX11::cursorData;
volatile
char      TScreenX11::cursorChange=0;

TScreenX11::~TScreenX11() {}

void TScreenX11::clearScreen()
{
 XSetForeground(disp,gc,colorMap[bg]);
 XFillRectangle(disp,mainWin,gc,0,0,maxX*fontW,maxY*fontH);
 XSetForeground(disp,gc,colorMap[fg]);

 char space[2];
 space[charPos]=' ';
 space[attrPos]=curAttr;

 unsigned c=maxX*maxY;
 while (c--)
   screenBuffer[c]=*((ushort *)space);
}

void TScreenX11::setCharacter(unsigned offset, ushort value)
{
 screenBuffer[offset]=value;

 unsigned x,y;
 x=(offset%maxX)*fontW;
 y=(offset/maxX)*fontH;

 uchar *theChar=(uchar *)(screenBuffer+offset);
 XSetBackground(disp,gc,colorMap[theChar[attrPos]>>4]);
 XSetForeground(disp,gc,colorMap[theChar[attrPos]&0xF]);
 UnDrawCursor();
 XPutImage(disp,mainWin,gc,ximgFont[theChar[charPos]],0,0,x,y,fontW,fontH);
 XFlush(disp);
}

void TScreenX11::setCharacters(unsigned offset, ushort *values, unsigned count)
{
 unsigned x,y;
 x=(offset%maxX)*fontW;
 y=(offset/maxX)*fontH;

 uchar *b=(uchar *)values,newChar,newAttr;
 uchar *sb=(uchar *)(screenBuffer+offset);
 unsigned oldAttr=0x100;
 UnDrawCursor();
 while (count--)
   {
    newChar=b[charPos];
    newAttr=b[attrPos];
    if (newChar!=sb[charPos] || newAttr!=sb[attrPos])
      {
       sb[charPos]=newChar;
       sb[attrPos]=newAttr;
       if (newAttr!=oldAttr)
         {
          XSetBackground(disp,gc,colorMap[newAttr>>4]);
          XSetForeground(disp,gc,colorMap[newAttr&0xF]);
          oldAttr=newAttr;
         }
       XPutImage(disp,mainWin,gc,ximgFont[newChar],0,0,x,y,fontW,fontH);
      }
    x+=fontW; b+=2; sb+=2;
   }
 XFlush(disp);
}

int TScreenX11::System(const char *command, pid_t *pidChild)
{
 if (!pidChild)
    return system(command);

 pid_t cpid=fork();
 if (cpid==0)
   {// Ok, we are the child
    //   I'm not sure about it, but is the best I have right now.
    //   Doing it we can kill this child and all the subprocesses
    // it creates by killing the group. It also have an interesting
    // effect that I must evaluate: By doing it this process lose
    // the controlling terminal and won't be able to read/write
    // to the parents console. I think that's good.
    if (setsid()==-1)
       _exit(127);
    char *argv[4];
   
    argv[0]=getenv("SHELL");
    if (!argv[0])
       argv[0]="/bin/sh";
    argv[1]="-c";
    argv[2]=(char *)command;
    argv[3]=0;
    execvp(argv[0],argv);
    // We get here only if exec failed
    _exit(127);
   }
 if (cpid==-1)
   {// Fork failed do it manually
    *pidChild=0;
    return system(command);
   }
 *pidChild=cpid;
 return 0;
}

typedef struct
{
 unsigned char r,g,b;
} PalCol;

static
PalCol BIOSPalette[16]={
{ 0x00, 0x00, 0x00 },
{ 0x00, 0x00, 0xA8 },
{ 0x00, 0xA8, 0x00 },
{ 0x00, 0xA8, 0xA8 },
{ 0xA8, 0x00, 0x00 },
{ 0xA8, 0x00, 0xA8 },
{ 0xA8, 0x54, 0x00 },
{ 0xA8, 0xA8, 0xA8 },
{ 0x54, 0x54, 0x54 },
{ 0x54, 0x54, 0xFC },
{ 0x54, 0xFC, 0x54 },
{ 0x54, 0xFC, 0xFC },
{ 0xFC, 0x54, 0x54 },
{ 0xFC, 0x54, 0xFC },
{ 0xFC, 0xFC, 0x54 },
{ 0xFC, 0xFC, 0xFC }};

static uchar DefaultFont[]=
{
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // 0
 0x00,0x00,0x7E,0x81,0xA5,0x81,0x81,0xBD,0x99,0x81,0x81,0x7E,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x7E,0xFF,0xDB,0xFF,0xFF,0xC3,0xE7,0xFF,0xFF,0x7E,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x00,0x00,0x6C,0xFE,0xFE,0xFE,0xFE,0x7C,0x38,0x10,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x00,0x00,0x10,0x38,0x7C,0xFE,0x7C,0x38,0x10,0x00,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x00,0x18,0x3C,0x3C,0xE7,0xE7,0xE7,0x18,0x18,0x3C,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x00,0x18,0x3C,0x7E,0xFF,0xFF,0x7E,0x18,0x18,0x3C,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x3C,0x3C,0x18,0x00,0x00,0x00,0x00,0x00,0x00, // 
 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xE7,0xC3,0xC3,0xE7,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // 
 0x00,0x00,0x00,0x00,0x00,0x3C,0x66,0x42,0x42,0x66,0x3C,0x00,0x00,0x00,0x00,0x00, // \t
 0xFF,0xFF,0xFF,0xFF,0xFF,0xC3,0x99,0xBD,0xBD,0x99,0xC3,0xFF,0xFF,0xFF,0xFF,0xFF, // \n
 0x00,0x00,0x1E,0x0E,0x1A,0x32,0x78,0xCC,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x3C,0x66,0x66,0x66,0x66,0x3C,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x3F,0x33,0x3F,0x30,0x30,0x30,0x30,0x70,0xF0,0xE0,0x00,0x00,0x00,0x00, // \r
 0x00,0x00,0x7F,0x63,0x7F,0x63,0x63,0x63,0x63,0x67,0xE7,0xE6,0xC0,0x00,0x00,0x00, // 
 0x00,0x00,0x00,0x18,0x18,0xDB,0x3C,0xE7,0x3C,0xDB,0x18,0x18,0x00,0x00,0x00,0x00, // 
 0x00,0x80,0xC0,0xE0,0xF0,0xF8,0xFE,0xF8,0xF0,0xE0,0xC0,0x80,0x00,0x00,0x00,0x00, // 
 0x00,0x02,0x06,0x0E,0x1E,0x3E,0xFE,0x3E,0x1E,0x0E,0x06,0x02,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x18,0x3C,0x7E,0x18,0x18,0x18,0x7E,0x3C,0x18,0x00,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x66,0x66,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x7F,0xDB,0xDB,0xDB,0x7B,0x1B,0x1B,0x1B,0x1B,0x1B,0x00,0x00,0x00,0x00, // 
 0x00,0x7C,0xC6,0x60,0x38,0x6C,0xC6,0xC6,0x6C,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00, // 
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0xFE,0xFE,0xFE,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x18,0x3C,0x7E,0x18,0x18,0x18,0x7E,0x3C,0x18,0x7E,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x3C,0x18,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x00,0x00,0x00,0x18,0x0C,0xFE,0x0C,0x18,0x00,0x00,0x00,0x00,0x00,0x00, // \x1A
 0x00,0x00,0x00,0x00,0x00,0x30,0x60,0xFE,0x60,0x30,0x00,0x00,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0xC0,0xC0,0xFE,0x00,0x00,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x00,0x00,0x00,0x28,0x6C,0xFE,0x6C,0x28,0x00,0x00,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x00,0x00,0x10,0x38,0x38,0x7C,0x7C,0xFE,0xFE,0x00,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x00,0x00,0xFE,0xFE,0x7C,0x7C,0x38,0x38,0x10,0x00,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, //  
 0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00, // !
 0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // "
 0x00,0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0x6C,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00, // #
 0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00, // $
 0x00,0x00,0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x60,0xC6,0x86,0x00,0x00,0x00,0x00, // %
 0x00,0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00, // &
 0x00,0x30,0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // '
 0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00, // (
 0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00, // )
 0x00,0x00,0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00, // *
 0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00, // +
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00, // ,
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // -
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00, // .
 0x00,0x00,0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00, // /
 0x00,0x00,0x38,0x6C,0xC6,0xC6,0xD6,0xD6,0xC6,0xC6,0x6C,0x38,0x00,0x00,0x00,0x00, // 0
 0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00, // 1
 0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00, // 2
 0x00,0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00, // 3
 0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00, // 4
 0x00,0x00,0xFE,0xC0,0xC0,0xC0,0xFC,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00, // 5
 0x00,0x00,0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00, // 6
 0x00,0x00,0xFE,0xC6,0x06,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00, // 7
 0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00, // 8
 0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00, // 9
 0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00, // :
 0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00, // ;
 0x00,0x00,0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00, // <
 0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // =
 0x00,0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00, // >
 0x00,0x00,0x7C,0xC6,0xC6,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00, // ?
 0x00,0x00,0x00,0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0x7C,0x00,0x00,0x00,0x00, // @
 0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00, // A
 0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00, // B
 0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00, // C
 0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00, // D
 0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00, // E
 0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00, // F
 0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00, // G
 0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00, // H
 0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00, // I
 0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00, // J
 0x00,0x00,0xE6,0x66,0x66,0x6C,0x78,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00, // K
 0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00, // L
 0x00,0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00, // M
 0x00,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00, // N
 0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00, // O
 0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00, // P
 0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0C,0x0E,0x00,0x00, // Q
 0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00, // R
 0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00, // S
 0x00,0x00,0x7E,0x7E,0x5A,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00, // T
 0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00, // U
 0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00, // V
 0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0xEE,0x6C,0x00,0x00,0x00,0x00, // W
 0x00,0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x38,0x7C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00, // X
 0x00,0x00,0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00, // Y
 0x00,0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00, // Z
 0x00,0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00, // [
 0x00,0x00,0x00,0x80,0xC0,0xE0,0x70,0x38,0x1C,0x0E,0x06,0x02,0x00,0x00,0x00,0x00, // \.
 0x00,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00, // ]
 0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // ^
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00, // _
 0x00,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // `
 0x00,0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00, // a
 0x00,0x00,0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00, // b
 0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00, // c
 0x00,0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00, // d
 0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00, // e
 0x00,0x00,0x1C,0x36,0x32,0x30,0x78,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00, // f
 0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00, // g
 0x00,0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00, // h
 0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00, // i
 0x00,0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x00, // j
 0x00,0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00, // k
 0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00, // l
 0x00,0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0x00,0x00,0x00,0x00, // m
 0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00, // n
 0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00, // o
 0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00, // p
 0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00, // q
 0x00,0x00,0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00, // r
 0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00, // s
 0x00,0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0x00,0x00,0x00,0x00, // t
 0x00,0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00, // u
 0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00,0x00,0x00,0x00, // v
 0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x00,0x00,0x00,0x00, // w
 0x00,0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00, // x
 0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0xF8,0x00, // y
 0x00,0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00, // z
 0x00,0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00, // {
 0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00, // |
 0x00,0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00, // }
 0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // ~
 0x00,0x00,0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00, // 
 0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x18,0x70,0x00,0x00, // �
 0x00,0x00,0xCC,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00, // �
 0x00,0x0C,0x18,0x30,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00, // �
 0x00,0x10,0x38,0x6C,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0xCC,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00, // �
 0x00,0x60,0x30,0x18,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00, // �
 0x00,0x38,0x6C,0x38,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x18,0x70,0x00,0x00, // �
 0x00,0x10,0x38,0x6C,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0xC6,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00, // �
 0x00,0x60,0x30,0x18,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x66,0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00, // �
 0x00,0x18,0x3C,0x66,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00, // �
 0x00,0x60,0x30,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00, // �
 0x00,0xC6,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00, // �
 0x38,0x6C,0x38,0x10,0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00, // �
 0x0C,0x18,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x62,0x66,0xFE,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0xEC,0x36,0x36,0x7E,0xD8,0xD8,0x6E,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x3E,0x6C,0xCC,0xCC,0xFE,0xCC,0xCC,0xCC,0xCC,0xCE,0x00,0x00,0x00,0x00, // �
 0x00,0x10,0x38,0x6C,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0xC6,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00, // �
 0x00,0x60,0x30,0x18,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00, // �
 0x00,0x30,0x78,0xCC,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00, // �
 0x00,0x60,0x30,0x18,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0xC6,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00, // �
 0x00,0xC6,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00, // �
 0x00,0xC6,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00, // �
 0x00,0x18,0x18,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x18,0x18,0x00,0x00,0x00,0x00, // �
 0x00,0x38,0x6C,0x64,0x60,0xF0,0x60,0x60,0x60,0x60,0xE6,0xFC,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x66,0x66,0x3C,0x18,0x7E,0x18,0x7E,0x18,0x18,0x18,0x00,0x00,0x00,0x00, // �
 0x00,0xF8,0xCC,0xCC,0xF8,0xC4,0xCC,0xDE,0xCC,0xCC,0xCC,0xC6,0x00,0x00,0x00,0x00, // �
 0x00,0x0E,0x1B,0x18,0x18,0x18,0x7E,0x18,0x18,0x18,0xD8,0x70,0x00,0x00,0x00,0x00, // �
 0x00,0x18,0x30,0x60,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00, // �
 0x00,0x0C,0x18,0x30,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00, // �
 0x00,0x18,0x30,0x60,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00, // �
 0x00,0x18,0x30,0x60,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x76,0xDC,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00, // �
 0x76,0xDC,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x3C,0x6C,0x6C,0x3E,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x38,0x6C,0x6C,0x38,0x00,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x30,0x30,0x00,0x30,0x30,0x60,0xC0,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0xC0,0xC0,0xC0,0xC0,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x06,0x06,0x06,0x06,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x60,0xE0,0x62,0x66,0x6C,0x18,0x30,0x60,0xDC,0x86,0x0C,0x18,0x3E,0x00,0x00, // �
 0x00,0x60,0xE0,0x62,0x66,0x6C,0x18,0x30,0x66,0xCE,0x9A,0x3F,0x06,0x06,0x00,0x00, // �
 0x00,0x00,0x18,0x18,0x00,0x18,0x18,0x18,0x3C,0x3C,0x3C,0x18,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x36,0x6C,0xD8,0x6C,0x36,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0xD8,0x6C,0x36,0x6C,0xD8,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x11,0x44,0x11,0x44,0x11,0x44,0x11,0x44,0x11,0x44,0x11,0x44,0x11,0x44,0x11,0x44, // �
 0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA, // �
 0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77, // �
 0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0x18,0x18,0x18,0x18,0x18,0x18,0x18,0xF8,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0x18,0x18,0x18,0x18,0x18,0xF8,0x18,0xF8,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0x36,0x36,0x36,0x36,0x36,0x36,0x36,0xF6,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36, // �
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36, // �
 0x00,0x00,0x00,0x00,0x00,0xF8,0x18,0xF8,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0x36,0x36,0x36,0x36,0x36,0xF6,0x06,0xF6,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36, // �
 0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36, // �
 0x00,0x00,0x00,0x00,0x00,0xFE,0x06,0xF6,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36, // �
 0x36,0x36,0x36,0x36,0x36,0xF6,0x06,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x36,0x36,0x36,0x36,0x36,0x36,0x36,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x18,0x18,0x18,0x18,0x18,0xF8,0x18,0xF8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF8,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x18,0x18,0x18,0x18,0x18,0x18,0x18,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x1F,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x18,0x18,0x18,0x18,0x18,0x18,0x18,0xFF,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0x18,0x18,0x18,0x18,0x18,0x1F,0x18,0x1F,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x37,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36, // �
 0x36,0x36,0x36,0x36,0x36,0x37,0x30,0x3F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x3F,0x30,0x37,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36, // �
 0x36,0x36,0x36,0x36,0x36,0xF7,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0xF7,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36, // �
 0x36,0x36,0x36,0x36,0x36,0x37,0x30,0x37,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36, // �
 0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x36,0x36,0x36,0x36,0x36,0xF7,0x00,0xF7,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36, // �
 0x18,0x18,0x18,0x18,0x18,0xFF,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x36,0x36,0x36,0x36,0x36,0x36,0x36,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0xFF,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36, // �
 0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x3F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x18,0x18,0x18,0x18,0x18,0x1F,0x18,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x1F,0x18,0x1F,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3F,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36, // �
 0x36,0x36,0x36,0x36,0x36,0x36,0x36,0xFF,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36, // �
 0x18,0x18,0x18,0x18,0x18,0xFF,0x18,0xFF,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0x18,0x18,0x18,0x18,0x18,0x18,0x18,0xF8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // �
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // �
 0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // �
 0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F, // �
 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x76,0xDC,0xD8,0xD8,0xD8,0xDC,0x76,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x78,0xCC,0xCC,0xCC,0xD8,0xCC,0xC6,0xC6,0xC6,0xCC,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0xFE,0xC6,0xC6,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0xFE,0x6C,0x6C,0x6C,0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0xFE,0xC6,0x60,0x30,0x18,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x7E,0xD8,0xD8,0xD8,0xD8,0xD8,0x70,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xC0,0x00, // �
 0x00,0x00,0x00,0x00,0x76,0xDC,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x7E,0x18,0x3C,0x66,0x66,0x66,0x66,0x3C,0x18,0x7E,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x6C,0x38,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x38,0x6C,0xC6,0xC6,0xC6,0x6C,0x6C,0x6C,0x6C,0xEE,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x1E,0x30,0x18,0x0C,0x3E,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x7E,0xDB,0xDB,0xDB,0x7E,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x03,0x06,0x7E,0xDB,0xDB,0xF3,0x7E,0x60,0xC0,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x1C,0x30,0x60,0x60,0x7C,0x60,0x60,0x60,0x30,0x1C,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0xFE,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x7E,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00,0x7E,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00,0x7E,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x0E,0x1B,0x1B,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18, // �
 0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0xD8,0xD8,0xD8,0x70,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x18,0x00,0x7E,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x76,0xDC,0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x38,0x6C,0x6C,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x0F,0x0C,0x0C,0x0C,0x0C,0x0C,0xEC,0x6C,0x6C,0x3C,0x1C,0x00,0x00,0x00,0x00, // �
 0x00,0x6C,0x36,0x36,0x36,0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x3C,0x66,0x0C,0x18,0x32,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x00,0x00,0x00,0x00,0x00, // �
 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  // 255
};

static
void microAlarm(unsigned int usec)
{
 struct itimerval newV;
 newV.it_interval.tv_usec=0;
 newV.it_interval.tv_sec=0;
 newV.it_value.tv_usec=(long int)usec;
 newV.it_value.tv_sec=0;
 setitimer(ITIMER_REAL,&newV,0);
}

TScreenX11::TScreenX11()
{
 int col;
 char *data;
 int i;

 maxX=80; maxY=25;
 fontW=8; fontH=16;

 /* Try to connect to the X server */
 disp=XOpenDisplay("");
 /* If we fail just return */
 if (!disp)
    return;

 /* Initialize driver */
 initialized=1;

 TDisplayX11::Init();

 TScreen::clearScreen=clearScreen;
 TScreen::getCharacters=getCharacters;
 TScreen::getCharacter=getCharacter;
 TScreen::setCharacter=setCharacter;
 TScreen::setCharacters=setCharacters;
 TScreen::System=System;

 TGKeyX11::Init();
 THWMouseX11::Init();

 /* Initialize common variables */
 startupCursor=getCursorType();
 screenMode=startupMode=getCrtMode();
 screenWidth =GetCols();
 screenHeight=GetRows();
 screenBuffer=new ushort[screenWidth*screenHeight];

 /* Get screen and graphic context */
 screen=DefaultScreen(disp);
 gc=DefaultGC(disp,screen);
 visual=DefaultVisual(disp,screen);

 /* Create what we'll use as font */
 for (i=0; i<256; i++)
    {/* Load the shape */
     data=(char *)malloc(fontH);
     memcpy(data,DefaultFont+i*fontH,fontH);
     /* Create a BitMap Image with this data */
     ximgFont[i]=XCreateImage(disp,visual,1,XYBitmap,0,data,fontW,fontH,8,0);
     /* Set the bit order */
     ximgFont[i]->byte_order=ximgFont[i]->bitmap_bit_order=MSBFirst;
    }

 /* Create the cursor image */
 cursorData=(char *)malloc(fontH);
 cursorImage=XCreateImage(disp,visual,1,XYBitmap,0,cursorData,fontW,fontH,8,0);
 cursorImage->byte_order=cursorImage->bitmap_bit_order=MSBFirst;
 cShapeFrom=14;
 cShapeTo=16;

 /* Set the locales */
 if (setlocale(LC_ALL,"")==NULL)
    fprintf(stderr,"Error: setlocale()!\n");

 /* Create a simple window */
 rootWin=RootWindow(disp,screen);
 mainWin=XCreateSimpleWindow(disp,rootWin,
         0,0,                             /* win position */
         maxX*fontW,maxY*fontH,           /* win size */
         0,                               /* frame width */
         BlackPixel(disp,screen),   /* Border color */
         BlackPixel(disp,screen));  /* Background */

 /* This is useful if we use subwindows.
 hints.flags=InputHint;
 hints.input=True;
 XSetWMHints(disp,mainWin,&hints);*/
 XTextProperty name;
 char *s="Test";
 XStringListToTextProperty(&s,1,&name);

 XClassHint aClass;
 aClass.res_name="tvapp";   /* Take resources for tvapp */
 aClass.res_class="XTVApp"; /* X Turbo Vision Application */

 XSizeHints *shints=XAllocSizeHints();
 shints->flags=PResizeInc /* These are useless: |PBaseSize|PMinSize */;
 /* Fonts increments */
 shints->width_inc =fontW;
 shints->height_inc=fontH;

 XSetWMProperties(disp,mainWin,
                  &name,   /* Visible title */
                  &name,   /* Icon title */
                  NULL,0,  /* Command line */
                  shints,  /* Normal size hints, resize increments */
                  NULL,    /* Window manager hints, nothing (i.e. icon) */
                  &aClass);/* Resource name and class of window */
 XFree((char *)name.value);

 /* Ask to be notified when they kill the window */
 theProtocols=XInternAtom(disp,"WM_DELETE_WINDOW",True);
 XSetWMProtocols(disp,mainWin,&theProtocols,1);

 /* Initialize the Input Context for international support */
 if ((xim=XOpenIM(disp,NULL,NULL,NULL))==NULL)
   {
    printf("Error: XOpenIM()!\n");
    exit(0);
   }
 xic=XCreateIC(xim,XNInputStyle,XIMPreeditNothing | XIMStatusNothing,
               XNClientWindow,mainWin,NULL);
 if (xic==NULL)
   {
    printf("Error: XCreateIC()!\n");
    XCloseIM(xim);
    exit(0);
   }

 /* We will accept the Input Context default events ... */
 unsigned long mask, fevent;
 XGetICValues(xic,XNFilterEvents,&fevent,NULL);
 /* plus these */
 mask=ExposureMask | KeyPressMask | KeyReleaseMask | FocusChangeMask |
      StructureNotifyMask | ButtonPressMask | ButtonReleaseMask |
      ButtonMotionMask/*PointerMotionMask*/;
 XSelectInput(disp,mainWin,mask|fevent);

 /* OK, now put the window on the display */
 XMapWindow(disp,mainWin);

 /* Map the VGA Text BIOS colors */
 cMap=DefaultColormap(disp,screen);
 XColor query;
 for (col=0; col<16; col++)
    {
     query.red  =BIOSPalette[col].r*256;
     query.green=BIOSPalette[col].g*256;
     query.blue =BIOSPalette[col].b*256;
     query.flags= ~0;
     XAllocColor(disp,cMap,&query);
     colorMap[col]=query.pixel;
    }

 /* A graphics context for the text cursor */
 cursorGC=XCreateGC(disp,mainWin,0,0);

 /* Create the cursor timer */
 signal(SIGALRM,sigAlm);
 microAlarm(cursorDelay);

 XSetBackground(disp,gc,colorMap[0]);
 XSetForeground(disp,gc,colorMap[7]);
 clearScreen();
}

/*****************************************************************************
 Routines to create a blinking cursor
*****************************************************************************/

void TScreenX11::sigAlm(int sig)
{
 cursorChange=1;
 microAlarm(cursorDelay);
}

void TScreenX11::UnDrawCursor()
{
 if (!cursorInScreen)
    return;
 unsigned offset=cursorX+cursorY*maxX;
 uchar *theChar=(uchar *)(screenBuffer+offset);
 int bg=theChar[attrPos]>>4;
 int fg=theChar[attrPos] & 0xF;

 XSetBackground(disp,cursorGC,colorMap[bg]);
 XSetForeground(disp,cursorGC,colorMap[fg]);
 XPutImage(disp,mainWin,cursorGC,ximgFont[theChar[charPos]],0,0,cursorPX,cursorPY,fontW,fontH);
 cursorInScreen=0;
 return;
}

void TScreenX11::DrawCursor()
{
 //fprintf(stderr,"DrawCursor: cursorEnabled=%d\n",cursorEnabled);
 if (cursorEnabled)
   {
    cursorInScreen=!cursorInScreen;

    /* Create an image with the character under cursor */
    unsigned offset=cursorX+cursorY*maxX;
    uchar *theChar=(uchar *)(screenBuffer+offset);
    int bg=theChar[attrPos]>>4;
    int fg=theChar[attrPos] & 0xF;
    XSetBackground(disp,cursorGC,colorMap[bg]);
    XSetForeground(disp,cursorGC,colorMap[fg]);
    memcpy(cursorData,ximgFont[theChar[charPos]]->data,fontH);

    //fprintf(stderr,"DrawCursor: cursorInScreen=%d from/to %d/%d\n",cursorInScreen,cShapeFrom,cShapeTo);
    /* If the cursor is on draw it over the character */
    if (cursorInScreen)
       memset(cursorData+cShapeFrom,0xFF,cShapeTo-cShapeFrom);

    /* Now put it in the screen */
    XPutImage(disp,mainWin,cursorGC,cursorImage,0,0,cursorPX,cursorPY,fontW,fontH);
    XFlush(disp);
   }
}

void TScreenX11::DisableCursor()
{
 cursorEnabled=0;
 UnDrawCursor();
}

void TScreenX11::EnableCursor()
{
 cursorEnabled=1;
 //DrawCursor();
}

/*****************************************************************************
 Events processing
*****************************************************************************/

void TScreenX11::ProcessGenericEvents()
{
 XEvent event;
 unsigned lastW, lastH;
 unsigned newPW, newPH;

 while (1)
   {
    if (cursorChange)
      {
       cursorChange=0;
       DrawCursor();
      }
    /* Check if we have generic events in the queuue */
    if (XCheckMaskEvent(disp,~(aMouseEvent|aKeyEvent),&event)!=True)
      {
       /* Process message that doesn't have mask */
       if (XCheckTypedEvent(disp,ClientMessage,&event)==True)
         {
          if ((Atom)event.xclient.data.l[0]==theProtocols)
            {
             printf("Bye, bye!\n");
             XDestroyIC(xic);
             XCloseIM(xim);
             XDestroyWindow(disp,mainWin);
             XCloseDisplay(disp);
             exit(0);
            }
         }
       return;
      }
    /* Not sure if needed, but documentation says it helps if the event
       should be redirected to another window */
    if (XFilterEvent(&event,0)==True)
       continue;
  
    switch (event.type)
      {
       case Expose:
            {
             /*printf("Expose: %d %d %d %d\n",event.xexpose.x,event.xexpose.y,
                    event.xexpose.width,event.xexpose.height);*/
             int x=event.xexpose.x/fontW;
             int y=event.xexpose.y/fontH;
             unsigned src=y*maxX+x;

             newPW=event.xexpose.x+event.xexpose.width;
             int x2=newPW/fontW;
             if (newPW%fontW) x2++;
             if (x2>=maxX) x2=maxX;

             newPW=event.xexpose.y+event.xexpose.height;
             int y2=newPW/fontH;
             if (newPW%fontH) y2++;
             if (y2>=maxY) y2=maxY;

             int w=x2-x;
             int h=y2-y;

             /*printf("x1,y1 %d,%d x2,y2 %d,%d w,h %d,%d\n",x,y,x2,y2,w,h);*/

             while (h)
               {
                redrawBuf(x,y,w,src);
                src+=maxX;
                y++; h--;
               }
             XFlush(disp);
            }
            break;

       case FocusIn:
            //printf("Focus in\n");
            if (xic)
               XSetICFocus(xic);
            break;

       case FocusOut:
            //printf("Focus out\n");
            if (xic)
               XUnsetICFocus(xic);
            break;

       case ConfigureNotify:
            if (event.xresizerequest.window!=mainWin)
               break;
    
            lastW=maxX;
            lastH=maxY;
            maxX=event.xconfigure.width /fontW;
            maxY=event.xconfigure.height/fontH;

            /* Minimal size */
            if (maxX<40) maxX=40;
            if (maxY<20) maxY=20;

            /* If size changed realloc buffer and indicate it */
            if ((maxX!=(int)lastW) || (maxY!=(int)lastH))
              {
               screenBuffer=(uint16 *)realloc(screenBuffer,maxX*maxY*2);
               windowSizeChanged=1;
              }

            /* Force the window to have a size in chars */
            newPW=fontW*maxX;
            newPH=fontH*maxY;

            if ((unsigned)event.xconfigure.width==newPW &&
                (unsigned)event.xconfigure.height==newPH)
               break;

            /*printf("Nuevo: %d,%d (%d,%d)\n",maxX,maxY,lastW,lastH);*/
            XResizeWindow(disp,mainWin,newPW,newPH);
            /*printf("Nuevo 2: %d,%d\n",maxX,maxY);*/
            break;
      }
   }
}

void TScreenX11::writeLine(int x, int y, int w, unsigned char *str, unsigned color)
{
 if (w<=0)
    return; // Nothing to do

 XSetBackground(disp,gc,colorMap[color>>4]);
 XSetForeground(disp,gc,colorMap[color&15]);

 x*=fontW; y*=fontH;
 UnDrawCursor();
 while (w--)
   {
    XPutImage(disp,mainWin,gc,ximgFont[*str],0,0,x,y,fontW,fontH);
    str++;
    x+=fontW;
   }
}

void TScreenX11::redrawBuf(int x, int y, unsigned w, unsigned off)
{
 int len   = 0;         /* longitud a escribir */
 int letra = 0;
 int color = 0;
 int last  = -1;
 unsigned char *tmp = (unsigned char *)alloca(w*sizeof(char));
 unsigned char *dst = tmp;
 uchar *b=(uchar *)(screenBuffer+off);

 if (y>=maxY)
   {
    printf("Y=%d\n",y);
    return;
   }
 while (w--)
   {
    letra=b[charPos];
    color=b[attrPos];
    
    if (color!=last)
      {
       if (last>=0)
         {
          writeLine(x,y,len,tmp,last);  // Print last same color block
          dst=tmp; x+=len; len=0;
         }
       last=color;
      }
    *dst++=letra; b+=2; len++;
   }
  
 writeLine(x,y,len,tmp,color);          // Print last block
}

TScreen *TV_XDriverCheck()
{
 TScreenX11 *drv=new TScreenX11();
 if (!TScreen::initialized)
   {
    delete drv;
    return 0;
   }
 return drv;
}
#endif // defined(TVOS_UNIX) && defined(HAVE_X11)


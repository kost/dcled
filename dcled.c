/* dcled - userland driver for Dream Cheeky (Dream Link?) USB LED Message Board
 * Copyright 2009-2014 Jeff Jahr <malakai@jeffrika.com> */

/* This is free software.  G'head, use it all you want.  Version 1.0 took ~12
 * hours to write, and was my first foray into usb device programming.  This is
 * probably not a real good example of how to do ANYTHING.  Sun Jan  4 00:18:41
 * PST 2009 -jsj */

/* dcled contains contributions from -
 * Andy Scheller
 * Michael Wensley
 * Glen Smith
 * Robert Flick
 * Stefan Misch
 * Florian Krauthan
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <sys/select.h>
#include <glob.h>
#include <hidapi.h>
#include <math.h>

#define VENDOR 0x1d34
#define PRODUCT 0x0013
/* heh heh.  ledsx.  thats almost dirty. */
#define LEDSX 21
#define LEDSY 7
#define FONTX 5
#define FONTY 7
#define MAXTESTPAT 8

/* if it isn't defined in the make file... */
#ifndef FONTDIR
#define FONTDIR "."
#endif

#ifndef DCLEDVERSION
#define DCLEDVERSION "0.0"
#endif

/* This is the usage that gets sent to the device, from the docs supplied by
 * Alvin Wong. */
struct ledpkt {
    unsigned char brightness;
    unsigned char row;
    unsigned char data1[3];
    unsigned char data2[3];
};

/* struct for a version 0 font definition */
struct ledfont {
    char *name;
    char *description;
    char *author;
    int dataformat;
    int dispwidth;
    int dispheight;
    char data[256][FONTY];
};

/* list of available fonts. */
struct ledfontlist {
    struct ledfont *font;
    struct ledfontlist *next;
};

/* This is a basic definition of the led display. 0,0 is the upper left. */
struct ledscreen {
    hid_device *handle;
    unsigned char path_in_len;
    int *path_in;
    int brightness;
    int scrolldelay;
    int scrolldir;
    int preamble;
    int tach;
    time_t lastupdate;
    int led[LEDSX][LEDSY];
    struct ledfont *font;
};

struct preamble {
    const char *name;
    const char *description;
    const char *author;
};

struct preamble preambles[] = {
        { "none"    ,"The default","Jeff Jahr" },
        { "dots"    ,"A string of random dots","Jeff Jahr" },
        { "static"  ,"Warms up like an old TV","Jeff Jahr" },
        { "squiggle","A squiggly line","Jeff Jahr" },
        { "clock24" ,"Shows the 24 hour time", "Andy Scheller"},
        { "clock"   ,"Shows the time", "Andy Scheller" },
        { "spiral"  ,"Draws a spiral", "Glen Smith" },
        { "fire"    ,"A nice warm hearth", "Glen Smith" },
        { "bcdclock"   ,"Shows the time in binary", "Jeff Jahr" },
        { NULL,NULL,NULL }
};

struct preamble tachs[] = {
        { "none"    ,"The default","Jeff Jahr" },
        { "tach"    ,"A simple tachometer","Jeff Jahr" },
        { "static"  ,"Random dots","Jeff Jahr" },
        { "scroll"  ,"Scrolling bar graph","Jeff Jahr" },
        { "fireline","Fireline Tach","Jeff Jahr" },
        { NULL,NULL,NULL }
};


void clearscreen(int mode,struct ledscreen *disp);
void scroll(int dir, struct ledscreen *disp);
void scrollmsg(struct ledscreen *disp, char* buf);
void scrollrndfade(struct ledscreen *disp, int isend, int width);
void scrollpreamble(int isend, struct ledscreen *disp);
void staticwarmup(struct ledscreen *disp, int isend, int width);
void scrollsquiggle(struct ledscreen *disp, int isend, int width);
void printtime(struct ledscreen *disp,int mode);
void spiral(struct ledscreen *disp);
void fire(struct ledscreen *disp, int isend);
struct ledfont *allocfont(void);
struct ledfont *initfont1(struct ledfont *target);
struct ledfont *initfont2(struct ledfont *target);
struct ledfont *loadfont(char *filename);
void savefonts(struct ledfontlist *fontlist);
void drawtach(struct ledscreen *disp, int load);

int debug = 0;
int echo = 0;
int fastprint = 0;
int nodev = 0;
int repeat=0;
char version[] = DCLEDVERSION;


/* This sends a copy of the ledscreen to stdout.  Useful as a debug.  */
void print_screen (struct ledscreen *sc) {

    int x,y;
    for (y=0;y<LEDSY;y++) {
        for(x=0;x<LEDSX;x++) {
            if(sc->led[x][y] == 1) {
                printf("O");
            } else {
                printf(".");
            }
        }
        printf("\n");
    };
    printf("\n");

    return;
}


/* This sends the led array in the ledscreen structure to the device.  Call it
 * when you want to update the display.  Seems like the device wants to clear
 * itself after about a second, so you might want to keep calling this to
 * refesh the device.  */
void send_screen (struct ledscreen *disp) {

    char bigpkt[sizeof(struct ledpkt) * 4];
    struct ledpkt pkt;
    int row, col, bytep, bitp;
    static const int HID_SET_REPORT = 0x09;

    if(debug) print_screen(disp);
    time(&(disp->lastupdate));

    for(row=0;row<LEDSY;row+=2) {
        pkt.brightness = disp->brightness;
        pkt.row = row;

        for(bytep=0;bytep<=2;bytep++) {
            pkt.data1[bytep] = 0xff;
            pkt.data2[bytep] = 0xff;
        }

        bytep=2;
        bitp=0;

        for(col=0;col<LEDSX;col++) {
            if (disp->led[col][row] == 1) {
                pkt.data1[bytep] &= ~(1<<bitp);
            }
            if (disp->led[col][row+1] == 1) {
                pkt.data2[bytep] &= ~(1<<bitp);
            }
            bitp++;
            if(bitp==8) {
                bitp=0;
                bytep--;
            }
        }
        memcpy(bigpkt+((row/2)*sizeof(struct ledpkt)), &pkt, sizeof(struct ledpkt));
    }
    if (!nodev) {
        const unsigned int chunk_count = 4;
        const unsigned int chunk_size = sizeof(bigpkt) / chunk_count;
        int chunk;
        for (chunk=0;chunk<chunk_count;chunk++) {
            hid_write(disp->handle, bigpkt+(chunk*chunk_size), chunk_size);
        }
    }

}

/* clears the screen either by flasing it to zeros, or by scrolling it away.*/
void clearscreen(int mode,struct ledscreen *disp) {
    int x,y;
    switch (mode) {
        case 1: /* west scroll */
            for(x=0;x<=LEDSX;x++) {
                send_screen(disp);
                usleep(disp->scrolldelay);
                scroll(3,disp);
            }
            break;
        default:
        case 0: /* clear */
            for (x=0;x<LEDSX;x++) {
                for(y=0;y<LEDSY;y++) {
                    disp->led[x][y] = 0;
                }
            }
            break;
            break;
    }
    return;
}

int open_hiddev(struct ledscreen *disp) {
    if (hid_init()) {
        return(EXIT_FAILURE);
    }

    disp->handle = hid_open(VENDOR, PRODUCT, NULL);
    if (disp->handle == NULL) {
        return(EXIT_FAILURE);
    }

    return(EXIT_SUCCESS);
}

/* can call this even if no device was ever set up. */
void close_hiddev(struct ledscreen *disp) {
    if(!disp){
        return;
    }
    if(!(disp->handle)) {
        return;
    }

    if (disp->handle != NULL) {
        hid_close(disp->handle);
        disp->handle = NULL;
    }

    hid_exit();
}



/* shift the disp one pixel in the given direction.*/
/* not all directions are implemented yet.*/
void scroll(int dir, struct ledscreen *disp) {

    int x,y;

    switch (dir) {
        case 0: /* north - up */
            for(x=0;x<LEDSX;x++) {
                for(y=0;y<LEDSY-1;y++) {
                    disp->led[x][y] = disp->led[x][y+1];
                }
            }
            for(x=0;x<LEDSX;x++) {
                disp->led[x][LEDSY-1] = 0;
            }
            break;
        case 1: /* east - right */
            for(x=LEDSX-1;x>0;x--) {
                for(y=0;y<LEDSY;y++) {
                    disp->led[x][y] = disp->led[x-1][y];
                }
            }
            for(y=0;y<LEDSY;y++) {
                disp->led[0][y] = 0;
            }
            break;
        case 2: /* south - down */
        case 3: /* west - left */
        default:
            for(x=0;x<LEDSX-1;x++) {
                for(y=0;y<LEDSY;y++) {
                    disp->led[x][y] = disp->led[x+1][y];
                }
            }
            for(y=0;y<LEDSY;y++) {
                disp->led[LEDSX-1][y] = 0;
            }
    }

    return;
}


/* overlay a character onto the display with the leftmost edge of the char
 * starting at xloc.  */
void printchar(struct ledscreen *disp, char c, int xloc) {

    int cx,x,y;
    char *f;

    f=(char*)disp->font->data;
    for(y=0;y<(disp->font->dispheight);y++) {
        for (cx=0,x=xloc;x<LEDSX && cx<=(disp->font->dispwidth);cx++,x++) {
            disp->led[x][y] =
                    ((*(f+(c*FONTY)+y) & (1<<cx))!=0)?  1:disp->led[x][y];
        }
    }
}

/* print a single decimal digit as a vertical line of binary bits. */
void printBCDchar(struct ledscreen *disp, char c, int xloc, int baseline) {

    int val;

    if (baseline >= LEDSY || baseline-5 < 0) {
        baseline = LEDSY-1;
    }

    if (!isdigit(c)) {
        if ( c == '|' ) {
            val = 15;
        } else {
            val = 0;
        }
    } else {
        val = c - '0';
    }

    disp->led[xloc][baseline-0]=(val&1)?1:0;
    disp->led[xloc][baseline-1]=(val&2)?1:0;
    disp->led[xloc][baseline-2]=(val&4)?1:0;
    disp->led[xloc][baseline-3]=(val&8)?1:0;

    return;
}

/* make one of those BCD clock displays that every geek seems to have.*/
void bcdclock(struct ledscreen *disp, int forever) {

    time_t rawtime;
    time_t firsttime;
    struct tm* timeinfo;
    char strtime[11];
    int x;
    char *p;

    time(&firsttime);
    time(&rawtime);

    while(forever || (rawtime - firsttime) < 3) {
        time(&rawtime);
        timeinfo = localtime(&rawtime);

        strftime(strtime, 11, " %I %M %S ", timeinfo);

        clearscreen(0, disp);

        /* draw the digits */
        for(p=strtime,x=1;*p;p++,x+=2) {
            printBCDchar(disp, *p, x, LEDSY-3);
        }
        send_screen(disp);
        usleep(disp->scrolldelay * 10);

    }
}

/* Contributed by Andy Scheller */
/* displays the time in HH:MM format, set mode to 1 for 24-hour display.*/
void printtime(struct ledscreen *disp, int mode) {
    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    char strtime[6];
    strftime(strtime, 6, (mode==1?"%H:%M":"%I:%M"), timeinfo);
    clearscreen(0, disp);
    printchar(disp,strtime[0],0);
    printchar(disp,strtime[1],4);
    printchar(disp,strtime[2],8);
    printchar(disp,strtime[3],12);
    printchar(disp,strtime[4],16);
}

/* This is Andy Scheller's printtime code, modified to run forever and wiggle
 * the colon.  Heh heh.  Thats almost dirty.*/
void clockmode(struct ledscreen *disp, int mode, int forever) {

    time_t rawtime;
    time_t firsttime;
    struct tm* timeinfo;
    int oddsec;
    char strtime[6];

    if (mode == 3) {
        bcdclock(disp,forever);
        return;
    }

    time(&firsttime);
    time(&rawtime);

    while(forever || (rawtime - firsttime) < 3) {
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        oddsec = rawtime%2;

        if(mode == 1) {
            strftime(strtime, 6, "%H:%M", timeinfo);
        } else {
            strftime(strtime, 6, "%I:%M", timeinfo);
        }

        clearscreen(0, disp);
        printchar(disp,strtime[0],0);
        printchar(disp,strtime[1],4);
        printchar(disp,strtime[2],8+oddsec);
        printchar(disp,strtime[3],12);
        printchar(disp,strtime[4],16);
        send_screen(disp);
        usleep(100000);
    }
}

/* Prints a test pattern to the screen.  pattern 0 and 1 are the all-on
 * and all-off patterns, which are pretty useful. */
void testpatern (int which, struct ledscreen *disp) {

    int x, y;
    /*char font[256*FONTY];*/


    switch(which) {
        case 0: /* all off */
            for (x=0;x<LEDSX;x++) {
                for(y=0;y<LEDSY;y++) {
                    disp->led[x][y] = 0;
                }
            }
            break;
        case 1: /* all on */
            for (x=0;x<LEDSX;x++) {
                for(y=0;y<LEDSY;y++) {
                    disp->led[x][y] = 1;
                }
            }
            break;
        case 2: /* box borders */
            for(x=0;x<LEDSX;x++) {
                disp->led[x][0] = 1;
                disp->led[x][LEDSY-1] = 1;
            }
            for(y=0;y<LEDSY;y++) {
                disp->led[0][y] = 1;
                disp->led[LEDSX-1][y] = 1;
            }
            break;
        case 3: /* box corners */
            disp->led[0][0] = 1;
            disp->led[LEDSX-1][0] = 1;
            disp->led[LEDSX-1][LEDSY-1] = 1;
            disp->led[0][LEDSY-1] = 1;
            break;
        case 4: /* simple diag */
            for(x=0,y=0;x<LEDSX && y<LEDSY;x++, y++) {
                disp->led[x][y] = 1;
            }
            break;
        case 5: /* random */
            for (x=0;x<LEDSX;x++) {
                for(y=0;y<LEDSY;y++) {
                    disp->led[x][y] = (rand()>=(RAND_MAX/2))?1:0;
                }
            }
            break;
        case 6: /* Jeff */
            printchar(disp,'J',0);
            printchar(disp,'e',4);
            printchar(disp,'f',8);
            printchar(disp,'f',12);
            printchar(disp,'!',16);
            break;
        case 7: /* 12h time */
            printtime(disp,0);
            break;
        case 8: /* 24h time */
            printtime(disp,1);
            break;
    }
    return;
}

/* scrolls random data.  kinda cool, used during development. */
void scrolltest(struct ledscreen *disp) {

    int y;
    clearscreen(0,disp);

    while(1) {
        for(y=0;y<LEDSY;y++) {
            disp->led[LEDSX-1][y] = (rand()<(RAND_MAX/3))?1:0;
        }

        send_screen(disp);
        usleep(disp->scrolldelay);

        scroll(3,disp);
    }
}

/* call the right graphic header depending on style and direction. */
void scrollpreamble(int isend, struct ledscreen *disp) {

    switch (disp->preamble) {
        case 7:
            fire(disp,isend);
            return;
        case 6:
            spiral(disp);
            return;
        case 8:
            if (!isend) {
                clockmode(disp,3,0);
            }
            return;
        case 5:
            if (!isend) {
                clockmode(disp,2,0);
            }
            return;
        case 4:
            if (!isend) {
                clockmode(disp,1,0);
            }
            return;
        case 3:
            scrollsquiggle(disp,isend,5*LEDSX);
            return;
        case 2:
            staticwarmup(disp,isend,5*LEDSX);
            return;
        case 1:
            scrollrndfade(disp,isend,3*LEDSX);
            return;
        case 0:
        default:
            if (isend && !fastprint) {
                clearscreen(1,disp);
            }
            return;
    }

}

/* make the display warm up with static, like an old tv. */
void staticwarmup(struct ledscreen *disp, int isend, int width) {

    int count, origbright;
    int x,y;


    if (!isend) {
        origbright = disp->brightness;

        for (count=0;count<width;count++) {
            for (x=0;x<LEDSX;x++) {
                for(y=0;y<LEDSY;y++) {
                    disp->led[x][y] =
                            (1.8*(float)rand()*width/(float)(RAND_MAX)<count)?1:0;
                    disp->brightness =
                            (int)(3.0*(((float)count/(float)width)+0.3*(float)rand()/(float)(RAND_MAX)));
                }
            }
            send_screen(disp);
        }
        disp->brightness = origbright;
        clearscreen(0,disp);
        send_screen(disp);
        usleep(disp->scrolldelay * 5);
    } else {
        clearscreen(1,disp);
    }

}

/* A random banner */
void scrollrndfade(struct ledscreen *disp, int isend, int width) {

    int y;
    int odds;

    if (!isend) {
        for(odds=0;odds<width;odds++){
            for(y=0;y<LEDSY;y++) {
                disp->led[LEDSX-1][y] =
                        (1.8*(float)rand()*width/(float)(RAND_MAX)<odds)?1:0;
            }

            send_screen(disp);
            usleep(disp->scrolldelay);
            scroll(3,disp);
        }
        scroll(3,disp);
        scroll(3,disp);
    } else {
        scroll(3,disp);
        scroll(3,disp);
        for(odds=width;odds>0;odds--){
            for(y=0;y<LEDSY;y++) {
                disp->led[LEDSX-1][y] =
                        (1.8*(float)rand()*width/(float)(RAND_MAX)<odds)?1:0;
            }

            send_screen(disp);
            usleep(disp->scrolldelay);
            scroll(3,disp);
        }
        clearscreen(1,disp);
    }
}

/* its a squiggly line. */
void scrollsquiggle(struct ledscreen *disp, int isend, int width) {

    int count;
    float yf,diff;
    int y;

    scroll(3,disp);
    yf = y = LEDSY/2;
    for(count=0;count<width;count++){
        diff = (2.0*((float)rand()/(float)(RAND_MAX))-1.0);
        yf+=diff;
        if(yf > LEDSY-1 || yf < 0.0) {
            yf-=1.5*diff;
        }
        y = yf;
        disp->led[LEDSX-1][y] = 1;
        scroll(3,disp);
        send_screen(disp);
        usleep(disp->scrolldelay);
    }
    scroll(3,disp);
    usleep(disp->scrolldelay);
}

/* its a spiral. Contributed by Glen Smith. */
void spiral(struct ledscreen *disp) {

    int offset,count=0,state,x,y;

    clearscreen(0, disp);

    offset=0;

    state=1;

    while(count<2) {
        while(offset<4) {
            for (x=0;x<(LEDSX-offset);x++) {
                disp->led[x][y+offset] = state;
                send_screen(disp);
                usleep(100);
            }

            for (y=0;y<(LEDSY-offset);y++) {
                disp->led[(LEDSX-1)-offset][y] = state;
                send_screen(disp);
                usleep(100);
            }

            for (x=(LEDSX-1-offset);x>=0;x--) {
                disp->led[x][(LEDSY-1)-offset] = state;
                send_screen(disp);
                usleep(100);
            }

            for (y=(LEDSY-1-offset);y>=(1+offset);y--) {
                disp->led[x+1+offset][y] = state;
                send_screen(disp);
                usleep(100);
            }
            y=0;
            x=0;
            offset++;
        }
        offset=0;
        count++;
        state=0;
    }

}

/* Its a fire.  Contributed by Glen Smith */
/* Made to fade in and out for a preamble by Jeff Jahr */
void fire(struct ledscreen *disp, int isend) {

    int count, x, y, origbright, height;
    float scale,burnrate;

    origbright = disp->brightness;

    if (isend) {
        scale = 1.0;
    } else {
        scale = 0.1;
    }

    while (scale <= 1.0 && scale >= 0) {
        count=0;
        while(count<4) {
            clearscreen(0, disp);
            for (x=0;x<LEDSX;x++) {
                height = scale * (rand()/(((double)RAND_MAX + 1) / LEDSY));
                for (y=LEDSY-1;y>=(LEDSY-height);y--) {
                    disp->led[x][y] = 1;
                }
            }
            disp->brightness = rand()/(((double)RAND_MAX + 1) / 2);
            send_screen(disp);
            usleep(disp->scrolldelay / 10);
            count++;
        }
        if (scale>0.9) {
            burnrate = 0.005 * ((isend)?-1:1);
        } else {
            burnrate = 0.06 * ((isend)?-1:1);
        }
        scale = scale + burnrate;
    }

    disp->brightness = origbright;
    clearscreen(0, disp);
}


/* Runs a bunch of test patterns.  */
void fancytest(struct ledscreen *disp, struct ledfontlist *fontlist) {

    int count, tp, b, preamble;
    char msg[8192];
    struct ledfontlist *flp;
    struct ledfont *origfont;
    int origspeed;
    int tach,load;

    /* in case you want to write out the compiled in fonts. */
    /* savefonts(fontlist); */

    /* basic test paterns */
    origspeed=disp->scrolldelay;
    disp->scrolldelay = 100000;

    for (count=0;count<1;count++){
        for (tp=1;tp<=MAXTESTPAT;tp++) {
            testpatern(0,disp);
            testpatern(tp,disp);
            for (b=0;b<=2;b++) {
                disp->brightness = b;
                send_screen(disp);
                usleep(disp->scrolldelay);
            }
        }
    }
    testpatern(0,disp);
    disp->scrolldelay = origspeed;

    /* now demo all of the preambles. */
    preamble = 1;

    origspeed=disp->scrolldelay;
    disp->scrolldelay = origspeed * 4;
    scrollmsg(disp,"    Preambles    \n");
    disp->scrolldelay = origspeed;

    while ( preambles[preamble].name != NULL) {
        snprintf(msg,8192,"%s - %-10s <%s>",
                 preambles[preamble].name,
                 preambles[preamble].description,
                 preambles[preamble].author
        );

        disp->preamble = preamble;
        scrollpreamble(0,disp);
        scrollmsg(disp,msg);
        scrollpreamble(1,disp);
        preamble++;
    }

    /* demo the tachs */
    tach = 1;

    origspeed=disp->scrolldelay;
    disp->scrolldelay = origspeed * 4;
    scrollmsg(disp,"    Tachometers   \n");
    disp->scrolldelay = origspeed;

    while ( tachs[tach].name != NULL) {

        for(load=0;load<=100;load+=10) {
            disp->tach=tach;
            drawtach(disp,load);
            usleep(250000);
        }

        for(load=100;load>0;load-=11) {
            disp->tach=tach;
            drawtach(disp,load);
            usleep(250000);
        }

        clearscreen(1, disp);

        snprintf(msg,8192,"%s - %-10s <%s>",
                 tachs[tach].name,
                 tachs[tach].description,
                 tachs[tach].author
        );
        scrollmsg(disp,msg);
        clearscreen(1, disp);

        tach++;
    }


    /* demo the fonts. */
    disp->preamble = 0;
    origfont = disp->font;

    origspeed=disp->scrolldelay;
    disp->scrolldelay = origspeed * 4;
    scrollmsg(disp,"    Fonts    \n");
    disp->scrolldelay = origspeed;

    for(flp=fontlist;flp;flp=flp->next) {
        snprintf(msg,8192,"'%s' - %s <%s>\n",
                 flp->font->name,
                 flp->font->description,
                 flp->font->author
        );
        disp->font = flp->font;
        scrollmsg(disp,msg);
    }

    clearscreen(1, disp);
    disp->preamble = 0;
    disp->font = origfont;
    disp->scrolldelay = 10000;
    disp->brightness = 0;
    sprintf(msg,"*** dcled %s Copyright 2014 Jeff Jahr <malakai@jeffrika.com> ***     ",version);
    scrollmsg(disp,msg);
}

/* scroll a character onto the display.  Not all directions are implemented
 * yet.*/
void scrollchar(struct ledscreen *disp, char ch) {

    int w;

    for (w=1;w<=(disp->font->dispwidth);w++) {
        scroll(3,disp);
        printchar(disp,ch,LEDSX-w);
        send_screen(disp);
        usleep(disp->scrolldelay);
    }
    if(echo){
        fputc(ch,stdout);
        fflush(stdout);
    }
}

/* char by char, scroll a string onto the screen.  This is pretty much what
 * we're here for, isn't it? */
void scrollmsg(struct ledscreen *disp, char* buf) {

    char *p;

    p=buf;

    while(*p){
        scrollchar(disp,*p++);
    }

    return;
}

void keep_lit(struct ledscreen *disp) {
    while (1) {
        send_screen(disp);
        usleep(250000);
    }
}

/* Jump to the end of the message and print just the chars that will fit on the
 * screen without scrolling.  Contributed by Robert Flick */
void printmsg_raw(struct ledscreen *disp, char* msg) {

    char* p = msg;
    int i = 0;
    int numchars, start;
    /* set pack to 0 if you want space between letters and only four chars per display. */
    int pack = 1;

    numchars = LEDSX / ((disp->font->dispwidth)-pack);

    start = strlen(msg);
    if (start > 0) {
        /* trim a trailing newline, which would otherwise be rendered as a
         * space by the line-by-line IO reader.*/
        if ((*(msg+start-1)) == '\n') {
            start = start - numchars -1;
        } else {
            start = start - numchars;
        }
    }
    /* don't backtrack too far. */
    if(start<0) {
        start=0;
    }
    p = msg + start;

    /* put 'em on the screen. */
    while(*p != '\0') {
        printchar(disp,*p, i*((disp->font->dispwidth)-pack));
        p++;
        i++;
    }

}


void printmsg(struct ledscreen *disp, char* msg) {

    clearscreen(0, disp);
    printmsg_raw(disp,msg);
    send_screen(disp);
    /* line print speed can be controlled with the --speed parameter */
    usleep(disp->scrolldelay);
}

/* more static when closer to 100% */
void statictach(struct ledscreen *disp, int load) {

    int x, y;
    float scale;

    clearscreen(0, disp);

    /* set the overall brightnetss. */
    disp->brightness = 0;
    if(load>33) {
        disp->brightness = 1;
    }
    if(load>66) {
        disp->brightness = 2;
    }

    scale = (float)load/100;

    for (x=0;x<LEDSX;x++) {
        for (y=0;y<LEDSY;y++) {
            disp->led[x][y] ^= ((load/100.0) > rand()/(((double)RAND_MAX + 1)))?1:0;
        }
    }

    send_screen(disp);
}

int sqrtscale(float load,int width) {
    width -=1;
    return(round(sqrt(load)*10.0 / 100.0 * width));
}

/* Graph looks like a wildfire spreading. */
void firelinetach(struct ledscreen *disp, int load) {

    int x, y;
    float scale,burnrate;
    int left, right;

    load = sqrt(load) * 10;

    /* scroll the whole display up one. */
    scroll(0,disp);
    burnrate = rand()/((double)RAND_MAX + 1);
    if(burnrate<0.25) {
        scroll(1,disp);
    } else if(burnrate>0.9) {
        scroll(3,disp);
    }

    /* extingquish some existing embers. */
    for (x=0;x<LEDSX;x++) {
        for (y=0;y<LEDSY;y++) {
            burnrate = rand()/((double)RAND_MAX + 1);
            scale = (float)x/LEDSX;
            if(scale < burnrate*burnrate ) {
                disp->led[x][y] = 0;
            }
        }
    }

    scale = (float)load/100;
    left = 0;
    right = round(LEDSX * scale);

    scale = (float)load/100;

    /* draw a new line of fire. */
    for (x=left;x<right;x++) {
        disp->led[x][LEDSY-1] = 1;
    }

    send_screen(disp);
}

/* Left to right bar with overlay of percentage. */
void simpletach(struct ledscreen *disp, int load) {

    char msg[80];
    int x,y;

    clearscreen(0, disp);

    /* set the overall brightnetss. */
    disp->brightness = 0;
    if(load>33) {
        disp->brightness = 1;
    }
    if(load>66) {
        disp->brightness = 2;
    }

    /* print the percentage. */
    sprintf(msg,"%4d%%",load);
    printmsg_raw(disp,msg);

    /* xor the bar onto the screen */
    for(x=0;x< (float)(load/100.0) * LEDSX;x++) {
        for(y=0;y<LEDSY;y++) {
            disp->led[x][y] ^= 1;
        }
    }
    send_screen(disp);
    return;
}

/* simple scrolling bar graph tach */
void scrolltach(struct ledscreen *disp, int load) {

    int y;
    int height;

    scroll(3,disp);

    height = (round((float)load/100.0 * LEDSY));
    for(y=0;y<height;y++) {
        disp->led[LEDSX-1][LEDSY-1-y] ^= 1;
    }
    send_screen(disp);
}


/* Update one frame of tachometer based on the tach variable stored in the
 * display. */
void drawtach(struct ledscreen *disp, int load) {

    switch (disp->tach) {
        case 4:
            firelinetach(disp,load);
            return;
        case 3:
            scrolltach(disp,load);
            return;
        case 2:
            statictach(disp,load);
            return;
        case 1:
        case 0:
        default:
            simpletach(disp,load);
            return;
    }
}


/* Same thing, bigger scope.  Scroll a whole file. */
void scrollfile(struct ledscreen *disp, FILE* cin) {

    char buf[8192];
    time_t now;
    time_t temptime;
    fd_set inset;
    struct timeval timeout;

    while(1) {
        FD_ZERO(&inset);
        FD_SET(fileno(cin),&inset);

        /* How long to wait for IO before coming back to refresh the display. */
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;

        select(fileno(cin)+1,&inset,NULL,NULL,&timeout);

        if ( ! FD_ISSET(fileno(cin),&inset) ) {
            if (repeat) {
                /* send_screen twiddles the last update time. just undo it.*/
                temptime = disp->lastupdate;
                send_screen(disp);
                disp->lastupdate = temptime;
            }
        } else {
            if(fgets(buf,8192,cin)) {
                time(&now);
                if(disp->preamble!= 0 &&
                   (now - disp->lastupdate) > 10) {
                    scrollpreamble(0,disp);
                }
                if(fastprint) {
                    printmsg(disp,buf);
                } else {
                    scrollmsg(disp,buf);
                }
            } else {
                /* end of file. */
                return;
            }
        }
    }

}

/* read stdin for a number between 0-100.  Draw it as a percentage on a
 * tachometer gague.  Could use this for monitoring CPU utilization, task
 * completion, engine RPM's, whatever.  */
void tachfile(struct ledscreen *disp, FILE* cin) {

    char buf[8192];
    fd_set inset;
    struct timeval timeout;
    int load=0;

    while(1) {
        FD_ZERO(&inset);
        FD_SET(fileno(cin),&inset);

        /* How long to wait for IO before coming back to refresh the display. */
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;

        select(fileno(cin)+1,&inset,NULL,NULL,&timeout);

        if ( FD_ISSET(fileno(cin),&inset) ) {
            if(fgets(buf,8192,cin)) {

                if(sscanf(buf,"%d",&load)!=1) {
                    if(debug) {
                        fprintf(stderr,"Couldn't find an integer between 0-100\n");
                        fprintf(stderr,"saw: %s\n",buf);
                    }
                }
                load = (load>100)?100:load;
                load = (load<0)?0:load;

                if(echo){
                    fputs(buf,stdout);
                    fflush(stdout);
                }

            } else {
                /* end of file. */
                return;
            }
            if(!fastprint) {
                usleep(disp->scrolldelay);
            }
        }
        drawtach(disp,load);
    }

}

struct ledfont *allocfont(void) {
    struct ledfont *f;

    int i,j;

    f = malloc(sizeof(struct ledfont));
    f->name = NULL;
    f->description = NULL;
    f->author = NULL;
    f->dataformat = 0;
    f->dispwidth = FONTX;
    f->dispheight = FONTY;

    /* fill it with a pattern. */
    for(i=0;i<256;i++) {
        for(j=0;j<FONTY;j++) {
            f->data[i][j]=(j&1)?0xaa:0x55;
        }
    }

    return(f);
}

/* called via atexit. */
void bye(void) {
    /* if I wanted to clean it up right, this is where I'd do it.*/
}

struct ledfontlist *initfonts(char *dirname) {

    struct ledfontlist *head;
    struct ledfontlist *p;
    struct ledfont *f;
    int i;
    glob_t globbuf;
    char filepat[8192];

    /* the original default 5x7 font. */
    p = head = malloc(sizeof(struct ledfontlist));
    p->font = initfont1(allocfont());
    p->next =  malloc(sizeof(struct ledfontlist));

    /* the tiny font made by Stefan Misch */
    p = p->next;
    p->font = initfont2(allocfont());
    p->next = NULL;

    /* search for any offboard fonts to add to the list. */
    if ( dirname ) {
        sprintf(filepat,"%s/*.dlf",dirname);
    } else {
        sprintf(filepat,"*.dlf");
    }

    if (glob(filepat,0,NULL,&globbuf) != 0) {
        return(head);
    }

    for(i=0;i<globbuf.gl_pathc;i++) {
        if( f = loadfont(globbuf.gl_pathv[i]) ) {
            p->next =  malloc(sizeof(struct ledfontlist));
            p = p->next;
            p->font = f;
            p->next = NULL;
        }
    }

    return(head);

}

/*
 * savefonts() will write out a font in a format that can be read back in.
 * Right now, its converting from the 'compiled in' format into version 0 of
 * the .dlf format.  This function is useful if uou ever want to update the
 * format of dlf files.  use the load function to bring a font in, then call a
 * modified copy of this function to write it out in a new format. I usualy
 * slap it into the fancytest() routine when I need to run it.  -jsj
 */
void savefonts(struct ledfontlist *fontlist) {
    struct ledfontlist *flp;
    struct ledfont *font;
    FILE *out;
    char filename[8192];
    int i;
    int j;

    for(flp=fontlist;flp;flp=flp->next) {
        font = flp->font;
        sprintf(filename,"%s.dlf",font->name);
        if(!(out=fopen(filename,"w"))) {
            perror("Ooops...");
            exit(0);
        }
        fprintf(out,"DcledFontVersion: %d\n",font->dataformat);
        fprintf(out,"Name: %s\n",font->name);
        fprintf(out,"Description: %s\n",font->description);
        fprintf(out,"Author: %s\n",font->author);
        fprintf(out,"Size: %d x %d\n",font->dispwidth,font->dispheight);

        for (i=0;i<256;i++) {
            fprintf(out,"%02x %c ",i,(isgraph(i)?i:'.'));
            for (j=0;j<FONTY;j++) {
                fprintf(out,"%02x ", (unsigned char)font->data[i][j]);
            }
            fprintf(out,"\n");
        }
        fclose(out);
    }

    return;

}

/* given a filename, try and load a font from it.  This probably needs some
 * improvement. -jsj */
struct ledfont *loadfont(char *filename) {

    FILE *in;
    struct ledfont *font;
    char buf[8192];
    int i;
    char junk;
    char row[FONTY];
    int scancount = 0;
    char *s;

    if(!(in=fopen(filename,"r"))) {
        perror(filename);
    }

    font = allocfont();

    if ( (fscanf(in,"DcledFontVersion: %d\n",&(font->dataformat)) != 1) || (font->dataformat !=0)) {
        /* its nothing we understand. */
        return(NULL);
    }

    /* lame.  the fields have tags, so they ought to be able to appear in the
     * file in any order, but I'm doing it this way because I'm in a hurry.  */

    if(fgets(buf,sizeof(buf),in)) {
        if (s = strchr(buf,'\n')) *s = '\0';
        if (s = strchr(buf,' ')) {
            font->name = strdup(s+1);
        } else {
            return(NULL);
        }
    }

    if(fgets(buf,sizeof(buf),in)) {
        if (s = strchr(buf,'\n')) *s = '\0';
        if (s = strchr(buf,' ')) {
            font->description = strdup(s+1);
        } else {
            return(NULL);
        }
    }

    if(fgets(buf,sizeof(buf),in)) {
        if (s = strchr(buf,'\n')) *s = '\0';
        if (s = strchr(buf,' ')) {
            font->author = strdup(s+1);
        } else {
            return(NULL);
        }
    }

    scancount += fscanf(in,"Size: %d x %d\n",&(font->dispwidth),&(font->dispheight));

    while(fgets(buf,sizeof(buf),in)) {
        sscanf(buf,"%02x %c %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx",
               &i,&junk,
               &row[0], &row[1], &row[2], &row[3], &row[4], &row[5], &row[6]
        );
        font->data[i][0] = row[0];
        font->data[i][1] = row[1];
        font->data[i][2] = row[2];
        font->data[i][3] = row[3];
        font->data[i][4] = row[4];
        font->data[i][5] = row[5];
        font->data[i][6] = row[6];
    };

    fclose(in);
    return(font);

}


/* yah, its main allright. */
int main (int argc, char **argv) {

    struct ledscreen maindisp;
    struct ledscreen *disp;
    int getoptc, option_index = 0;
    int brightness = 2;
    int speed = 20000;
    char *msg = NULL;
    int test=0;
    int fileidx=0;
    FILE *cin;
    int preamble=0;
    int dfont=0;
    int clock=0;
    char *fontdir = FONTDIR;
    char *fontname = NULL;
    int printhelp = 0;
    int pickfont = 0;
    int tach=0;

    struct ledfontlist *fontlist;
    struct ledfontlist *flp;

    static struct option long_options[] = {
            { "brightness",	optional_argument,	0, 'b' },
            { "clock",	optional_argument,	0, 'c' },
            { "clock24h",	optional_argument,	0, 'C' },
            { "bcdclock",	optional_argument,	0, 'B' },
            { "debug",	no_argument,	0, 'd' },
            { "echo",	no_argument,	0, 'e' },
            { "help",	no_argument,	0, 'h' },
            { "message",	optional_argument,	0, 'm' },
            { "repeat",	no_argument,	0, 'r' },
            { "fastprint",	no_argument,	0, 'f' },
            { "speed",	optional_argument,	0, 's' },
            { "tach",	optional_argument,	0, 'T' },
            { "preamble",	optional_argument,	0, 'p' },
            { "font",	optional_argument,	0, 'g' },
            { "fontdir",	optional_argument,	0, 'G' },
            { "test",	no_argument,	0, 't' },
            { 0,0,0,0 }
    };

    atexit(bye);

    while (1) {
        getoptc = getopt_long (argc, argv, "ho:m:b:s:T:p:g:G:dtrfencCB",
                               long_options, &option_index
        );

        if (getoptc == -1) break;

        switch (getoptc) {
            default:
            case 'h':
                printhelp = 1;
                break;
            case 'b':
                if (optarg != NULL) {
                    brightness = atoi(optarg);
                }
                if (brightness < 0 || brightness > 2) {
                    fprintf(stderr,"Brightness must be between 0 and 2.\n");
                    exit(1);
                }
                break;
            case 's':
                if (optarg != NULL) {
                    speed = atof(optarg) * 1000;
                }
                if (speed < 0 ) {
                    fprintf(stderr,"Speed has to be a positive delay.\n");
                    exit(1);
                }
                break;
            case 'p':
                if (optarg != NULL) {
                    preamble = atoi(optarg);
                    if (preamble < 0 ) {
                        preamble = 0;
                        /* look for a match by name */
                        while ( preambles[preamble].name != NULL) {
                            if (!strcmp(optarg,preambles[preamble].name)) {
                                break;
                            }
                            preamble++;
                        }
                        if(preambles[preamble].name == NULL ) {
                            fprintf(stdout,"Unknown preamble.  Try -h for the list. \n");
                            preamble=0;
                        }
                    }
                }
                break;
            case 'T':
                if (optarg != NULL) {
                    tach = atoi(optarg);
                    if (tach <= 0 ) {
                        tach = 1;
                        /* look for a match by name */
                        while ( tachs[tach].name != NULL) {
                            if (!strcmp(optarg,tachs[tach].name)) {
                                break;
                            }
                            tach++;
                        }
                        if(tachs[tach].name == NULL ) {
                            fprintf(stdout,"Unknown tach.  Try -h for the list. \n");
                            tach=0;
                        }
                    }
                }
                break;
            case 'G':
                fontdir = strdup(optarg);
                break;
            case 'g':
                fontname = strdup(optarg);
                pickfont = 1;
                break;
            case 'm':
                if (optarg != NULL) {
                    msg = strdup(optarg);
                }
                break;
            case 't':
                test = 1;
                break;
            case 'd':
                debug = 1;
                break;
            case 'r':
                repeat = 1;
                break;
            case 'f':
                fastprint = 1;
                break;
            case 'e':
                echo = 1;
                break;
            case 'n':
                nodev = 1;
                break;
            case 'c':
                clock = 2;
                break;
            case 'C':
                clock = 1;
                break;
            case 'B':
                clock = 3;
                break;
        }
    }

    /* init the builtin fonts */
    fontlist = initfonts(fontdir);

    if(printhelp) {
        /* bah getopt sucks.  Why do i have to format this?*/
        /* maybe i just dont know the right way...*/
        fprintf(stdout,"Usage- %s [opts] [files]\n\n",argv[0]);
        fprintf(stdout,"\t--brightness  -b   How bright, 0-2\n");
        fprintf(stdout,"\t--clock       -c   Show the time\n");
        fprintf(stdout,"\t--clock24h    -C   Show the 24h time\n");
        fprintf(stdout,"\t--bcdclock    -B   Show the time in binary\n");
        fprintf(stdout,"\t--debug       -d   Mostly useless\n");
        fprintf(stdout,"\t--echo        -e   Send copy to stdout\n");
        fprintf(stdout,"\t--help        -h   Show this message\n");
        fprintf(stdout,"\t--message     -m   A single line message to scroll\n");
        fprintf(stdout,"\t--nodev       -n   Don't use the device\n");
        fprintf(stdout,"\t--preamble    -p   Send a graphic before the text.\n");
        fprintf(stdout,"\t--repeat      -r   Keep scrolling forever\n");
        fprintf(stdout,"\t--fastprint   -f   Jump to end of message.\n");
        fprintf(stdout,"\t--speed       -s   General delay in ms\n");
        fprintf(stdout,"\t--tach        -T   Display a tachometer\n");
        fprintf(stdout,"\t--test        -t   Output a test pattern\n");
        fprintf(stdout,"\t--font        -g   Select a font\n");
        fprintf(stdout,"\t--fontdir     -G   Select a font directory\n");
        fprintf(stdout,"\n");
        fprintf(stdout,"Available preamble graphics:\n\n");
        preamble = 1;
        while ( preambles[preamble].name != NULL) {
            fprintf(stdout,"\t%2d - %-10s - %s\n",
                    preamble,
                    preambles[preamble].name,
                    preambles[preamble].description
            );
            preamble++;
        }
        fprintf(stdout,"\n");

        fprintf(stdout,"Available tachometer displays:\n\n");
        preamble = 1;
        while ( tachs[preamble].name != NULL) {
            fprintf(stdout,"\t%2d - %-10s - %s\n",
                    preamble,
                    tachs[preamble].name,
                    tachs[preamble].description
            );
            preamble++;
        }
        fprintf(stdout,"\n");

        fprintf(stdout,"Optional fonts:\n\n");
        for(dfont=1,flp=fontlist->next;flp;flp=flp->next,dfont++) {
            fprintf(stdout,"\t%2d - %-10s - %s\n",
                    dfont,
                    flp->font->name,
                    flp->font->description
            );
        }
        fprintf(stdout,"\n");
        exit(0);
    }

    disp = &maindisp;
    disp->brightness = brightness;
    disp->scrolldelay = speed;
    disp->scrolldir = 3;
    disp->preamble = preamble;
    disp->tach = tach;

    /* the first font in the list is the default. */
    disp->font = fontlist->font;
    /* maybe override that. */
    if(pickfont) {
        if (fontname != NULL) {
            dfont = atoi(fontname);

            if (dfont == 0 ) {
                /* look for a match by name */
                for(flp=fontlist;flp;flp=flp->next) {
                    if(!strcmp(flp->font->name,fontname)) {
                        disp->font = flp->font;
                        break;
                    }
                }
            } else {
                if(dfont > 0) {
                    /* its a number.  skip forward that many.*/
                    for(flp=fontlist;flp && dfont ;flp=flp->next,dfont--);
                    if(flp) {
                        disp->font = flp->font;
                    } else {
                        dfont = -1;
                    }
                }
                if (dfont < 0) {
                    fprintf(stdout,"Unknown font.  Try -h for the list. \n");
                    exit(0);
                }
            }
        }
    }

    srand(getpid());

    if (!nodev) {
        if(open_hiddev(disp)==EXIT_FAILURE) {
            fprintf(stderr,"Couldn't find the device.  Was expecting to find a readable\ndevice that matched vendor %0x and product %0x.  Is the\ndevice plugged in? Do you have permission?\n",VENDOR,PRODUCT);
            return(EXIT_FAILURE);
        }
    }


    /* clears the display */
    clearscreen(0,disp);

    if(test) {
        fprintf(stdout,"Version is %s\n",version);
        fprintf(stdout,"brightness is %d\n",brightness);
        fprintf(stdout,"debug is %d\n",debug);
        fprintf(stdout,"font directory is %s\n",FONTDIR);
        disp->scrolldelay = 10000;
        fancytest(disp,fontlist);
        close_hiddev(disp);
        exit(0);
    }

    if(clock) {
        clockmode(disp,clock,repeat);
        close_hiddev(disp);
        exit(0);
    }

    /* if there is a message, print it and dont bother with files. */
    if(msg != NULL) {
        do {
            scrollpreamble(0,disp);
            if(fastprint) {
                printmsg(disp,msg);
            } else {
                scrollmsg(disp,msg);
            }
            scrollpreamble(1,disp);
        } while (repeat);
        close_hiddev(disp);
        exit(0);
    }

    /* if there are files, print them and dont bother with stdin. */
    if(optind < argc) {
        do {
            fileidx = optind;
            while (fileidx < argc) {
                if ( (cin = fopen(argv[fileidx],"r")) == NULL ) {
                    fprintf(stderr,"Couldnt open %s: %s\n",
                            argv[fileidx], strerror(errno)
                    );
                    close_hiddev(disp);
                    exit(0);
                }
                scrollpreamble(0,disp);
                if(tach) {
                    tachfile(disp,cin);
                } else {
                    scrollfile(disp,cin);
                }
                scrollpreamble(1,disp);
                fclose(cin);
                fileidx++;
            }
        } while (repeat);
        close_hiddev(disp);
        exit(0);
    }

    /* read from stdin. */
    scrollpreamble(0,disp);
    if(tach) {
        tachfile(disp,stdin);
    } else {
        scrollfile(disp,stdin);
    }
    scrollpreamble(1,disp);
    close_hiddev(disp);

}

/*
  Copy a font definition into a character pointer.  In this application, fonts
  are 256x7 bytes.  256 one byte chars by 7 font rows.

  default font
*/
struct ledfont *initfont1(struct ledfont *target) {

    /*
     Font is 7 bytes per entry, each byte is a row.  The character bitmaps are
     like 5 bits wide, mirrored, starting at bit zero.  Why so bizzare, you
     ask?  Oh god, the horror of converting from an existing font to this...
     ImageMagick -draw text, conversion to xbm format, tcl scripts to parse the
     xbm data into this c code... anyway, it was faster than drawing a font
     myself, although not by much.  Ulitmately, this array was built
     automatically from the X11 5x7 font, and it works.  That is what matters
     for an afternoon project.  Feel free to improve it. :)
    */

    /*
     Now that there's a way to load font files, I could have removed this font
     from the source code and provided a default.dlf file.  However, I like the
     idea of having the default font compiled in.
    */

    static char font[256][FONTY] = {
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00 },
            { 0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x00 },
            { 0x00, 0x0E, 0x05, 0x0E, 0x14, 0x0E, 0x00 },
            { 0x01, 0x09, 0x04, 0x02, 0x09, 0x08, 0x00 },
            { 0x00, 0x02, 0x05, 0x02, 0x05, 0x0A, 0x00 },
            { 0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00 },
            { 0x04, 0x02, 0x02, 0x02, 0x02, 0x04, 0x00 },
            { 0x02, 0x04, 0x04, 0x04, 0x04, 0x02, 0x00 },
            { 0x00, 0x0A, 0x04, 0x0E, 0x04, 0x0A, 0x00 },
            { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 },
            { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x02 },
            { 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00, 0x06, 0x06, 0x00 },
            { 0x00, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00 },
            { 0x04, 0x0A, 0x0A, 0x0A, 0x0A, 0x04, 0x00 },
            { 0x04, 0x06, 0x04, 0x04, 0x04, 0x0E, 0x00 },
            { 0x06, 0x09, 0x08, 0x04, 0x02, 0x0F, 0x00 },
            { 0x0F, 0x08, 0x06, 0x08, 0x09, 0x06, 0x00 },
            { 0x04, 0x06, 0x05, 0x0F, 0x04, 0x04, 0x00 },
            { 0x0F, 0x01, 0x07, 0x08, 0x09, 0x06, 0x00 },
            { 0x06, 0x01, 0x07, 0x09, 0x09, 0x06, 0x00 },
            { 0x0F, 0x08, 0x04, 0x04, 0x02, 0x02, 0x00 },
            { 0x06, 0x09, 0x06, 0x09, 0x09, 0x06, 0x00 },
            { 0x06, 0x09, 0x09, 0x0E, 0x08, 0x06, 0x00 },
            { 0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00 },
            { 0x00, 0x06, 0x06, 0x00, 0x06, 0x02, 0x01 },
            { 0x00, 0x08, 0x04, 0x02, 0x04, 0x08, 0x00 },
            { 0x00, 0x00, 0x0F, 0x00, 0x0F, 0x00, 0x00 },
            { 0x00, 0x02, 0x04, 0x08, 0x04, 0x02, 0x00 },
            { 0x04, 0x0A, 0x08, 0x04, 0x00, 0x04, 0x00 },
            { 0x06, 0x09, 0x0D, 0x0D, 0x01, 0x06, 0x00 },
            { 0x06, 0x09, 0x09, 0x0F, 0x09, 0x09, 0x00 },
            { 0x07, 0x09, 0x07, 0x09, 0x09, 0x07, 0x00 },
            { 0x06, 0x09, 0x01, 0x01, 0x09, 0x06, 0x00 },
            { 0x07, 0x09, 0x09, 0x09, 0x09, 0x07, 0x00 },
            { 0x0F, 0x01, 0x07, 0x01, 0x01, 0x0F, 0x00 },
            { 0x0F, 0x01, 0x07, 0x01, 0x01, 0x01, 0x00 },
            { 0x06, 0x09, 0x01, 0x0D, 0x09, 0x0E, 0x00 },
            { 0x09, 0x09, 0x0F, 0x09, 0x09, 0x09, 0x00 },
            { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 },
            { 0x08, 0x08, 0x08, 0x08, 0x09, 0x06, 0x00 },
            { 0x09, 0x05, 0x03, 0x03, 0x05, 0x09, 0x00 },
            { 0x01, 0x01, 0x01, 0x01, 0x01, 0x0F, 0x00 },
            { 0x09, 0x0F, 0x0F, 0x09, 0x09, 0x09, 0x00 },
            { 0x09, 0x0B, 0x0B, 0x0D, 0x0D, 0x09, 0x00 },
            { 0x06, 0x09, 0x09, 0x09, 0x09, 0x06, 0x00 },
            { 0x07, 0x09, 0x09, 0x07, 0x01, 0x01, 0x00 },
            { 0x06, 0x09, 0x09, 0x09, 0x0B, 0x06, 0x08 },
            { 0x07, 0x09, 0x09, 0x07, 0x05, 0x09, 0x00 },
            { 0x06, 0x09, 0x02, 0x04, 0x09, 0x06, 0x00 },
            { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 },
            { 0x09, 0x09, 0x09, 0x09, 0x09, 0x06, 0x00 },
            { 0x09, 0x09, 0x09, 0x09, 0x06, 0x06, 0x00 },
            { 0x09, 0x09, 0x09, 0x0F, 0x0F, 0x09, 0x00 },
            { 0x09, 0x09, 0x06, 0x06, 0x09, 0x09, 0x00 },
            { 0x0A, 0x0A, 0x0A, 0x04, 0x04, 0x04, 0x00 },
            { 0x0F, 0x08, 0x04, 0x02, 0x01, 0x0F, 0x00 },
            { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x0E, 0x00 },
            { 0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x00 },
            { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x0E, 0x00 },
            { 0x04, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00 },
            { 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x0E, 0x09, 0x0D, 0x0A, 0x00 },
            { 0x01, 0x01, 0x07, 0x09, 0x09, 0x07, 0x00 },
            { 0x00, 0x00, 0x06, 0x01, 0x01, 0x06, 0x00 },
            { 0x08, 0x08, 0x0E, 0x09, 0x09, 0x0E, 0x00 },
            { 0x00, 0x00, 0x06, 0x0D, 0x03, 0x06, 0x00 },
            { 0x04, 0x0A, 0x02, 0x07, 0x02, 0x02, 0x00 },
            /* This is the g converted from the 5x7 font */
            /*{ 0x00, 0x00, 0x0E, 0x09, 0x06, 0x01, 0x0E },*/
            /* This one is from Andy Scheller. its better.*/
            { 0x00, 0x00, 0x0E, 0x09, 0x0E, 0x08, 0x06 },
            /* end */
            { 0x01, 0x01, 0x07, 0x09, 0x09, 0x09, 0x00 },
            { 0x04, 0x00, 0x06, 0x04, 0x04, 0x0E, 0x00 },
            { 0x08, 0x00, 0x08, 0x08, 0x08, 0x0A, 0x04 },
            { 0x01, 0x01, 0x05, 0x03, 0x05, 0x09, 0x00 },
            { 0x06, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 },
            { 0x00, 0x00, 0x05, 0x0F, 0x09, 0x09, 0x00 },
            { 0x00, 0x00, 0x07, 0x09, 0x09, 0x09, 0x00 },
            { 0x00, 0x00, 0x06, 0x09, 0x09, 0x06, 0x00 },
            { 0x00, 0x00, 0x07, 0x09, 0x09, 0x07, 0x01 },
            { 0x00, 0x00, 0x0E, 0x09, 0x09, 0x0E, 0x08 },
            { 0x00, 0x00, 0x07, 0x09, 0x01, 0x01, 0x00 },
            { 0x00, 0x00, 0x0E, 0x03, 0x0C, 0x07, 0x00 },
            { 0x02, 0x02, 0x07, 0x02, 0x02, 0x0C, 0x00 },
            { 0x00, 0x00, 0x09, 0x09, 0x09, 0x0E, 0x00 },
            { 0x00, 0x00, 0x0A, 0x0A, 0x0A, 0x04, 0x00 },
            { 0x00, 0x00, 0x09, 0x09, 0x0F, 0x0F, 0x00 },
            { 0x00, 0x00, 0x09, 0x06, 0x06, 0x09, 0x00 },
            { 0x00, 0x00, 0x09, 0x09, 0x0A, 0x04, 0x02 },
            { 0x00, 0x00, 0x0F, 0x04, 0x02, 0x0F, 0x00 },
            { 0x08, 0x04, 0x06, 0x04, 0x04, 0x08, 0x00 },
            { 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 },
            { 0x02, 0x04, 0x0C, 0x04, 0x04, 0x02, 0x00 },
            { 0x0A, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x04, 0x00, 0x04, 0x04, 0x04, 0x04, 0x00 },
            { 0x00, 0x04, 0x0E, 0x05, 0x05, 0x0E, 0x04 },
            { 0x00, 0x0C, 0x02, 0x07, 0x02, 0x0D, 0x00 },
            { 0x00, 0x11, 0x0E, 0x0A, 0x0E, 0x11, 0x00 },
            { 0x0A, 0x0A, 0x04, 0x0E, 0x04, 0x04, 0x00 },
            { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 },
            { 0x0C, 0x02, 0x06, 0x0A, 0x0C, 0x08, 0x06 },
            { 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x0E, 0x11, 0x15, 0x13, 0x15, 0x11, 0x0E },
            { 0x06, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x12, 0x09, 0x12, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x0F, 0x08, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00 },
            { 0x0E, 0x11, 0x17, 0x13, 0x13, 0x11, 0x0E },
            { 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x04, 0x0A, 0x04, 0x00, 0x00, 0x00, 0x00 },
            { 0x04, 0x04, 0x1F, 0x04, 0x04, 0x1F, 0x00 },
            { 0x06, 0x04, 0x02, 0x06, 0x00, 0x00, 0x00 },
            { 0x06, 0x06, 0x04, 0x06, 0x00, 0x00, 0x00 },
            { 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x09, 0x09, 0x09, 0x07, 0x01 },
            { 0x0E, 0x0B, 0x0B, 0x0A, 0x0A, 0x0A, 0x00 },
            { 0x00, 0x00, 0x06, 0x06, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x02 },
            { 0x04, 0x06, 0x04, 0x0E, 0x00, 0x00, 0x00 },
            { 0x02, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x09, 0x12, 0x09, 0x00, 0x00 },
            { 0x01, 0x01, 0x01, 0x09, 0x0C, 0x0E, 0x08 },
            { 0x01, 0x01, 0x01, 0x0D, 0x08, 0x04, 0x0C },
            { 0x03, 0x03, 0x02, 0x0B, 0x0C, 0x0E, 0x08 },
            { 0x04, 0x00, 0x04, 0x02, 0x0A, 0x04, 0x00 },
            { 0x06, 0x09, 0x09, 0x0F, 0x09, 0x09, 0x00 },
            { 0x06, 0x09, 0x09, 0x0F, 0x09, 0x09, 0x00 },
            { 0x06, 0x09, 0x09, 0x0F, 0x09, 0x09, 0x00 },
            { 0x06, 0x09, 0x09, 0x0F, 0x09, 0x09, 0x00 },
            { 0x09, 0x06, 0x09, 0x0F, 0x09, 0x09, 0x00 },
            { 0x06, 0x06, 0x09, 0x0F, 0x09, 0x09, 0x00 },
            { 0x0E, 0x05, 0x0D, 0x07, 0x05, 0x0D, 0x00 },
            { 0x06, 0x09, 0x01, 0x01, 0x09, 0x06, 0x02 },
            { 0x0F, 0x01, 0x07, 0x01, 0x01, 0x0F, 0x00 },
            { 0x0F, 0x01, 0x07, 0x01, 0x01, 0x0F, 0x00 },
            { 0x0F, 0x01, 0x07, 0x01, 0x01, 0x0F, 0x00 },
            { 0x0F, 0x01, 0x07, 0x01, 0x01, 0x0F, 0x00 },
            { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 },
            { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 },
            { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 },
            { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00 },
            { 0x07, 0x0A, 0x0B, 0x0A, 0x0A, 0x07, 0x00 },
            { 0x0D, 0x09, 0x0B, 0x0D, 0x0D, 0x09, 0x00 },
            { 0x06, 0x09, 0x09, 0x09, 0x09, 0x06, 0x00 },
            { 0x06, 0x09, 0x09, 0x09, 0x09, 0x06, 0x00 },
            { 0x06, 0x09, 0x09, 0x09, 0x09, 0x06, 0x00 },
            { 0x06, 0x09, 0x09, 0x09, 0x09, 0x06, 0x00 },
            { 0x09, 0x06, 0x09, 0x09, 0x09, 0x06, 0x00 },
            { 0x00, 0x00, 0x09, 0x06, 0x06, 0x09, 0x00 },
            { 0x0E, 0x0D, 0x0D, 0x0B, 0x0B, 0x07, 0x00 },
            { 0x09, 0x09, 0x09, 0x09, 0x09, 0x06, 0x00 },
            { 0x09, 0x09, 0x09, 0x09, 0x09, 0x06, 0x00 },
            { 0x09, 0x09, 0x09, 0x09, 0x09, 0x06, 0x00 },
            { 0x09, 0x00, 0x09, 0x09, 0x09, 0x06, 0x00 },
            { 0x0A, 0x0A, 0x0A, 0x04, 0x04, 0x04, 0x00 },
            { 0x01, 0x07, 0x09, 0x07, 0x01, 0x01, 0x00 },
            { 0x06, 0x09, 0x05, 0x09, 0x09, 0x05, 0x00 },
            { 0x02, 0x04, 0x0E, 0x09, 0x0D, 0x0A, 0x00 },
            { 0x04, 0x02, 0x0E, 0x09, 0x0D, 0x0A, 0x00 },
            { 0x04, 0x0A, 0x0E, 0x09, 0x0D, 0x0A, 0x00 },
            { 0x0A, 0x05, 0x0E, 0x09, 0x0D, 0x0A, 0x00 },
            { 0x0A, 0x00, 0x0E, 0x09, 0x0D, 0x0A, 0x00 },
            { 0x06, 0x06, 0x0E, 0x09, 0x0D, 0x0A, 0x00 },
            { 0x00, 0x00, 0x0E, 0x0D, 0x05, 0x0E, 0x00 },
            { 0x00, 0x00, 0x0C, 0x02, 0x02, 0x0C, 0x04 },
            { 0x02, 0x04, 0x06, 0x0D, 0x03, 0x06, 0x00 },
            { 0x04, 0x02, 0x06, 0x0D, 0x03, 0x06, 0x00 },
            { 0x02, 0x05, 0x06, 0x0D, 0x03, 0x06, 0x00 },
            { 0x05, 0x00, 0x06, 0x0D, 0x03, 0x06, 0x00 },
            { 0x02, 0x04, 0x06, 0x04, 0x04, 0x0E, 0x00 },
            { 0x04, 0x02, 0x06, 0x04, 0x04, 0x0E, 0x00 },
            { 0x04, 0x0A, 0x06, 0x04, 0x04, 0x0E, 0x00 },
            { 0x0A, 0x00, 0x06, 0x04, 0x04, 0x0E, 0x00 },
            { 0x02, 0x0C, 0x06, 0x09, 0x09, 0x06, 0x00 },
            { 0x0A, 0x05, 0x07, 0x09, 0x09, 0x09, 0x00 },
            { 0x02, 0x04, 0x06, 0x09, 0x09, 0x06, 0x00 },
            { 0x04, 0x02, 0x06, 0x09, 0x09, 0x06, 0x00 },
            { 0x06, 0x00, 0x06, 0x09, 0x09, 0x06, 0x00 },
            { 0x0A, 0x05, 0x06, 0x09, 0x09, 0x06, 0x00 },
            { 0x0A, 0x00, 0x06, 0x09, 0x09, 0x06, 0x00 },
            { 0x00, 0x06, 0x00, 0x0F, 0x00, 0x06, 0x00 },
            { 0x00, 0x00, 0x0E, 0x0D, 0x0B, 0x07, 0x00 },
            { 0x02, 0x04, 0x09, 0x09, 0x09, 0x0E, 0x00 },
            { 0x04, 0x02, 0x09, 0x09, 0x09, 0x0E, 0x00 },
            { 0x06, 0x00, 0x09, 0x09, 0x09, 0x0E, 0x00 },
            { 0x0A, 0x00, 0x09, 0x09, 0x09, 0x0E, 0x00 },
            { 0x04, 0x02, 0x09, 0x09, 0x0A, 0x04, 0x02 },
            { 0x00, 0x01, 0x07, 0x09, 0x09, 0x07, 0x01 },
            { 0x0A, 0x00, 0x09, 0x09, 0x0A, 0x04, 0x02 },
    };

    target->name = strdup("default");
    target->description = strdup("The default font, taken from X11's 5x7");
    target->author = strdup("Jeff Jahr");
    target->dataformat = 0;
    target->dispwidth = 5;
    target->dispheight = 7;

    memcpy(target->data,&font,sizeof(font));

    return(target);
};
/*
  font with smaller characters by Stefan Misch <misel@misel.de>
*/
struct ledfont *initfont2 (struct ledfont *target) {

    static char font[256][FONTY] = {
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x02, 0x02, 0x02, 0x00, 0x02, 0x00, 0x00 },
            { 0x05, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x02, 0x07, 0x02, 0x07, 0x02, 0x00, 0x00 },
            { 0x06, 0x03, 0x02, 0x06, 0x03, 0x00, 0x00 },
            { 0x05, 0x04, 0x02, 0x01, 0x05, 0x00, 0x00 },
            { 0x02, 0x05, 0x02, 0x05, 0x06, 0x00, 0x00 },
            { 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x02, 0x01, 0x01, 0x01, 0x02, 0x00, 0x00 },
            { 0x02, 0x04, 0x04, 0x04, 0x02, 0x00, 0x00 },
            { 0x00, 0x05, 0x02, 0x05, 0x00, 0x00, 0x00 },
            { 0x00, 0x02, 0x07, 0x02, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00 },
            { 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00 },
            { 0x04, 0x04, 0x02, 0x01, 0x01, 0x00, 0x00 },
            { 0x02, 0x05, 0x07, 0x05, 0x02, 0x00, 0x00 },
            { 0x04, 0x06, 0x05, 0x04, 0x04, 0x00, 0x00 },
            { 0x02, 0x05, 0x02, 0x01, 0x07, 0x00, 0x00 },
            { 0x03, 0x04, 0x02, 0x04, 0x03, 0x00, 0x00 },
            { 0x05, 0x05, 0x07, 0x04, 0x04, 0x00, 0x00 },
            { 0x07, 0x01, 0x03, 0x04, 0x03, 0x00, 0x00 },
            { 0x02, 0x01, 0x03, 0x05, 0x02, 0x00, 0x00 },
            { 0x07, 0x04, 0x02, 0x02, 0x02, 0x00, 0x00 },
            { 0x02, 0x05, 0x02, 0x05, 0x02, 0x00, 0x00 },
            { 0x02, 0x05, 0x06, 0x04, 0x02, 0x00, 0x00 },
            { 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x00 },
            { 0x00, 0x02, 0x00, 0x02, 0x02, 0x00, 0x00 },
            { 0x08, 0x04, 0x02, 0x04, 0x08, 0x00, 0x00 },
            { 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00 },
            { 0x01, 0x02, 0x04, 0x02, 0x01, 0x00, 0x00 },
            { 0x02, 0x05, 0x04, 0x02, 0x00, 0x02, 0x00 },
            { 0x02, 0x05, 0x07, 0x01, 0x06, 0x00, 0x00 },
            { 0x02, 0x05, 0x07, 0x05, 0x05, 0x00, 0x00 },
            { 0x03, 0x05, 0x03, 0x05, 0x03, 0x00, 0x00 },
            { 0x06, 0x01, 0x01, 0x01, 0x06, 0x00, 0x00 },
            { 0x03, 0x05, 0x05, 0x05, 0x03, 0x00, 0x00 },
            { 0x07, 0x01, 0x03, 0x01, 0x07, 0x00, 0x00 },
            { 0x07, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00 },
            { 0x02, 0x05, 0x01, 0x05, 0x06, 0x00, 0x00 },
            { 0x05, 0x05, 0x07, 0x05, 0x05, 0x00, 0x00 },
            { 0x02, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00 },
            { 0x04, 0x04, 0x04, 0x04, 0x03, 0x00, 0x00 },
            { 0x05, 0x05, 0x03, 0x05, 0x05, 0x00, 0x00 },
            { 0x01, 0x01, 0x01, 0x01, 0x07, 0x00, 0x00 },
            { 0x05, 0x07, 0x07, 0x05, 0x05, 0x00, 0x00 },
            { 0x05, 0x07, 0x07, 0x07, 0x05, 0x00, 0x00 },
            { 0x02, 0x05, 0x05, 0x05, 0x02, 0x00, 0x00 },
            { 0x03, 0x05, 0x03, 0x01, 0x01, 0x00, 0x00 },
            { 0x02, 0x05, 0x05, 0x05, 0x06, 0x00, 0x00 },
            { 0x03, 0x05, 0x03, 0x05, 0x05, 0x00, 0x00 },
            { 0x06, 0x01, 0x02, 0x04, 0x03, 0x00, 0x00 },
            { 0x07, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00 },
            { 0x05, 0x05, 0x05, 0x05, 0x06, 0x00, 0x00 },
            { 0x05, 0x05, 0x05, 0x02, 0x02, 0x00, 0x00 },
            { 0x05, 0x05, 0x07, 0x07, 0x02, 0x00, 0x00 },
            { 0x05, 0x05, 0x02, 0x05, 0x05, 0x00, 0x00 },
            { 0x05, 0x05, 0x05, 0x02, 0x01, 0x00, 0x00 },
            { 0x07, 0x04, 0x02, 0x01, 0x07, 0x00, 0x00 },
            { 0x07, 0x01, 0x01, 0x01, 0x07, 0x00, 0x00 },
            { 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00 },
            { 0x07, 0x04, 0x04, 0x04, 0x07, 0x00, 0x00 },
            { 0x02, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00 },
            { 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x06, 0x05, 0x06, 0x00, 0x00 },
            { 0x01, 0x01, 0x03, 0x05, 0x03, 0x00, 0x00 },
            { 0x00, 0x00, 0x06, 0x01, 0x06, 0x00, 0x00 },
            { 0x04, 0x04, 0x06, 0x05, 0x06, 0x00, 0x00 },
            { 0x00, 0x00, 0x02, 0x07, 0x06, 0x00, 0x00 },
            { 0x02, 0x05, 0x01, 0x03, 0x01, 0x00, 0x00 },
            { 0x00, 0x00, 0x06, 0x05, 0x06, 0x04, 0x02 },
            { 0x01, 0x01, 0x03, 0x05, 0x05, 0x00, 0x00 },
            { 0x02, 0x00, 0x02, 0x02, 0x02, 0x00, 0x00 },
            { 0x02, 0x00, 0x02, 0x02, 0x02, 0x02, 0x01 },
            { 0x01, 0x01, 0x05, 0x03, 0x05, 0x00, 0x00 },
            { 0x01, 0x01, 0x01, 0x01, 0x03, 0x00, 0x00 },
            { 0x00, 0x00, 0x03, 0x07, 0x07, 0x00, 0x00 },
            { 0x00, 0x00, 0x03, 0x05, 0x05, 0x00, 0x00 },
            { 0x00, 0x00, 0x02, 0x05, 0x02, 0x00, 0x00 },
            { 0x00, 0x00, 0x03, 0x05, 0x03, 0x01, 0x01 },
            { 0x00, 0x00, 0x06, 0x05, 0x06, 0x04, 0x04 },
            { 0x00, 0x00, 0x07, 0x01, 0x01, 0x00, 0x00 },
            { 0x00, 0x00, 0x06, 0x02, 0x03, 0x00, 0x00 },
            { 0x02, 0x02, 0x07, 0x02, 0x02, 0x00, 0x00 },
            { 0x00, 0x00, 0x05, 0x05, 0x06, 0x00, 0x00 },
            { 0x00, 0x00, 0x05, 0x05, 0x02, 0x00, 0x00 },
            { 0x00, 0x00, 0x05, 0x07, 0x02, 0x00, 0x00 },
            { 0x00, 0x00, 0x05, 0x02, 0x05, 0x00, 0x00 },
            { 0x00, 0x00, 0x05, 0x05, 0x05, 0x02, 0x01 },
            { 0x00, 0x00, 0x07, 0x02, 0x07, 0x00, 0x00 },
            { 0x04, 0x02, 0x03, 0x02, 0x04, 0x00, 0x00 },
            { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00 },
            { 0x01, 0x02, 0x06, 0x02, 0x01, 0x00, 0x00 },
            { 0x06, 0x83, 0x00, 0x80, 0x00, 0x80, 0x00 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x02, 0x00, 0x02, 0x02, 0x02, 0x00, 0x00 },
            { 0x02, 0x07, 0x03, 0x07, 0x02, 0x00, 0x00 },
            { 0x06, 0x02, 0x07, 0x02, 0x07, 0x00, 0x00 },
            { 0x00, 0x05, 0x02, 0x02, 0x05, 0x00, 0x00 },
            { 0x05, 0x05, 0x02, 0x07, 0x02, 0x00, 0x00 },
            { 0x02, 0x02, 0x00, 0x02, 0x02, 0x00, 0x80 },
            { 0x06, 0x01, 0x03, 0x06, 0x04, 0x02, 0x01 },
            { 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x07, 0x05, 0x07, 0x07, 0x05, 0x07, 0x00 },
            { 0x06, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x06, 0x03, 0x06, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x07, 0x04, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00 },
            { 0x02, 0x05, 0x07, 0x07, 0x05, 0x02, 0x00 },
            { 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x02, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00 },
            { 0x02, 0x02, 0x07, 0x02, 0x07, 0x00, 0x00 },
            { 0x03, 0x02, 0x01, 0x03, 0x00, 0x00, 0x00 },
            { 0x03, 0x03, 0x02, 0x03, 0x00, 0x00, 0x00 },
            { 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x05, 0x05, 0x03, 0x01, 0x01 },
            { 0x06, 0x07, 0x07, 0x06, 0x06, 0x0E, 0x00 },
            { 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00 },
            { 0x02, 0x03, 0x02, 0x07, 0x00, 0x00, 0x00 },
            { 0x02, 0x05, 0x02, 0x00, 0x00, 0x00, 0x00 },
            { 0x00, 0x03, 0x06, 0x03, 0x00, 0x00, 0x00 },
            { 0x01, 0x01, 0x05, 0x06, 0x07, 0x04, 0x00 },
            { 0x01, 0x01, 0x07, 0x04, 0x02, 0x06, 0x00 },
            { 0x03, 0x03, 0x02, 0x07, 0x07, 0x04, 0x00 },
            { 0x02, 0x00, 0x02, 0x01, 0x06, 0x00, 0x00 },
            { 0x02, 0x05, 0x07, 0x05, 0x05, 0x00, 0x00 },
            { 0x02, 0x05, 0x07, 0x05, 0x05, 0x00, 0x00 },
            { 0x02, 0x05, 0x07, 0x05, 0x05, 0x00, 0x00 },
            { 0x02, 0x05, 0x07, 0x05, 0x05, 0x00, 0x00 },
            { 0x05, 0x02, 0x05, 0x07, 0x05, 0x00, 0x00 },
            { 0x02, 0x02, 0x05, 0x07, 0x05, 0x00, 0x00 },
            { 0x06, 0x03, 0x07, 0x03, 0x07, 0x00, 0x00 },
            { 0x06, 0x01, 0x01, 0x01, 0x06, 0x00, 0x00 },
            { 0x07, 0x01, 0x03, 0x01, 0x07, 0x00, 0x00 },
            { 0x07, 0x01, 0x03, 0x01, 0x07, 0x00, 0x00 },
            { 0x07, 0x01, 0x03, 0x01, 0x07, 0x00, 0x00 },
            { 0x07, 0x01, 0x03, 0x01, 0x07, 0x00, 0x00 },
            { 0x07, 0x02, 0x02, 0x02, 0x07, 0x00, 0x00 },
            { 0x07, 0x02, 0x02, 0x02, 0x07, 0x00, 0x00 },
            { 0x07, 0x02, 0x02, 0x02, 0x07, 0x00, 0x00 },
            { 0x07, 0x02, 0x02, 0x02, 0x07, 0x00, 0x00 },
            { 0x03, 0x06, 0x07, 0x06, 0x03, 0x00, 0x00 },
            { 0x07, 0x05, 0x07, 0x07, 0x05, 0x00, 0x00 },
            { 0x02, 0x05, 0x05, 0x05, 0x02, 0x00, 0x00 },
            { 0x02, 0x05, 0x05, 0x05, 0x02, 0x00, 0x00 },
            { 0x02, 0x05, 0x05, 0x05, 0x02, 0x00, 0x00 },
            { 0x02, 0x05, 0x05, 0x05, 0x02, 0x00, 0x00 },
            { 0x05, 0x02, 0x05, 0x05, 0x02, 0x00, 0x00 },
            { 0x00, 0x00, 0x05, 0x02, 0x05, 0x00, 0x00 },
            { 0x06, 0x07, 0x05, 0x07, 0x03, 0x00, 0x00 },
            { 0x05, 0x05, 0x05, 0x05, 0x06, 0x00, 0x00 },
            { 0x05, 0x05, 0x05, 0x05, 0x06, 0x00, 0x00 },
            { 0x05, 0x05, 0x05, 0x05, 0x06, 0x00, 0x00 },
            { 0x05, 0x00, 0x05, 0x05, 0x06, 0x00, 0x00 },
            { 0x05, 0x05, 0x02, 0x02, 0x02, 0x00, 0x00 },
            { 0x01, 0x07, 0x07, 0x01, 0x01, 0x00, 0x00 },
            { 0x02, 0x07, 0x03, 0x07, 0x03, 0x00, 0x00 },
            { 0x01, 0x02, 0x06, 0x05, 0x06, 0x00, 0x00 },
            { 0x02, 0x01, 0x06, 0x05, 0x06, 0x00, 0x00 },
            { 0x02, 0x05, 0x06, 0x05, 0x06, 0x00, 0x00 },
            { 0x06, 0x03, 0x06, 0x05, 0x06, 0x00, 0x00 },
            { 0x05, 0x00, 0x06, 0x05, 0x06, 0x00, 0x00 },
            { 0x02, 0x00, 0x06, 0x05, 0x06, 0x00, 0x00 },
            { 0x00, 0x00, 0x06, 0x07, 0x06, 0x00, 0x00 },
            { 0x00, 0x00, 0x06, 0x01, 0x06, 0x00, 0x00 },
            { 0x01, 0x02, 0x02, 0x07, 0x06, 0x00, 0x00 },
            { 0x02, 0x01, 0x02, 0x07, 0x06, 0x00, 0x00 },
            { 0x02, 0x05, 0x02, 0x07, 0x06, 0x00, 0x00 },
            { 0x05, 0x00, 0x02, 0x07, 0x06, 0x00, 0x00 },
            { 0x01, 0x02, 0x00, 0x02, 0x02, 0x00, 0x00 },
            { 0x04, 0x02, 0x00, 0x02, 0x02, 0x00, 0x00 },
            { 0x02, 0x05, 0x00, 0x02, 0x02, 0x00, 0x00 },
            { 0x05, 0x00, 0x02, 0x02, 0x02, 0x00, 0x00 },
            { 0x01, 0x02, 0x02, 0x05, 0x02, 0x00, 0x00 },
            { 0x06, 0x01, 0x03, 0x05, 0x05, 0x00, 0x00 },
            { 0x01, 0x02, 0x02, 0x05, 0x02, 0x00, 0x00 },
            { 0x04, 0x02, 0x02, 0x05, 0x02, 0x00, 0x00 },
            { 0x07, 0x00, 0x02, 0x05, 0x02, 0x00, 0x00 },
            { 0x06, 0x03, 0x02, 0x05, 0x02, 0x00, 0x00 },
            { 0x05, 0x00, 0x02, 0x05, 0x02, 0x00, 0x00 },
            { 0x02, 0x00, 0x07, 0x00, 0x02, 0x00, 0x00 },
            { 0x00, 0x00, 0x06, 0x05, 0x03, 0x00, 0x00 },
            { 0x01, 0x02, 0x05, 0x05, 0x06, 0x00, 0x00 },
            { 0x04, 0x02, 0x05, 0x05, 0x06, 0x00, 0x00 },
            { 0x07, 0x00, 0x05, 0x05, 0x06, 0x00, 0x00 },
            { 0x05, 0x00, 0x05, 0x05, 0x06, 0x00, 0x00 },
            { 0x04, 0x02, 0x05, 0x05, 0x06, 0x02, 0x01 },
            { 0x01, 0x03, 0x05, 0x03, 0x01, 0x00, 0x00 },
            { 0x05, 0x00, 0x05, 0x05, 0x05, 0x02, 0x01 },
    };

    target->name = strdup("small");
    target->description = strdup("Very small characters");
    target->author = strdup("Stefan Misch");
    target->dataformat = 0;
    target->dispwidth = 4;
    target->dispheight = 7;

    memcpy(target->data,&font,sizeof(font));

    return(target);

};

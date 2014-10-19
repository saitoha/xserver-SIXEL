/*
 * Copyright © 2004 PillowElephantBadgerBankPond
 * Copyright © 2014 Sergii Pylypenko
 * Copyright © 2014 Hayaki Saito
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of PillowElephantBadgerBankPond not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.    PillowElephantBadgerBankPond makes no
 * representations about the suitability of this software for any purpose.    It
 * is provided "as is" without express or implied warranty.
 *
 * PillowElephantBadgerBankPond DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL PillowElephantBadgerBankPond BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * It's really not my fault - see it was the elephants!!
 *    - jaymz
 *
 */
#ifdef HAVE_CONFIG_H
#include "kdrive-config.h"
#endif
#include "kdrive.h"
#include <sixel.h>
#include <termios.h>
#include <X11/keysym.h>
#include <sys/wait.h>
#include <pthread.h>
#include <signal.h>

#define USE_DECMOUSE 1
#define USE_FILTER_RECTANGLE 1

static void xsixelFini(void);
static Bool sixelScreenInit(KdScreenInfo *screen);
static Bool sixelFinishInitScreen(ScreenPtr pScreen);
static Bool sixelCreateRes(ScreenPtr pScreen);
static Bool sixelMapFramebuffer(KdScreenInfo *screen);

static void sixelKeyboardFini(KdKeyboardInfo *ki);
static Status sixelKeyboardInit(KdKeyboardInfo *ki);
static Status sixelKeyboardEnable(KdKeyboardInfo *ki);
static void sixelKeyboardDisable(KdKeyboardInfo *ki);
static void sixelKeyboardLeds(KdKeyboardInfo *ki, int leds);
static void sixelKeyboardBell(KdKeyboardInfo *ki, int volume, int frequency, int duration);

static Bool sixelMouseInit(KdPointerInfo *pi);
static void sixelMouseFini(KdPointerInfo *pi);
static Status sixelMouseEnable(KdPointerInfo *pi);
static void sixelMouseDisable(KdPointerInfo *pi);

KdKeyboardInfo *sixelKeyboard = NULL;
KdPointerInfo *sixelPointer = NULL;

#if 0
#define DEBUG 1
#endif

#if DEBUG
#define TRACE(s) printf(s)
#define TRACE1(s, arg1) printf(s, arg1)
#define TRACE2(s, arg1, arg2) printf(s, arg1, arg2)
#define TRACE3(s, arg1, arg2, arg3) printf(s, arg1, arg2, arg3)
#define TRACE4(s, arg1, arg2, arg3, arg4) printf(s, arg1, arg2, arg3, arg4)
#define TRACE5(s, arg1, arg2, arg3, arg4, arg5) printf(s, arg1, arg2, arg3, arg4, arg5)
#else
#define TRACE(s)
#define TRACE1(s, arg1)
#define TRACE2(s, arg1, arg2)
#define TRACE3(s, arg1, arg2, arg3)
#define TRACE4(s, arg1, arg2, arg3, arg4)
#define TRACE5(s, arg1, arg2, arg3, arg4, arg5)
#endif

KdKeyboardDriver sixelKeyboardDriver = {
    .name = "keyboard",
    .Init = sixelKeyboardInit,
    .Fini = sixelKeyboardFini,
    .Enable = sixelKeyboardEnable,
    .Disable = sixelKeyboardDisable,
    .Leds = sixelKeyboardLeds,
    .Bell = sixelKeyboardBell,
};

KdPointerDriver sixelMouseDriver = {
    .name = "mouse",
    .Init = sixelMouseInit,
    .Fini = sixelMouseFini,
    .Enable = sixelMouseEnable,
    .Disable = sixelMouseDisable,
};


KdCardFuncs sixelFuncs = {
    .scrinit = sixelScreenInit,    /* scrinit */
    .finishInitScreen = sixelFinishInitScreen, /* finishInitScreen */
    .createRes = sixelCreateRes,    /* createRes */
};

int mouseState = 0;

enum { NUMRECTS = 32, FULLSCREEN_REFRESH_TIME = 1000 };

typedef struct
{
    int w;
    int h;
    int pitch;
    int pixel_w, pixel_h;
    int cell_w, cell_h;
    Rotation randr;
    Bool shadow;
    unsigned char *buffer;
    unsigned char *bitmap;
    sixel_dither_t *dither;
    sixel_output_t *output;
} SIXEL_Driver;

static SIXEL_Driver *g_driver = NULL;

#define SIXEL_UP                (1 << 12 | ('A' - '@'))
#define SIXEL_DOWN              (1 << 12 | ('B' - '@'))
#define SIXEL_RIGHT             (1 << 12 | ('C' - '@'))
#define SIXEL_LEFT              (1 << 12 | ('D' - '@'))
#define SIXEL_END               (1 << 12 | ('F' - '@'))
#define SIXEL_HOME              (1 << 12 | ('H' - '@'))
#define SIXEL_F1                (1 << 12 | ('P' - '@'))
#define SIXEL_F2                (1 << 12 | ('Q' - '@'))
#define SIXEL_F3                (1 << 12 | ('R' - '@'))
#define SIXEL_F4                (1 << 12 | ('S' - '@'))
#define SIXEL_FKEYS             (1 << 12 | ('~' - '@'))
#define SIXEL_MOUSE_SGR         (1 << 12 | ('<' - ';') << 4 << 6 | ('M' - '@'))
#define SIXEL_MOUSE_SGR_RELEASE (1 << 12 | ('<' - ';') << 4 << 6 | ('m' - '@'))
#define SIXEL_MOUSE_DEC         (1 << 12 | ('&' - 0x1f) << 6 | ('w' - '@'))
#define SIXEL_DTTERM_SEQS       (1 << 12 | ('t' - '@'))
#define SIXEL_UNKNOWN           (513)

typedef struct _key {
    int params[256];
    int nparams;
    int value;
} sixel_key_t;

enum _state {
    STATE_GROUND = 0,
    STATE_ESC = 1,
    STATE_CSI = 2,
    STATE_CSI_IGNORE = 3,
    STATE_CSI_PARAM = 4,
};

static int get_input(char *buf, int size) {
    fd_set fdset;
    struct timeval timeout;
    FD_ZERO(&fdset);
    FD_SET(STDIN_FILENO, &fdset);
    timeout.tv_sec = 0;
    timeout.tv_usec = 1;
    if (select(STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout) == 1)
        return read(STDIN_FILENO, buf, size);
    return 0;
}

static int getkeys(char *buf, int nread, sixel_key_t *keys)
{
    int i, c;
    int size = 0;
    static int state = STATE_GROUND;
    static int ibytes = 0;
    static int pbytes = 0;

    for (i = 0; i < nread; i++) {
        c = buf[i];
restart:
        switch (state) {
        case STATE_GROUND:
            switch (c) {
            case 0x1b:
                state = STATE_ESC;
                break;
            default:
                keys[size++].value = c;
                break;
            }
            break;
        case STATE_ESC:
            switch (c) {
            case 'O':
            case '[':
                keys[size].nparams = 0;
                pbytes = 0;
                state = STATE_CSI;
                break;
            default:
                keys[size++].value = 0x1b;
                state = STATE_GROUND;
                goto restart;
            }
            break;
        case STATE_CSI:
            switch (c) {
            case '\x1b':
                state = STATE_ESC;
                break;
            case '\x00'...'\x1a':
            case '\x1c'...'\x1f':
            case '\x7f':
                break;
            case ' '...'/':
                ibytes = c - ' ';
                pbytes = 0;
                state = STATE_CSI_PARAM;
                break;
            case '0'...'9':
                ibytes = 0;
                pbytes = c - '0';
                keys[size].nparams = 0;
                state = STATE_CSI_PARAM;
                break;
            case '<'...'?':
                ibytes = (c - ';') << 4;
                keys[size].nparams = 0;
                state = STATE_CSI_PARAM;
                break;
            case '@'...'~':
                keys[size].nparams = 0;
                keys[size++].value = 1 << 12 | (c - '@');
                state = STATE_GROUND;
                break;
            default:
                state = STATE_GROUND;
                break;
            }
            break;
        case STATE_CSI_PARAM:
            switch (c) {
            case '\x1b':
                state = STATE_ESC;
                break;
            case '\x00'...'\x1a':
            case '\x1c'...'\x1f':
            case '\x7f':
                break;
            case ' '...'/':
                ibytes |= c - 0x1f;
                state = STATE_CSI_PARAM;
                break;
            case '0'...'9':
                pbytes = pbytes * 10 + c - '0';
                state = STATE_CSI_PARAM;
                break;
            case ':'...';':
                if (keys[size].nparams < sizeof(keys[size].params) / sizeof(*keys[size].params)) {
                    keys[size].params[keys[size].nparams++] = pbytes;
                    pbytes = 0;
                }
                break;
            case '@'...'~':
                if (keys[size].nparams < sizeof(keys[size].params) / sizeof(*keys[size].params)) {
                    keys[size].params[keys[size].nparams++] = pbytes;
                    keys[size++].value = 1 << 12 | ibytes << 6  | c - '@';
                }
                state = STATE_GROUND;
                break;
            default:
                state = STATE_GROUND;
                break;
            }
            break;
        }
    }
    return size;
}



static int SendModifierKey(int state, uint8_t press_state)
{
    int posted = 0;

    if (state & 1) {
        KdEnqueueKeyboardEvent(sixelKeyboard, 42+8, press_state);
    }
    if (state & 2) {
        KdEnqueueKeyboardEvent(sixelKeyboard, 56+8, press_state);
    }
    if (state & 4) {
        KdEnqueueKeyboardEvent(sixelKeyboard, 29+8, press_state);
    }

    return posted;
}

static int GetScancode(int code)
{
    static u_char tbl[] = {
         0,  0,  0,  0,  0,  0,  0,  0, 14, 15, 28,  0,  0, 28,  0,  0, /* 0x00 - 0x0f */
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0, /* 0x10 - 0x1f */
        57,  2, 40,  4,  5,  6,  8, 40, 10, 11,  9, 13, 51, 12, 52, 53, /* 0x20 - 0x2f */
        11,  2,  3,  4,  5,  6,  7,  8,  9, 10, 39, 39, 51, 13, 52, 53, /* 0x30 - 0x3f */
         3, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, 49, 24, /* 0x40 - 0x4f */
        25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44, 26, 43, 27,  7, 12, /* 0x50 - 0x5f */
        41, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, 49, 24, /* 0x60 - 0x6f */
        25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44, 26, 43, 27, 41, 14, /* 0x70 - 0x7f */
    };

    if(code <= 0x7f && tbl[code] > 0) {
        return tbl[code]+8;
    } else {
        return 0;
    }
}


static int GetState(int code)
{
    if (code < 0x20) {
        if (GetScancode(code) == 0) {
            return 4;    /* Control */
        }
    }
    else if (code <= 0x7f) {
        static u_char tbl[] = {
            0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, /* 0x20 - 0x2f */
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1, /* 0x30 - 0x3f */
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 0x40 - 0x4f */
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, /* 0x50 - 0x5f */
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x60 - 0x6f */
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, /* 0x70 - 0x7f */
        };

        /* Shift */
        return tbl[code - 0x20];
    }
    return 0;
}

static int TranslateKey(int value)
{
    /* Set the keysym information */
    int scancode = GetScancode(value);

    if (scancode == 0 && value < 0x20) {
        /* It seems Ctrl+N key */
        scancode = GetScancode(value + 0x60);
    }
    return scancode;
}

struct termios orig_termios;

static void tty_raw(void)
{
    struct termios raw;

    if (tcgetattr(fileno(stdin), &orig_termios) < 0) {
        perror("can't set raw mode");
    }
    raw = orig_termios;
    raw.c_iflag &= ~(/*BRKINT |*/ ICRNL /*| INPCK | ISTRIP | IXON*/);
    raw.c_lflag &= ~(ECHO | ICANON /*| IEXTEN | ISIG*/);
    raw.c_lflag &= ~(OPOST);
    raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;
    raw.c_cc[VINTR] = 0; raw.c_cc[VKILL] = 0; raw.c_cc[VQUIT] = 0;
    raw.c_cc[VSTOP] = 0; raw.c_cc[VSUSP] = 0;
    if (tcsetattr(fileno(stdin), TCSAFLUSH, &raw) < 0) {
        perror("can't set raw mode");
    }
}

static void tty_restore(void)
{
    tcsetattr(fileno(stdin), TCSADRAIN, &orig_termios);
}

static int SIXEL_Flip(SIXEL_Driver *driver)
{
    int start_row = 1;
    int start_col = 1;

    memcpy(driver->bitmap, driver->buffer, driver->h * driver->w * 3);
    printf("\033[%d;%dH", start_row, start_col);
    sixel_encode(driver->bitmap, driver->w, driver->h, 3, driver->dither, driver->output);

    return 0;
}


static void SIXEL_UpdateRects(SIXEL_Driver *driver, int numrects, pixman_box16_t *rects)
{
    int start_row = 1, start_col = 1;
    int cell_height = 0, cell_width = 0;
    int i, y;
    unsigned char *src, *dst;
#if SIXEL_VIDEO_DEBUG
    static int frames = 0;
    char *format;
#endif

    if (driver->cell_h != 0 && driver->pixel_h != 0) {
        for (i = 0; i < numrects; ++i, ++rects) {
            start_row = 1;
            start_col = 1;
            cell_height = driver->pixel_h / driver->cell_h;
            cell_width = driver->pixel_w / driver->cell_w;
            start_row += rects->y1 / cell_height;
            start_col += rects->x1 / cell_width;
            rects->y1 = (start_row - 1) * cell_height;
            rects->x1 = (start_col - 1) * cell_width;
            rects->y2 = min((rects->y2 / cell_height + 1) * cell_height, driver->h);
            rects->x2 = min((rects->x2 / cell_width + 1) * cell_width, driver->w);
            if (rects->x1 == 0 && rects->x2 == driver->w) {
                dst = driver->bitmap;
                src = driver->buffer + rects->y1 * driver->w * 3;
                memcpy(dst, src, (rects->y2 - rects->y1) * driver->w * 3);
            } else {
                for (y = rects->y1; y < rects->y2; ++y) {
                    dst = driver->bitmap + (y - rects->y1) * (rects->x2 - rects->x1) * 3;
                    src = driver->buffer + y * driver->w * 3 + rects->x1 * 3;
                    memcpy(dst, src, (rects->x2 - rects->x1) * 3);
                }
            }
            printf("\033[%d;%dH", start_row, start_col);
            sixel_encode(driver->bitmap, (rects->x2 - rects->x1), (rects->y2 - rects->y1), 3, driver->dither, driver->output);
#if SIXEL_VIDEO_DEBUG
            format = "\033[100;1Hframes: %05d, x: %04d, y: %04d, w: %04d, h: %04d";
            printf(format, ++frames, rects->x1, rects->y2, rects->x2 - rects->x1, rects->y2 - rects->y1);
#endif
        }
    } else {
        SIXEL_Flip(driver);
    }
    fflush(stdout);
}


static Bool sixelMapFramebuffer(KdScreenInfo *screen)
{
    SIXEL_Driver *driver = screen->driver;
    KdPointerMatrix m;

    if (driver->randr != RR_Rotate_0)
        driver->shadow = TRUE;
    else
        driver->shadow = FALSE;

    KdComputePointerMatrix (&m, driver->randr, screen->width, screen->height);

    KdSetPointerMatrix (&m);

    screen->width = driver->w;
    screen->height = driver->h;

    TRACE2("%s: shadow %d\n", __func__, driver->shadow);

    if (driver->shadow)
    {
        if (!KdShadowFbAlloc (screen,
                              driver->randr & (RR_Rotate_90|RR_Rotate_270)))
            return FALSE;
    }
    else
    {
        screen->fb.byteStride = driver->pitch;
        screen->fb.pixelStride = driver->w;
        screen->fb.frameBuffer = (CARD8 *) (driver->buffer);
    }

    return TRUE;
}

static void
sixelSetScreenSizes(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    SIXEL_Driver *driver = screen->driver;

    if (driver->randr & (RR_Rotate_0|RR_Rotate_180))
    {
        pScreen->width = driver->w;
        pScreen->height = driver->h;
        pScreen->mmWidth = screen->width_mm;
        pScreen->mmHeight = screen->height_mm;
    }
    else
    {
        pScreen->width = driver->h;
        pScreen->height = driver->w;
        pScreen->mmWidth = screen->height_mm;
        pScreen->mmHeight = screen->width_mm;
    }
}

static Bool
sixelUnmapFramebuffer(KdScreenInfo *screen)
{
    KdShadowFbFree (screen);
    return TRUE;
}

static int sixel_write(char *data, int size, void *priv)
{
    return fwrite(data, 1, size, (FILE *)priv);
}

static Bool sixelScreenInit(KdScreenInfo *screen)
{
    SIXEL_Driver *driver;

    TRACE1("%s\n", __func__);

    if (!screen->width || !screen->height)
    {
        screen->width = 640;
        screen->height = 480;
    }
//    if (!screen->fb.depth) {
        screen->fb.depth = 24;
//    }

    driver = g_driver = calloc(1, sizeof(SIXEL_Driver));

    TRACE3("Attempting for %dx%d/%dbpp mode\n", screen->width, screen->height, screen->fb.depth);

    driver->output = sixel_output_create(sixel_write, stdout);
    driver->dither = sixel_dither_get(BUILTIN_XTERM256);

    driver->buffer = calloc(1, 3 * screen->width * screen->height);
    if (!driver->buffer) {
        printf("Couldn't allocate buffer for requested mode\n");
        return FALSE;
    }
    driver->bitmap = calloc(1, 3 * screen->width * screen->height);
    if (!driver->bitmap) {
        printf("Couldn't allocate buffer for requested mode\n");
        return FALSE;
    }

    /* Set up the new mode framebuffer */
    driver->w = screen->width;
    driver->h = screen->height;
    driver->pixel_w = 0;
    driver->pixel_h = 0;
    driver->cell_w = 0;
    driver->cell_h = 0;
    driver->pitch = screen->width * 3;

    printf("\033[14t" "\033[18t");

    driver->randr = screen->randr;
    screen->driver = driver;

    TRACE3("Set %dx%d/%dbpp mode\n", driver->w, driver->h, screen->fb.depth);

    screen->fb.visuals = (1 << 4);
    screen->fb.redMask = 0x0000ff;
    screen->fb.greenMask = 0x00ff00;
    screen->fb.blueMask = 0xff0000;
    screen->fb.bitsPerPixel = screen->fb.depth;
#if 0
    screen->fb.shadow = FALSE;
#endif
    screen->rate = 8;  /* 60 is too intense for CPU */

    printf("\033]1;Freedesktop.org X server on SIXEL\007", NULL);
    return sixelMapFramebuffer(screen);
}

static void sixelShadowUpdate(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    SIXEL_Driver *driver = screen->driver;
    pixman_box16_t * rects;
    int amount, i;
    int updateRectsPixelCount = 0;

    if (driver->shadow)
    {
        shadowUpdatePacked(pScreen, pBuf);
    }

    rects = pixman_region_rectangles(&pBuf->pDamage->damage, &amount);
    for (i = 0; i < amount; i++)
    {
        updateRectsPixelCount += (pBuf->pDamage->damage.extents.x2 - pBuf->pDamage->damage.extents.x1) *
                                 (pBuf->pDamage->damage.extents.y2 - pBuf->pDamage->damage.extents.y1);
    }
    /*
     * Each subrect is copied into temp buffer before uploading to OpenGL texture,
     * so if total area of pixels copied is more than 1/3 of the whole screen area,
     * there will be performance hit instead of optimization.
     */
    if (amount > NUMRECTS || updateRectsPixelCount * 3 > driver->w * driver->h) {
        SIXEL_Flip(driver);
    } else {
        SIXEL_UpdateRects(driver, amount, rects);
    }
}

static void *sixelShadowWindow(ScreenPtr pScreen, CARD32 row, CARD32 offset, int mode, CARD32 *size, void *closure)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    SIXEL_Driver *driver = screen->driver;

    if (!pScreenPriv->enabled) {
        return NULL;
    }

    *size = driver->pitch;

    TRACE1("%s\n", __func__);

    return (void *)((CARD8 *)driver->buffer + row * (*size) + offset);
}


static Bool sixelCreateRes(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    SIXEL_Driver *driver = screen->driver;
    Bool oldShadow = screen->fb.shadow;

    TRACE1("%s\n", __func__);

    /*
     * Hack: Kdrive assumes we have dumb videobuffer, which updates automatically,
     * and does not call update callback if shadow flag is not set.
     */
    screen->fb.shadow = TRUE;
    KdShadowSet(pScreen, driver->randr, sixelShadowUpdate, sixelShadowWindow);
    screen->fb.shadow = oldShadow;

    return TRUE;
}


#ifdef RANDR
static Bool sixelRandRGetInfo(ScreenPtr pScreen, Rotation *rotations)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    SIXEL_Driver *driver = screen->driver;
    RRScreenSizePtr pSize;
    Rotation randr;
    int n;

    TRACE1("%s", __func__);

    *rotations = RR_Rotate_All|RR_Reflect_All;

    for (n = 0; n < pScreen->numDepths; n++)
        if (pScreen->allowedDepths[n].numVids)
            break;
    if (n == pScreen->numDepths) {
        return FALSE;
    }

    pSize = RRRegisterSize(pScreen,
                           screen->width,
                           screen->height,
                           screen->width_mm,
                           screen->height_mm);

    randr = KdSubRotation(driver->randr, screen->randr);

    RRSetCurrentConfig(pScreen, randr, 0, pSize);

    return TRUE;
}

static Bool sixelRandRSetConfig(ScreenPtr pScreen,
                                Rotation randr,
                                int rate,
                                RRScreenSizePtr pSize)
{
    KdScreenPriv(pScreen);
    KdScreenInfo *screen = pScreenPriv->screen;
    SIXEL_Driver *driver = screen->driver;
    Bool wasEnabled = pScreenPriv->enabled;
    SIXEL_Driver oldDriver;
    int oldwidth;
    int oldheight;
    int oldmmwidth;
    int oldmmheight;

    if (wasEnabled) {
        KdDisableScreen (pScreen);
    }

    oldDriver = *driver;

    oldwidth = screen->width;
    oldheight = screen->height;
    oldmmwidth = pScreen->mmWidth;
    oldmmheight = pScreen->mmHeight;

    /*
     * Set new configuration
     */

    driver->randr = KdAddRotation(screen->randr, randr);

    TRACE2("%s driver->randr %d", __func__, driver->randr);

    sixelUnmapFramebuffer(screen);

    if (!sixelMapFramebuffer(screen)) {
        goto bail4;
    }

    KdShadowUnset(screen->pScreen);

    if (!sixelCreateRes(screen->pScreen)) {
        goto bail4;
    }

    sixelSetScreenSizes(screen->pScreen);

    /*
     * Set frame buffer mapping
     */
    (*pScreen->ModifyPixmapHeader)(fbGetScreenPixmap(pScreen),
                                   pScreen->width,
                                   pScreen->height,
                                   screen->fb.depth,
                                   screen->fb.bitsPerPixel,
                                   screen->fb.byteStride,
                                   screen->fb.frameBuffer);

    /* set the subpixel order */

    KdSetSubpixelOrder(pScreen, driver->randr);
    if (wasEnabled) {
        KdEnableScreen(pScreen);
    }

    return TRUE;

bail4:
    sixelUnmapFramebuffer(screen);
    *driver = oldDriver;
    (void) sixelMapFramebuffer(screen);
    pScreen->width = oldwidth;
    pScreen->height = oldheight;
    pScreen->mmWidth = oldmmwidth;
    pScreen->mmHeight = oldmmheight;

    if (wasEnabled) {
        KdEnableScreen (pScreen);
    }
    return FALSE;
}

static Bool sixelRandRInit(ScreenPtr pScreen)
{
    rrScrPrivPtr pScrPriv;

    TRACE1("%s", __func__);

    if (!RRScreenInit(pScreen)) {
        return FALSE;
    }

    pScrPriv = rrGetScrPriv(pScreen);
    pScrPriv->rrGetInfo = sixelRandRGetInfo;
    pScrPriv->rrSetConfig = sixelRandRSetConfig;
    return TRUE;
}
#endif


static Bool sixelFinishInitScreen(ScreenPtr pScreen)
{
    if (!shadowSetup(pScreen)) {
        return FALSE;
    }

#ifdef RANDR
    if (!sixelRandRInit(pScreen)) {
        return FALSE;
    }
#endif
    return TRUE;
}

static void sixelKeyboardFini(KdKeyboardInfo *ki)
{
    TRACE1("sixelKeyboardFini() %p\n", ki);
    sixelKeyboard = NULL;
}

static Status sixelKeyboardInit(KdKeyboardInfo *ki)
{
    ki->minScanCode = 8;
    ki->maxScanCode = 255;
    free(ki->name);
    free(ki->xkbRules);
    free(ki->xkbModel);
    free(ki->xkbLayout);
    ki->name = strdup("mlterm keyboard");
    ki->xkbRules = strdup("evdev");
    ki->xkbModel = strdup("pc105");
    ki->xkbLayout = strdup("us");
    sixelKeyboard = ki;
    TRACE1("sixelKeyboardInit() %p\n", ki);
    return Success;
}

static Status sixelKeyboardEnable(KdKeyboardInfo *ki)
{
    return Success;
}

static void sixelKeyboardDisable(KdKeyboardInfo *ki)
{
}

static void sixelKeyboardLeds(KdKeyboardInfo *ki, int leds)
{
}

static void sixelKeyboardBell(KdKeyboardInfo *ki, int volume, int frequency, int duration)
{
}

static Status sixelMouseInit(KdPointerInfo *pi)
{
    pi->nButtons = 7;
    pi->name = strdup("Android touchscreen and stylus");
    sixelPointer = pi;
    TRACE1("sixelMouseInit() %p\n", pi);
    return Success;
}

static void sixelMouseFini(KdPointerInfo *pi)
{
    TRACE1("sixelMouseFini() %p\n", pi);
    sixelPointer = NULL;
}

static Status sixelMouseEnable(KdPointerInfo *pi)
{
    /* TODO */
    return Success;
}

static void sixelMouseDisable(KdPointerInfo *pi)
{
    /* TODO */
    return;
}

void InitCard(char *name)
{
    KdCardInfoAdd(&sixelFuncs,  0);
    TRACE1("InitCard: %s\n", name);
}

void InitOutput(ScreenInfo *pScreenInfo, int argc, char **argv)
{
    KdInitOutput(pScreenInfo, argc, argv);
    signal(SIGHUP, SIG_DFL);
    TRACE("InitOutput()\n");
}

void InitInput(int argc, char **argv)
{
    KdPointerInfo *pi;
    KdKeyboardInfo *ki;

    KdAddKeyboardDriver(&sixelKeyboardDriver);
    KdAddPointerDriver(&sixelMouseDriver);

    ki = KdParseKeyboard("keyboard");
    KdAddKeyboard(ki);
    pi = KdParsePointer("mouse");
    KdAddPointer(pi);

    KdInitInput();
}

#ifdef DDXBEFORERESET
void ddxBeforeReset(void)
{
}
#endif

void ddxUseMsg(void)
{
    KdUseMsg();
}

int ddxProcessArgument(int argc, char **argv, int i)
{
    return KdProcessArgument(argc, argv, i);
}

static void sixelPollInput(void)
{
    int posted = 0;
    static int prev_x = -1, prev_y = -1;
    static int mouse_x = -1, mouse_y = -1;
    static int mouse_button = 0;
#if SIXEL_DEBUG
    static int events = 0;
#endif
    char buf[4096];
    static sixel_key_t keys[4096];
    int scancode;
    sixel_key_t *key;
    int nread, nkeys;
    int i;
    int state;

    nread = get_input(buf, sizeof(buf));
    if (nread > 0) {
        nkeys = getkeys(buf, nread, keys);
        if (nkeys >= sizeof(keys) / sizeof(*keys)) {
            nkeys = sizeof(keys) / sizeof(*keys) - 1;
        }
        for (i = 0; i < nkeys; ++i) {
            key = keys + i;
            switch (key->value) {
            case SIXEL_DTTERM_SEQS:
                if (g_driver) {
                    switch (key->params[0]) {
                    case 4:
                        g_driver->pixel_h = key->params[1];
                        g_driver->pixel_w = key->params[2];
                        break;
                    case 8:
                        g_driver->cell_h = key->params[1];
                        g_driver->cell_w = key->params[2];
                        break;
                    default:
                        break;
                    }
                }
                break;

            case SIXEL_MOUSE_DEC:
                if (key->nparams >= 4) {
                    mouse_y = key->params[2];
                    mouse_x = key->params[3];
                    switch (key->params[0]) {
                    case 1:
                        break;
                    case 2:
                        if (!(mouse_button & 1)) {
                            mouseState |= KD_BUTTON_1;
                            mouse_button |= 1;
                            KdEnqueuePointerEvent(sixelPointer, mouseState|KD_MOUSE_DELTA, 0, 0, 0);
                        }
                        break;
                    case 3:
                        //if (mouse_button & 1) {
                            mouseState &= ~KD_BUTTON_1;
                            mouse_button = 0;
                            KdEnqueuePointerEvent(sixelPointer, mouseState|KD_MOUSE_DELTA, 0, 0, 1);
                        //}
                        break;
                    case 4:
                        if (!(mouse_button & 2)) {
                            mouseState |= KD_BUTTON_2;
                            mouse_button |= 2;
                            KdEnqueuePointerEvent(sixelPointer, mouseState|KD_MOUSE_DELTA, 0, 0, 0);
                        }
                        break;
                    case 5:
                        //if (mouse_button & 2) {
                            mouseState &= ~KD_BUTTON_2;
                            mouse_button = 0;
                            KdEnqueuePointerEvent(sixelPointer, mouseState|KD_MOUSE_DELTA, 0, 0, 1);
                        //}
                        break;
                    case 6:
                        if (!(mouse_button & 4)) {
                            mouseState |= KD_BUTTON_3;
                            mouse_button |= 4;
                            KdEnqueuePointerEvent(sixelPointer, mouseState|KD_MOUSE_DELTA, 0, 0, 0);
                        }
                        break;
                    case 7:
                        //if (mouse_button & 4) {
                            mouseState &= ~KD_BUTTON_3;
                            mouse_button = 0;
                            KdEnqueuePointerEvent(sixelPointer, mouseState|KD_MOUSE_DELTA, 0, 0, 1);
                        //}
                        break;
                    case 10:
                        printf("\033[1;1'z" "\033[3'{" "\033[1'{");
                        printf("\033['w");
                        break;
                    case 8:
                    case 9:
                    case 32:
                    case 64:
                    default:
                        break;
                    }
                    mouse_button = key->params[1];
                }
#if USE_FILTER_RECTANGLE
#else
                printf("\033['|");
#endif
                fflush(stdout);
                break;

            case SIXEL_FKEYS:
                /* TODO: modifyFunctionKeys */
                switch (key->params[0]) {
                case 2:  scancode = 110+8; break;
                case 3:  scancode = 111+8; break;
                case 5:  scancode = 104+8; break;
                case 6:  scancode = 109+8; break;
                case 7:  scancode = 102+8; break;
                case 8:  scancode = 107+8; break;
                case 11: scancode =  59+8; break;
                case 12: scancode =  60+8; break;
                case 13: scancode =  61+8; break;
                case 14: scancode =  62+8; break;
                case 15: scancode =  63+8; break;
                case 17: scancode =  64+8; break;
                case 18: scancode =  65+8; break;
                case 19: scancode =  66+8; break;
                case 20: scancode =  67+8; break;
                case 21: scancode =  68+8; break;
                case 23: scancode =  87+8; break;
                case 24: scancode =  88+8; break;
                default:
                    scancode = 0;
                    break;
                }
                if (key->nparams == 2) {
                    key->params[1]--;
                    SendModifierKey(key->params[1], 0);
                }
                KdEnqueueKeyboardEvent(sixelKeyboard, scancode, 0);
                if (key->nparams == 2) {
                    SendModifierKey(key->params[1], 1);
                }
                KdEnqueueKeyboardEvent(sixelKeyboard, scancode, 1);
                break;
            default:
                if ((key->value >= SIXEL_UP && key->value <= SIXEL_LEFT) ||
                    (key->value >= SIXEL_END && key->value <= SIXEL_HOME) ||
                    (key->value >= SIXEL_F1 && key->value <= SIXEL_F4)) {
                    /* TODO: modifyCursorKeys, modifyOtherKeys */
                    switch(key->value) {
                    case SIXEL_UP:    scancode = 103+8; break;
                    case SIXEL_DOWN:  scancode = 108+8; break;
                    case SIXEL_RIGHT: scancode = 106+8; break;
                    case SIXEL_LEFT:  scancode = 105+8; break;
                    case SIXEL_HOME:  scancode = 102+8; break;
                    case SIXEL_END:   scancode = 107+8; break;
                    case SIXEL_F1:    scancode =  59+8; break;
                    case SIXEL_F2:    scancode =  60+8; break;
                    case SIXEL_F3:    scancode =  61+8; break;
                    case SIXEL_F4:    scancode =  62+8; break;
                    default:
                        scancode = 0;
                        break;
                    }
                    if (key->nparams >= 1) {
                        key->params[key->nparams-1]--;
                        posted += SendModifierKey(key->params[key->nparams-1], 0);
                    }
                    KdEnqueueKeyboardEvent(sixelKeyboard, scancode, 0);
                    if (key->nparams >= 1) {
                        posted += SendModifierKey(key->params[key->nparams-1], 1);
                    }
                    KdEnqueueKeyboardEvent(sixelKeyboard, scancode, 1);
                }
                else {
                    state = GetState(key->value);
                    scancode = TranslateKey(key->value);
                    if (state) {
                        SendModifierKey(state, 0);
                    }
                    KdEnqueueKeyboardEvent(sixelKeyboard, scancode, 0);
                    if (state) {
                        SendModifierKey(state, 1);
                    }
                    KdEnqueueKeyboardEvent(sixelKeyboard, scancode, 1);
                }
                break;
            }
        }
    }
    if (prev_x != mouse_x || prev_y != mouse_y) {
        KdEnqueuePointerEvent(sixelPointer, mouseState, mouse_x, mouse_y, 0);
        prev_x = mouse_x;
        prev_y = mouse_y;
    }
}

static int xsixelInit(void)
{
    tty_raw();
    printf("\033[H");
    printf("\033[?25l");
    printf("\033[>2p");
#if USE_DECMOUSE
    printf("\033[1;1'z" "\033[3'{" "\033[1'{");
#if USE_FILTER_RECTANGLE
    printf("\033['w");
#else
    printf("\033['|");
#endif

#else
    printf("\033[?1003h");
    printf("\033[?1006h");
#endif

    return 0;
}


static void xsixelFini(void)
{
    fd_set fdset;
    struct timeval timeout;
    char buf[4096];

    printf("\033\\");
    fflush(stdout);
#if USE_DECMOUSE
    printf("\033[0'z" "\033[2'{" "\033[4'{");
#else
    printf("\033[?1006l");
    printf("\033[?1003l");
#endif
    printf("\033[>0p");
    printf("\033[?25h");

    FD_ZERO(&fdset);
    FD_SET(STDIN_FILENO, &fdset);
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;

    if (select(STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout) == 1) {
        while (read(STDIN_FILENO, buf, sizeof(buf))) {
            ;
        }
    }
    tty_restore();

    if (g_driver) {
        sixel_dither_unref(g_driver->dither);
        sixel_output_unref(g_driver->output);
        free(g_driver);
    }
}

void CloseInput(void)
{
    KdCloseInput();
}

KdOsFuncs sixelOsFuncs = {
    .Init = xsixelInit,
    .Fini = xsixelFini,
    .pollEvents = sixelPollInput,
};

void OsVendorInit (void)
{
    KdOsInit (&sixelOsFuncs);
}


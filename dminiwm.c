/* dminiwm.c [ 0.4.4 ]
*
*  I started this from catwm 31/12/10
*  Permission is hereby granted, free of charge, to any person obtaining a
*  copy of this software and associated documentation files (the "Software"),
*  to deal in the Software without restriction, including without limitation
*  the rights to use, copy, modify, merge, publish, distribute, sublicense,
*  and/or sell copies of the Software, and to permit persons to whom the
*  Software is furnished to do so, subject to the following conditions:
*
*  The above copyright notice and this permission notice shall be included in
*  all copies or substantial portions of the Software.
*
*  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
*  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*  DEALINGS IN THE SOFTWARE.
*
*/

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
//#include <X11/XF86keysym.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLEANMASK(mask) (mask & ~(numlockmask | LockMask))
#define TABLENGTH(X)    (sizeof(X)/sizeof(*X))

typedef union {
    const char** com;
    const int i;
} Arg;

// Structs
typedef struct {
    unsigned int mod;
    KeySym keysym;
    void (*function)(const Arg arg);
    const Arg arg;
} key;

typedef struct client client;
struct client{
    // Prev and next client
    client *next, *prev;
    // The window
    Window win;
    unsigned int x, y, width, height, order;
};

typedef struct desktop desktop;
struct desktop{
    unsigned int master_size, mode, growth, numwins, nmaster, showbar;
    client *head,*current, *transient;
};

typedef struct {
    const char *class;
    unsigned int preferredd, followwin;
} Convenience;

typedef struct {
    const char *class;
    unsigned int x, y, width, height;
} Positional;

// Functions
static void add_window(Window w, unsigned int tw, client *cl);
static void buttonpress(XEvent *e);
static void buttonrelease(XEvent *e);
static void change_desktop(const Arg arg);
static void client_to_desktop(const Arg arg);
static void configurerequest(XEvent *e);
static void destroynotify(XEvent *e);
static void follow_client_to_desktop(const Arg arg);
static unsigned long getcolor(const char* color);
static void grabkeys();
static void keypress(XEvent *e);
static void kill_client();
static void kill_client_now(Window w);
static void last_desktop();
static void logger(const char* e);
static void maprequest(XEvent *e);
static void motionnotify(XEvent *e);
static void more_master(const Arg arg);
static void move_down(const Arg arg);
static void move_up(const Arg arg);
static void move_sideways(const Arg arg);
static void next_win();
static void prev_win();
static void quit();
static void remove_window(Window w, unsigned int dr, unsigned int tw);
static void resize_master(const Arg arg);
static void resize_stack(const Arg arg);
static void rotate_desktop(const Arg arg);
static void rotate_mode(const Arg arg);
static void save_desktop(unsigned int i);
static void select_desktop(unsigned int i);
static void setup();
static void sigchld(int unused);
static void spawn(const Arg arg);
static void start();
static void swap_master();
static void switch_mode(const Arg arg);
static void tile();
static void toggle_panel();
static void unmapnotify(XEvent *e);    // Thunderbird's write window just unmaps...
static void update_current();
static void update_info();
static void warp_pointer();

// Include configuration file (need struct key)
#include "config.h"

// Variable
static Display *dis;
static unsigned int bool_quit, current_desktop, previous_desktop, doresize;
static int growth, sh, sw, master_size, nmaster;
static unsigned int mode, panel_size, showbar, screen, bdw, numwins, win_focus, win_unfocus;
static int xerror(Display *dis, XErrorEvent *ee), (*xerrorxlib)(Display *, XErrorEvent *);
unsigned int numlockmask;		/* dynamic key lock mask */
static Window root;
static client *head, *current, *transient;
static XWindowAttributes attr;
static XButtonEvent starter;
static Atom *protocols, wm_delete_window, protos;

// Events array
static void (*events[LASTEvent])(XEvent *e) = {
    [KeyPress] = keypress,
    [MapRequest] = maprequest,
    [UnmapNotify] = unmapnotify,
    [ButtonPress] = buttonpress,
    [MotionNotify] = motionnotify,
    [ButtonRelease] = buttonrelease,
    [DestroyNotify] = destroynotify,
    [ConfigureRequest] = configurerequest
};

// Desktop array
static desktop desktops[DESKTOPS];

/* ***************************** Window Management ******************************* */
void add_window(Window w, unsigned int tw, client *cl) {
    client *c,*t, *dummy = head;

    if(cl != NULL) c = cl;
    else if(!(c = (client *)calloc(1,sizeof(client)))) {
        logger("\033[0;31mError calloc!");
        exit(1);
    }

    if(tw == 0 && cl == NULL) {
        XClassHint chh = {0};
        unsigned int i, j=0;
        if(XGetClassHint(dis, w, &chh)) {
            for(i=0;i<TABLENGTH(positional);++i)
                if(strcmp(chh.res_class, positional[i].class) == 0) {
                    XMoveResizeWindow(dis,w,positional[i].x,positional[i].y,positional[i].width,positional[i].height);
                    ++j;
                }
            if(chh.res_class) XFree(chh.res_class);
            if(chh.res_name) XFree(chh.res_name);
        } 
        if(j < 1) {
            XGetWindowAttributes(dis, w, &attr);
            XMoveWindow(dis, w,sw/2-(attr.width/2),sh/2-(attr.height/2)+panel_size);
        }
        XGetWindowAttributes(dis, w, &attr);
        c->x = attr.x;
        if(TOP_PANEL == 0 && attr.y < panel_size) c->y = panel_size;
        else c->y = attr.y;
        c->width = attr.width;
        c->height = attr.height;
    }

    c->win = w; c->order = 0;
    if(tw == 1) dummy = transient; // For the transient window
    for(t=dummy;t;t=t->next)
        ++t->order;

    if(dummy == NULL) {
        c->next = NULL; c->prev = NULL;
        dummy = c;
    } else {
        if(ATTACH_ASIDE == 0) {
            if(TOP_STACK == 0) {
                c->next = dummy->next; c->prev = dummy;
                dummy->next = c;
            } else {
                for(t=dummy;t->next;t=t->next); // Start at the last in the stack
                t->next = c; c->next = NULL;
                c->prev = t;
            }
        } else {
            c->prev = NULL; c->next = dummy;
            c->next->prev = c;
            dummy = c;
        }
    }

    if(tw == 1) {
        transient = dummy;
        save_desktop(current_desktop);
        return;
    } else head = dummy;
    current = c;
    numwins += 1;
    growth = (growth > 0) ? growth*(numwins-1)/numwins:0;
    save_desktop(current_desktop);
    // for folow mouse
    if(FOLLOW_MOUSE == 0) XSelectInput(dis, c->win, PointerMotionMask);
}

void remove_window(Window w, unsigned int dr, unsigned int tw) {
    client *c, *t, *dummy;

    dummy = (tw == 1) ? transient : head;
    for(c=dummy;c;c=c->next) {
        if(c->win == w) {
            if(c->prev == NULL && c->next == NULL) {
                dummy = NULL;
            } else if(c->prev == NULL) {
                dummy = c->next;
                c->next->prev = NULL;
            } else if(c->next == NULL) {
                c->prev->next = NULL;
            } else {
                c->prev->next = c->next;
                c->next->prev = c->prev;
            } break;
        }
    }
    if(tw == 1) {
        transient = dummy;
        free(c);
        save_desktop(current_desktop);
        update_current();
        return;
    } else {
        head = dummy;
        XUngrabButton(dis, AnyButton, AnyModifier, c->win);
        XUnmapWindow(dis, c->win);
        numwins -= 1;
        if(head != NULL) {
            for(t=head;t;t=t->next) {
                if(t->order > c->order) --t->order;
                if(t->order == 0) current = t;
            }
        } else current = NULL;
        if(dr == 0) free(c);
        if(numwins < 3) growth = 0;
        if(nmaster > 0 && nmaster == (numwins-1)) nmaster -= 1;
        save_desktop(current_desktop);
        if(mode != 4) tile();
        update_current();
        return;
    }
}

void next_win() {
    if(numwins < 2) return;
    current = (current->next == NULL) ? head:current->next;
    if(mode == 1) tile();
    update_current();
}

void prev_win() {
    if(numwins < 2) return;
    client *c;

    if(current->prev == NULL) for(c=head;c->next;c=c->next);
    else c = current->prev;

    current = c;
    if(mode == 1) tile();
    update_current();
}

void move_down(const Arg arg) {
    if(mode == 4 && current != NULL) {
        current->y += arg.i;
        XMoveResizeWindow(dis,current->win,current->x,current->y,current->width,current->height);
        return;
    }
    if(current == NULL || current->next == NULL || current->win == head->win || current->prev == NULL)
        return;
    Window tmp = current->win;
    current->win = current->next->win;
    current->next->win = tmp;
    //keep the moved window activated
    next_win();
    save_desktop(current_desktop);
    tile();
}

void move_up(const Arg arg) {
    if(mode == 4 && current != NULL) {
        current->y += arg.i;
        XMoveResizeWindow(dis,current->win,current->x,current->y,current->width,current->height);
        return;
    }
    if(current == NULL || current->prev == head || current->win == head->win)
        return;
    Window tmp = current->win;
    current->win = current->prev->win;
    current->prev->win = tmp;
    prev_win();
    save_desktop(current_desktop);
    tile();
}

void move_sideways(const Arg arg) {
    if(mode == 4 && current != NULL) {
        current->x += arg.i;
        XMoveResizeWindow(dis,current->win,current->x,current->y,current->width,current->height);
    }
}

void swap_master() {
    if(numwins < 3 || mode == 1 || mode == 4) return;
    Window tmp;

    if(current == head) {
        tmp = head->next->win; head->next->win = head->win;
        head->win = tmp;
    } else {
        tmp = head->win; head->win = current->win;
        current->win = tmp; current = head;
    }
    save_desktop(current_desktop);
    tile();
    update_current();
}

/* **************************** Desktop Management ************************************* */
void update_info() {
    if(OUTPUT_INFO != 0) return;
    unsigned int i, j;

    fflush(stdout);
    for(i=0;i<DESKTOPS;++i) {
        j = (i == current_desktop) ? 1 : 0;
        fprintf(stdout, "%d:%d:%d ", i, desktops[i].numwins, j);
    }
    fprintf(stdout, "%d\n", mode);
    fflush(stdout);
}

void change_desktop(const Arg arg) {
    if(arg.i == current_desktop) return;
    client *c; unsigned int tmp = current_desktop;

    // Save current "properties"
    save_desktop(current_desktop); previous_desktop = current_desktop;

    // Take "properties" from the new desktop
    select_desktop(arg.i);

    if((panel_size > 0 && showbar == 1) || (panel_size < 1 && showbar == 0)) toggle_panel();
    // Map all windows
    if(head != NULL) {
        if(mode != 1)
            for(c=head;c;c=c->next)
                XMapWindow(dis,c->win);
        tile();
    }
    if(transient != NULL)
        for(c=transient;c;c=c->next)
            XMapWindow(dis,c->win);

    select_desktop(tmp);
    // Unmap all window
    if(transient != NULL)
        for(c=transient;c;c=c->next)
            XUnmapWindow(dis,c->win);
    if(head != NULL)
        for(c=head;c;c=c->next)
            XUnmapWindow(dis,c->win);

    select_desktop(arg.i);
    update_current();
    update_info();
}

void last_desktop() {
    Arg a = {.i = previous_desktop};
    change_desktop(a);
}

void rotate_desktop(const Arg arg) {
    Arg a = {.i = (current_desktop + DESKTOPS + arg.i) % DESKTOPS};
     change_desktop(a);
}

void rotate_mode(const Arg arg) {
    // there's five hardcoded modes so use 5 in the math
    Arg a = {.i = (mode + 5 + arg.i) % 5};
     switch_mode(a);
}

void follow_client_to_desktop(const Arg arg) {
    client_to_desktop(arg);
    change_desktop(arg);
}

void client_to_desktop(const Arg arg) {
    if(arg.i == current_desktop || current == NULL) return;

    client *tmp = current;
    unsigned int tmp2 = current_desktop;

    // Remove client from current desktop
    remove_window(current->win, 1, 0);

    // Add client to desktop
    select_desktop(arg.i);
    add_window(tmp->win, 0, tmp);
    save_desktop(arg.i);
    select_desktop(tmp2);
    update_info();
}

void save_desktop(unsigned int i) {
    desktops[i].master_size = master_size;
    desktops[i].nmaster = nmaster;
    desktops[i].numwins = numwins;
    desktops[i].mode = mode;
    desktops[i].growth = growth;
    desktops[i].showbar = showbar;
    desktops[i].head = head;
    desktops[i].current = current;
    desktops[i].transient = transient;
}

void select_desktop(unsigned int i) {
    master_size = desktops[i].master_size;
    nmaster = desktops[i].nmaster;
    numwins = desktops[i].numwins;
    mode = desktops[i].mode;
    growth = desktops[i].growth;
    showbar = desktops[i].showbar;
    head = desktops[i].head;
    current = desktops[i].current;
    transient = desktops[i].transient;
    current_desktop = i;
}

void more_master (const Arg arg) {
    if(arg.i > 0) {
        if((numwins < 3) || (nmaster == (numwins-2))) return;
        nmaster += 1;
    } else {
        if(nmaster == 0) return;
        nmaster -= 1;
    }
    save_desktop(current_desktop);
    tile();
}

void tile() {
    if(head == NULL) return;
    client *c, *d = NULL;
    unsigned int x = 0, xpos = 0, ypos, wdt = 0, msw, ssw, ncols = 2, nrows = 1;
    int ht = 0, y = 0, n = 0;

    // For a top panel
    if(TOP_PANEL == 0) y = panel_size; ypos = y;

    // If only one window
    if(mode != 4 && head != NULL && head->next == NULL) {
        if(mode == 1) XMapWindow(dis, current->win);
        XMoveResizeWindow(dis,head->win,0,y,sw+bdw,sh+bdw);
    } else {
        switch(mode) {
            case 0: /* Vertical */
            	// Master window
            	if(nmaster < 1)
                    XMoveResizeWindow(dis,head->win,0,y,master_size - bdw,sh - bdw);
                else {
                    for(d=head;d;d=d->next) {
                        XMoveResizeWindow(dis,d->win,0,ypos,master_size - bdw,sh/(nmaster+1) - bdw);
                        if(x == nmaster) break;
                        ypos += sh/(nmaster+1); ++x;
                    }
                }

                // Stack
                if(d == NULL) d = head;
                n = numwins - (nmaster+1);
                XMoveResizeWindow(dis,d->next->win,master_size,y,sw-master_size-bdw,(sh/n)+growth - bdw);
                y += (sh/n)+growth;
                for(c=d->next->next;c;c=c->next) {
                    XMoveResizeWindow(dis,c->win,master_size,y,sw-master_size-bdw,(sh/n)-(growth/(n-1)) - bdw);
                    y += (sh/n)-(growth/(n-1));
                }
                break;
            case 1: /* Fullscreen */
                XMoveResizeWindow(dis,current->win,0,y,sw+bdw,sh+bdw);
                XMapWindow(dis, current->win);
                break;
            case 2: /* Horizontal */
            	// Master window
            	if(nmaster < 1)
                    XMoveResizeWindow(dis,head->win,xpos,ypos,sw-bdw,master_size-bdw);
                else {
                    for(d=head;d;d=d->next) {
                        XMoveResizeWindow(dis,d->win,xpos,ypos,sw/(nmaster+1)-bdw,master_size-bdw);
                        if(x == nmaster) break;
                        xpos += sw/(nmaster+1); ++x;
                    }
                }

                // Stack
                if(d == NULL) d = head;
                n = numwins - (nmaster+1);
                XMoveResizeWindow(dis,d->next->win,0,y+master_size,(sw/n)+growth-bdw,sh-master_size-bdw);
                msw = (sw/n)+growth;
                for(c=d->next->next;c;c=c->next) {
                    XMoveResizeWindow(dis,c->win,msw,y+master_size,(sw/n)-(growth/(n-1)) - bdw,sh-master_size-bdw);
                    msw += (sw/n)-(growth/(n-1));
                }
                break;
            case 3: // Grid
                x = numwins;
                for(xpos=0;xpos<=x;++xpos) {
                    if(xpos == 3 || xpos == 7 || xpos == 10 || xpos == 17) ++nrows;
                    if(xpos == 5 || xpos == 13 || xpos == 21) ++ncols;
                }
                msw = (ncols > 2) ? ((master_size*2)/ncols) : master_size;
                ssw = (sw - msw)/(ncols-1); ht = sh/nrows;
                xpos = msw+(ssw*(ncols-2)); ypos = y+((nrows-1)*ht);
                for(c=head;c->next;c=c->next);
                for(d=c;d;d=d->prev) {
                    --x;
                    if(n == nrows) {
                        xpos -= (xpos == msw) ? msw : ssw;
                        ypos = y+((nrows-1)*ht);
                        n = 0;
                    }
                    if(x == 0) {
                        ht = (ypos-y+ht);
                        ypos = y;
                    }
                    if(x == 2 && xpos == msw && ypos != y) {
                        ht -= growth;
                        ypos += growth;
                    }
                    if(x == 1) {
                        ht += growth;
                        ypos -= growth;
                    }
                    wdt = (xpos > 0) ? ssw : msw;
                    XMoveResizeWindow(dis,d->win,xpos,ypos,wdt-bdw,ht-bdw);
                    ht = sh/nrows;
                    ypos -= ht; ++n;
                }
                break;
            case 4: // Stacking
                for(c=head;c;c=c->next)
                    XMoveResizeWindow(dis,c->win,c->x,c->y,c->width,c->height);
                break;
        }
    }
}

void update_current() {
    if(head == NULL) return;
    client *c; unsigned int border;

    border = ((head->next == NULL && mode != 4) || (mode == 1)) ? 0 : bdw;
    for(c=head;c;c=c->next) {
        XSetWindowBorderWidth(dis,c->win,border);

        if(c != current) {
            if(c->order < current->order) ++c->order;
            XSetWindowBorder(dis,c->win,win_unfocus);
            if(CLICK_TO_FOCUS == 0)
                XGrabButton(dis, AnyButton, AnyModifier, c->win, True, ButtonPressMask|ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
        }
        else {
            // "Enable" current window
            XSetWindowBorder(dis,c->win,win_focus);
            XSetInputFocus(dis,c->win,RevertToParent,CurrentTime);
            XRaiseWindow(dis,c->win);
            if(CLICK_TO_FOCUS == 0)
                XUngrabButton(dis, AnyButton, AnyModifier, c->win);
        }
    }
    current->order = 0;
    if(transient != NULL) {
        XRaiseWindow(dis,transient->win);
    }
    warp_pointer();
    XSync(dis, False);
}

void switch_mode(const Arg arg) {
    if(mode == arg.i) return;
    client *c;

    growth = 0;
    if(mode == 1 && current != NULL && head->next != NULL) {
        XUnmapWindow(dis, current->win);
        for(c=head;c;c=c->next)
            XMapWindow(dis, c->win);
    }

    mode = arg.i;
    master_size = (mode == 2) ? sh*MASTER_SIZE : sw*MASTER_SIZE;
    if(mode == 1 && current != NULL && head->next != NULL)
        for(c=head;c;c=c->next)
            XUnmapWindow(dis, c->win);

    tile();
    update_current();
    update_info();
}

void resize_master(const Arg arg) {
    if(mode == 4 && current != NULL) {
        current->width += arg.i;
        XMoveResizeWindow(dis,current->win,current->x,current->y,current->width,current->height);
    } else if(arg.i > 0) {
        if((mode != 2 && sw-master_size > 70) || (mode == 2 && sh-master_size > 70))
            master_size += arg.i;
    } else if(master_size > 70) master_size += arg.i;
    tile();
}

void resize_stack(const Arg arg) {
    if(mode == 4 && current != NULL) {
        current->height += arg.i;
        XMoveResizeWindow(dis,current->win,current->x,current->y,current->width,current->height+arg.i);
    } else if(nmaster == (numwins-2)) return;
    else if(mode == 3) {
        if(arg.i > 0 && ((sh/2+growth) < (sh-100))) growth += arg.i;
        else if(arg.i < 0 && ((sh/2+growth) > 80)) growth += arg.i;
        tile();
    } else if(numwins > 2) {
        unsigned int n = numwins-1;
        if(arg.i >0) {
            if((mode != 2 && sh-(growth+sh/n) > (n-1)*70) || (mode == 2 && sw-(growth+sw/n) > (n-1)*70))
                growth += arg.i;
        } else {
            if((mode != 2 && (sh/n+growth) > 70) || (mode == 2 && (sw/n+growth) > 70))
                growth += arg.i;
        }
        tile();
    }
}

void toggle_panel() {
    if(PANEL_HEIGHT > 0) {
        if(panel_size >0) {
            sh += panel_size;
            panel_size = 0;
            showbar = 1;
        } else {
            panel_size = PANEL_HEIGHT;
            sh -= panel_size;
            showbar = 0;
        }
        tile();
    }
}

/* ********************** Keyboard Management ********************** */
void grabkeys() {
    unsigned int i,j;
    KeyCode code;

    // numlock workaround
    XModifierKeymap *modmap;
    numlockmask = 0;
    modmap = XGetModifierMapping(dis);
    for (i=0;i<8;++i) {
        for (j=0;j<modmap->max_keypermod;++j) {
            if(modmap->modifiermap[i * modmap->max_keypermod + j] == XKeysymToKeycode(dis, XK_Num_Lock))
                numlockmask = (1 << i);
        }
    }
    XFreeModifiermap(modmap);

    XUngrabKey(dis, AnyKey, AnyModifier, root);
    // For each shortcuts
    for(i=0;i<TABLENGTH(keys);++i) {
        code = XKeysymToKeycode(dis,keys[i].keysym);
        XGrabKey(dis, code, keys[i].mod, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dis, code, keys[i].mod | LockMask, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dis, code, keys[i].mod | numlockmask, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dis, code, keys[i].mod | numlockmask | LockMask, root, True, GrabModeAsync, GrabModeAsync);
    }
    for(i=1;i<4;i+=2) {
        XGrabButton(dis, i, RESIZEMOVEKEY, root, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
        XGrabButton(dis, i, RESIZEMOVEKEY | LockMask, root, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
        XGrabButton(dis, i, RESIZEMOVEKEY | numlockmask, root, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
        XGrabButton(dis, i, RESIZEMOVEKEY | numlockmask | LockMask, root, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
    }
}

void keypress(XEvent *e) {
    unsigned int i;
    KeySym keysym;
    XKeyEvent *ev = &e->xkey;

    keysym = XkbKeycodeToKeysym(dis, (KeyCode)ev->keycode, 0, 0);
    for(i=0;i<TABLENGTH(keys); ++i) {
        if(keysym == keys[i].keysym && CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)) {
            if(keys[i].function)
                keys[i].function(keys[i].arg);
        }
    }
}

void warp_pointer() {
    // Move cursor to the center of the current window
    if(FOLLOW_MOUSE == 0 && head != NULL) {
        XGetWindowAttributes(dis, current->win, &attr);
        XWarpPointer(dis, None, current->win, 0, 0, 0, 0, attr.width/2, attr.height/2);
    }
}

/* ********************** Signal Management ************************** */
void configurerequest(XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc;
    unsigned int y = (TOP_PANEL == 0) ? panel_size:0;

    wc.x = ev->x;
    wc.y = ev->y + y;
    wc.width = (ev->width < sw-bdw) ? ev->width:sw+bdw;
    wc.height = (ev->height < sh-bdw-y) ? ev->height:sh+bdw;
    wc.border_width = 0;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    XConfigureWindow(dis, ev->window, ev->value_mask, &wc);
    XSync(dis, False);
}

void maprequest(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;

    XGetWindowAttributes(dis, ev->window, &attr);
    if(attr.override_redirect) return;

    unsigned int y=0;
    if(TOP_PANEL == 0) y = panel_size;
    // For fullscreen mplayer (and maybe some other program)
    client *c;

    for(c=head;c;c=c->next)
        if(ev->window == c->win) {
            XMapWindow(dis,ev->window);
            return;
        }

   	Window trans = None;
    if (XGetTransientForHint(dis, ev->window, &trans) && trans != None) {
        add_window(ev->window, 1, NULL); 
        if((attr.y + attr.height) > sh)
            XMoveResizeWindow(dis,ev->window,attr.x,y,attr.width,attr.height-10);
        XSetWindowBorderWidth(dis,ev->window,bdw);
        XSetWindowBorder(dis,ev->window,win_focus);
        XMapWindow(dis, ev->window);
        update_current();
        return;
    }

    if(mode == 1 && current != NULL) XUnmapWindow(dis, current->win);
    XClassHint ch = {0};
    unsigned int i=0, j=0, tmp = current_desktop;
    if(XGetClassHint(dis, ev->window, &ch))
        for(i=0;i<TABLENGTH(convenience);++i)
            if(strcmp(ch.res_class, convenience[i].class) == 0) {
                save_desktop(tmp);
                select_desktop(convenience[i].preferredd-1);
                for(c=head;c;c=c->next)
                    if(ev->window == c->win)
                        ++j;
                if(j < 1) add_window(ev->window, 0, NULL);
                if(tmp == convenience[i].preferredd-1) {
                    tile();
                    XMapWindow(dis, ev->window);
                    update_current();
                } else select_desktop(tmp);
                if(convenience[i].followwin != 0) {
                    Arg a = {.i = convenience[i].preferredd-1};
                    change_desktop(a);
                }
                if(ch.res_class) XFree(ch.res_class);
                if(ch.res_name) XFree(ch.res_name);
                update_info();
                return;
            }
    if(ch.res_class) XFree(ch.res_class);
    if(ch.res_name) XFree(ch.res_name);

    add_window(ev->window, 0, NULL);
    if(mode != 4) tile();
    if(mode != 1) XMapWindow(dis,ev->window);
    update_current();
    update_info();
}

void destroynotify(XEvent *e) {
    unsigned int i = 0, tmp = current_desktop;
    client *c;
    XDestroyWindowEvent *ev = &e->xdestroywindow;

    save_desktop(tmp);
    for(i=current_desktop;i<current_desktop+DESKTOPS;++i) {
        select_desktop(i%DESKTOPS);
        for(c=head;c;c=c->next)
            if(ev->window == c->win) {
                remove_window(ev->window, 0, 0);
                select_desktop(tmp);
                update_info();
                return;
            }
        if(transient != NULL) {
            for(c=transient;c;c=c->next)
                if(ev->window == c->win) {
                    remove_window(ev->window, 0, 1);
                    select_desktop(tmp);
                    return;
                }
        }
    }
    select_desktop(tmp);
}

void unmapnotify(XEvent *e) { // for thunderbird's write window and maybe others
    XUnmapEvent *ev = &e->xunmap;
    client *c;

    if(ev->send_event == 1) {
        for(c=head;c;c=c->next)
            if(ev->window == c->win) {
                remove_window(ev->window, 1, 0);
                update_info();
                return;
            }
    }
}

void buttonpress(XEvent *e) {
    XButtonEvent *ev = &e->xbutton;
    client *c;

    for(c=transient;c;c=c->next)
        if(ev->window == c->win) {
            XSetInputFocus(dis,ev->window,RevertToParent,CurrentTime);
            return;
        }
    // change focus with LMB
    if(CLICK_TO_FOCUS == 0 && ev->window != current->win && ev->button == Button1)
        for(c=head;c;c=c->next)
            if(ev->window == c->win) {
                current = c;
                update_current();
                XSendEvent(dis, PointerWindow, False, 0xfff, e);
                XFlush(dis);
                return;
            }

    if(ev->subwindow == None || mode != 4) return;
    for(c=head;c;c=c->next)
        if(ev->subwindow == c->win) {
            XGrabPointer(dis, ev->subwindow, True, PointerMotionMask|ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
            XGetWindowAttributes(dis, ev->subwindow, &attr);
            starter = e->xbutton; doresize = 1;
        }
}

void motionnotify(XEvent *e) {
    int xdiff, ydiff;
    client *c;
    XMotionEvent *ev = &e->xmotion;

    if(ev->window != current->win) {
        for(c=head;c;c=c->next)
           if(ev->window == c->win) {
                current = c;
                update_current();
                return;
           }
    }
    if(doresize < 1) return;
    while(XCheckTypedEvent(dis, MotionNotify, e));
    xdiff = ev->x_root - starter.x_root;
    ydiff = ev->y_root - starter.y_root;
    XMoveResizeWindow(dis, ev->window, attr.x + (starter.button==1 ? xdiff : 0), attr.y + (starter.button==1 ? ydiff : 0),
        MAX(1, attr.width + (starter.button==3 ? xdiff : 0)),
        MAX(1, attr.height + (starter.button==3 ? ydiff : 0)));
}

void buttonrelease(XEvent *e) {
    XButtonEvent *ev = &e->xbutton;

    if(doresize < 1) {
        XSendEvent(dis, PointerWindow, False, 0xfff, e);
        XFlush(dis);
        return;
    }
    XUngrabPointer(dis, CurrentTime);
    if(mode != 4) return;
    if(ev->window == current->win) {
        XGetWindowAttributes(dis, current->win, &attr);
        current->x = attr.x;
        current->y = attr.y;
        current->width = attr.width;
        current->height = attr.height;
    }
    update_current();
    doresize = 0;
}

void kill_client() {
    if(head == NULL) return;
    kill_client_now(current->win);
    remove_window(current->win, 0, 0);
    update_info();
}

void kill_client_now(Window w) {
    int n, i;
    XEvent ke;

    if (XGetWMProtocols(dis, w, &protocols, &n) != 0) {
        for(i=n;i>=0;--i) {
            if (protocols[i] == wm_delete_window) {
                ke.type = ClientMessage;
                ke.xclient.window = w;
                ke.xclient.message_type = protos;
                ke.xclient.format = 32;
                ke.xclient.data.l[0] = wm_delete_window;
                ke.xclient.data.l[1] = CurrentTime;
                XSendEvent(dis, w, False, NoEventMask, &ke);
            }
        }
    } else XKillClient(dis, w);
    XFree(protocols);
}

void quit() {
    unsigned int i;
    client *c;

    logger("\033[0;34mYou Quit : Bye!");
    for(i=0;i<DESKTOPS;++i) {
        if(desktops[i].head != NULL) select_desktop(i);
        else continue;
        for(c=head;c;c=c->next)
            kill_client_now(c->win);
    }
    XClearWindow(dis, root);
    XUngrabKey(dis, AnyKey, AnyModifier, root);
    XSync(dis, False);
    XSetInputFocus(dis, root, RevertToPointerRoot, CurrentTime);
    bool_quit = 1;
}

unsigned long getcolor(const char* color) {
    XColor c;
    Colormap map = DefaultColormap(dis,screen);

    if(!XAllocNamedColor(dis,map,color,&c,&c))
        logger("\033[0;31mError parsing color!");

    return c.pixel;
}

void logger(const char* e) {
    fprintf(stderr,"\n\033[0;34m:: dminiwm : %s \033[0;m\n", e);
}

void setup() {
    unsigned int i;

    // Install a signal
    sigchld(0);

    // Screen and root window
    screen = DefaultScreen(dis);
    root = RootWindow(dis,screen);

    XSelectInput(dis,root,SubstructureRedirectMask);
    bdw = BORDER_WIDTH;
    panel_size = PANEL_HEIGHT;
    // Screen width and height
    sw = XDisplayWidth(dis,screen) - bdw;
    sh = XDisplayHeight(dis,screen) - (panel_size+bdw);

    // For having the panel shown at startup or not
    showbar = SHOW_BAR;
    if(showbar != 0) toggle_panel();

    // Colors
    win_focus = getcolor(FOCUS);
    win_unfocus = getcolor(UNFOCUS);

    // Shortcuts
    grabkeys();

    // Master size
    master_size = (mode == 2) ? sh*MASTER_SIZE : sw*MASTER_SIZE;

    // Set up all desktop
    for(i=0;i<TABLENGTH(desktops);++i) {
        desktops[i].master_size = master_size;
        desktops[i].nmaster = 0;
        desktops[i].mode = DEFAULT_MODE;
        desktops[i].growth = 0;
        desktops[i].showbar = showbar;
        desktops[i].numwins = 0;
        desktops[i].head = NULL;
        desktops[i].current = NULL;
        desktops[i].transient = NULL;
    }

    // Select first desktop by default
    select_desktop(0);
    wm_delete_window = XInternAtom(dis, "WM_DELETE_WINDOW", False);
    protos = XInternAtom(dis, "WM_PROTOCOLS", False);
    // To catch maprequest and destroynotify (if other wm running)
    XSelectInput(dis,root,SubstructureNotifyMask|SubstructureRedirectMask);
    // For exiting
    bool_quit = 0;
    logger("\033[0;32mWe're up and running!");
    update_info();
}

void sigchld(int unused) {
	if(signal(SIGCHLD, sigchld) == SIG_ERR) {
		logger("\033[0;31mCan't install SIGCHLD handler");
		exit(1);
        }
	while(0 < waitpid(-1, NULL, WNOHANG));
}

void spawn(const Arg arg) {
    if(fork() == 0) {
        if(fork() == 0) {
            if(dis) close(ConnectionNumber(dis));
            setsid();
            execvp((char*)arg.com[0],(char**)arg.com);
        }
        exit(0);
    }
}

/* There's no way to check accesses to destroyed windows, thus those cases are ignored (especially on UnmapNotify's).  Other types of errors call Xlibs default error handler, which may call exit.  */
int xerror(Display *dis, XErrorEvent *ee) {
    if(ee->error_code == BadWindow || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
        return 0;
    if(ee->error_code == BadAccess) {
        logger("\033[0;31mIs Another Window Manager Running? Exiting!");
        exit(1);
    } else logger("\033[0;31mBad Window Error!");
    return xerrorxlib(dis, ee); /* may call exit */
}

void start() {
    XEvent ev;

    while(!bool_quit && !XNextEvent(dis,&ev)) {
        if(events[ev.type])
            events[ev.type](&ev);
    }
}


int main() {
    // Open display
    if(!(dis = XOpenDisplay(NULL))) {
        logger("\033[0;31mCannot open display!");
        exit(1);
    }
    XSetErrorHandler(xerror);

    // Setup env
    setup();

    // Start wm
    start();

    // Close display
    XCloseDisplay(dis);

    exit(0);
}

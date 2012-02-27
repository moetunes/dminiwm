/* dminiwm.c [ 0.3.3 ]
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
#include <X11/keysym.h>
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
    client *next;
    client *prev;
    // The window
    Window win;
};

typedef struct desktop desktop;
struct desktop{
    int master_size;
    int mode, growth, numwins;
    client *head;
    client *current;
    client *transient;
};

typedef struct {
    const char *class;
    int preferredd;
    int followwin;
} Convenience;

// Functions
static void add_window(Window w, int tw);
static void buttonpressed(XEvent *e);
static void change_desktop(const Arg arg);
static void client_to_desktop(const Arg arg);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static void destroynotify(XEvent *e);
static void enternotify(XEvent *e);
static void follow_client_to_desktop(const Arg arg);
static void last_desktop();
static void logger(const char* e);
static unsigned long getcolor(const char* color);
static void grabkeys();
static void keypress(XEvent *e);
static void kill_client();
static void kill_client_now(Window w);
static void maprequest(XEvent *e);
static void move_down();
static void move_up();
static void next_win();
static void prev_win();
static void quit();
static void remove_window(Window w, int dr);
static void resize_master(const Arg arg);
static void resize_stack(const Arg arg);
static void rotate_desktop(const Arg arg);
static void save_desktop(int i);
static void select_desktop(int i);
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
static void warp_pointer();

// Include configuration file (need struct key)
#include "config.h"

// Variable
static Display *dis;
static int bool_quit;
static int current_desktop;
static int previous_desktop;
static int growth;
static int master_size;
static int mode;
static int panel_size;
static int sh;
static int sw;
static int screen;
static int xerror(Display *dis, XErrorEvent *ee);
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int win_focus;
static unsigned int win_unfocus;
unsigned int numlockmask;		/* dynamic key lock mask */
static Window root;
static client *head;
static client *current;
static client *transient;
static XWindowAttributes attr;

// Events array
static void (*events[LASTEvent])(XEvent *e) = {
    [KeyPress] = keypress,
    [MapRequest] = maprequest,
    [EnterNotify] = enternotify,
    [UnmapNotify] = unmapnotify,
    [ButtonPress] = buttonpressed,
    [DestroyNotify] = destroynotify,
    [ConfigureNotify] = configurenotify,
    [ConfigureRequest] = configurerequest
};

// Desktop array
static desktop desktops[DESKTOPS];

/* ***************************** Window Management ******************************* */
void add_window(Window w, int tw) {
    client *c,*t;

    if(!(c = (client *)calloc(1,sizeof(client)))) {
        logger("\0.3.3;31mError calloc!");
        exit(1);
    }

    if(tw == 1) { // For the transient window
        c->win = w;
        transient = c;
        save_desktop(current_desktop);
        return;
    }

    if(head == NULL) {
        c->next = NULL;
        c->prev = NULL;
        c->win = w;
        head = c;
    }
    else {
        if(ATTACH_ASIDE == 0) {
            for(t=head;t->next;t=t->next); // Start at the last in the stack
            c->next = NULL;
            c->prev = t;
            c->win = w;
            t->next = c;
        }
        else {
            t=head;
            c->prev = NULL;
            c->next = t;
            c->win = w;
            t->prev = c;
            head = c;
        }
    }

    current = c;
    desktops[current_desktop].numwins += 1;
    if(growth > 0) growth = growth*(desktops[current_desktop].numwins-1)/desktops[current_desktop].numwins;
    else growth = 0;
    save_desktop(current_desktop);
    // for folow mouse
    if(FOLLOW_MOUSE == 0) XSelectInput(dis, c->win, EnterWindowMask);
}

void remove_window(Window w, int dr) {
    client *c;

    if(transient != NULL && w == transient->win) {
        c = transient;
        transient = NULL;
        free(c);
        save_desktop(current_desktop);
        update_current();
        return;
    }

    // CHANGE THIS UGLY CODE
    for(c=head;c;c=c->next) {

        if(c->win == w) {
            if(desktops[current_desktop].numwins < 4) growth = 0;
            else growth = growth*(desktops[current_desktop].numwins-1)/desktops[current_desktop].numwins;
            desktops[current_desktop].numwins -= 1;
            if(c->prev == NULL && c->next == NULL) {
                free(head);
                head = NULL;
                current = NULL;
                save_desktop(current_desktop);
                return;
            }

            if(c->prev == NULL) {
                head = c->next;
                c->next->prev = NULL;
                current = c->next;
            }
            else if(c->next == NULL) {
                c->prev->next = NULL;
                current = c->prev;
            }
            else {
                c->prev->next = c->next;
                c->next->prev = c->prev;
                current = c->prev;
            }

            if(dr == 0) free(c);
            if(head->next == NULL && mode != 2) master_size = sw*MASTER_SIZE;
            if(head->next == NULL && mode == 2) master_size = sh*MASTER_SIZE;
            save_desktop(current_desktop);
            tile();
            update_current();
            if(desktops[current_desktop].numwins > 1) warp_pointer();
            return;
        }
    }
}

void next_win() {
    client *c;

    if(current != NULL && head != NULL) {
        if(current->next == NULL)
            c = head;
        else
            c = current->next;

        current = c;
        if(mode == 1)
            tile();
        update_current();
        warp_pointer();
    }
}

void prev_win() {
    client *c;

    if(current != NULL && head != NULL) {
        if(current->prev == NULL)
            for(c=head;c->next;c=c->next);
        else
            c = current->prev;

        current = c;
        if(mode == 1)
            tile();
        update_current();
        warp_pointer();
    }
}

void move_down() {
    Window tmp;
    if(current == NULL || current->next == NULL || current->win == head->win || current->prev == NULL)
        return;

    tmp = current->win;
    current->win = current->next->win;
    current->next->win = tmp;
    //keep the moved window activated
    next_win();
    save_desktop(current_desktop);
    tile();
}

void move_up() {
    Window tmp;
    if(current == NULL || current->prev == head || current->win == head->win) {
        return;
    }
    tmp = current->win;
    current->win = current->prev->win;
    current->prev->win = tmp;
    prev_win();
    save_desktop(current_desktop);
    tile();
}

void swap_master() {
    Window tmp;

    if(head->next != NULL && current != NULL && mode != 1) {
        if(current == head) {
            tmp = head->next->win;
            head->next->win = head->win;
            head->win = tmp;
        } else {
            tmp = head->win;
            head->win = current->win;
            current->win = tmp;
            current = head;
        }
        save_desktop(current_desktop);
        tile();
        update_current();
    }
}

/* **************************** Desktop Management ************************************* */
void change_desktop(const Arg arg) {
    client *c;

    if(arg.i == current_desktop)
        return;

    // Save current "properties"
    save_desktop(current_desktop);
    previous_desktop = current_desktop;

    // Unmap all window
    if(transient != NULL) XUnmapWindow(dis,transient->win);
    if(head != NULL)
        for(c=head;c;c=c->next)
            XUnmapWindow(dis,c->win);

    // Take "properties" from the new desktop
    select_desktop(arg.i);

    // Map all windows
    if(transient != NULL) XMapWindow(dis,transient->win);
    if(head != NULL) {
        if(mode != 1) {
            for(c=head;c;c=c->next)
                XMapWindow(dis,c->win);
        } else
            XMapWindow(dis, current->win);
    }

    tile();
    update_current();
    warp_pointer();
}

void last_desktop() {
    Arg a = {.i = previous_desktop};
    change_desktop(a);
}

void rotate_desktop(const Arg arg) {
    Arg a = {.i = (current_desktop + TABLENGTH(desktops) + arg.i) % TABLENGTH(desktops)};
     change_desktop(a);
}

void follow_client_to_desktop(const Arg arg) {
    client *tmp = current;
    int tmp2 = current_desktop;

    if(arg.i == current_desktop || current == NULL)
        return;

    // Add client to desktop
    select_desktop(arg.i);
    add_window(tmp->win, 0);
    save_desktop(arg.i);

    // Remove client from current desktop
    select_desktop(tmp2);
    XUnmapWindow(dis,tmp->win);
    remove_window(tmp->win, 0);
    save_desktop(tmp2);
    tile();
    update_current();
    change_desktop(arg);
}

void client_to_desktop(const Arg arg) {
    client *tmp = current;
    int tmp2 = current_desktop;

    if(arg.i == current_desktop || current == NULL)
        return;

    // Add client to desktop
    select_desktop(arg.i);
    add_window(tmp->win, 0);
    save_desktop(arg.i);

    // Remove client from current desktop
    select_desktop(tmp2);
    XUnmapWindow(dis,tmp->win);
    remove_window(tmp->win, 0);
    save_desktop(tmp2);
    tile();
    update_current();
}

void save_desktop(int i) {
    desktops[i].master_size = master_size;
    desktops[i].mode = mode;
    desktops[i].growth = growth;
    desktops[i].head = head;
    desktops[i].current = current;
    desktops[i].transient = transient;
}

void select_desktop(int i) {
    master_size = desktops[i].master_size;
    mode = desktops[i].mode;
    growth = desktops[i].growth;
    head = desktops[i].head;
    current = desktops[i].current;
    transient = desktops[i].transient;
    current_desktop = i;
}

void tile() {
    client *c;
    int n = 0;
    int x = 0;
    int y = 0;

    // For a top panel
    if(TOP_PANEL == 0) y = panel_size;

    // If only one window
    if(head != NULL && head->next == NULL) {
        if(mode == 1) XMapWindow(dis, current->win);
        XMoveResizeWindow(dis,head->win,0,y,sw+2*BORDER_WIDTH,sh+2*BORDER_WIDTH);
    }

    else if(head != NULL) {
        switch(mode) {
            case 0: /* Vertical */
            	// Master window
                XMoveResizeWindow(dis,head->win,0,y,master_size - BORDER_WIDTH,sh - BORDER_WIDTH);

                // Stack
                for(c=head->next;c;c=c->next) ++n;
                XMoveResizeWindow(dis,head->next->win,master_size + BORDER_WIDTH,y,sw-master_size-(2*BORDER_WIDTH),(sh/n)+growth - BORDER_WIDTH);
                y += (sh/n)+growth;
                for(c=head->next->next;c;c=c->next) {
                    XMoveResizeWindow(dis,c->win,master_size + BORDER_WIDTH,y,sw-master_size-(2*BORDER_WIDTH),(sh/n)-(growth/(n-1)) - BORDER_WIDTH);
                    y += (sh/n)-(growth/(n-1));
                }
                break;
            case 1: /* Fullscreen */
                XMapWindow(dis, current->win);
                XMoveResizeWindow(dis,current->win,0,y,sw+2*BORDER_WIDTH,sh+2*BORDER_WIDTH);
                break;
            case 2: /* Horizontal */
            	// Master window
                XMoveResizeWindow(dis,head->win,0,y,sw-BORDER_WIDTH,master_size - BORDER_WIDTH);

                // Stack
                for(c=head->next;c;c=c->next) ++n;
                XMoveResizeWindow(dis,head->next->win,0,y+master_size + BORDER_WIDTH,(sw/n)+growth-BORDER_WIDTH,sh-master_size-(2*BORDER_WIDTH));
                x = (sw/n)+growth;
                for(c=head->next->next;c;c=c->next) {
                    XMoveResizeWindow(dis,c->win,x,y+master_size + BORDER_WIDTH,(sw/n)-(growth/(n-1)) - BORDER_WIDTH,sh-master_size-(2*BORDER_WIDTH));
                    x += (sw/n)-(growth/(n-1));
                }
                break;
            case 3: { // Grid
                int xpos = 0;
                int wdt = 0;
                int ht = 0;

                for(c=head;c;c=c->next) ++x;

                for(c=head;c;c=c->next) {
                    ++n;
                    if(x >= 7) {
                        wdt = (sw/3) - BORDER_WIDTH;
                        ht  = (sh/3) - BORDER_WIDTH;
                        if((n == 1) || (n == 4) || (n == 7))
                            xpos = 0;
                        if((n == 2) || (n == 5) || (n == 8))
                            xpos = (sw/3) + BORDER_WIDTH;
                        if((n == 3) || (n == 6) || (n == 9))
                            xpos = (2*(sw/3)) + BORDER_WIDTH;
                        if((n == 4) || (n == 7))
                            y += (sh/3) + BORDER_WIDTH;
                        if((n == x) && (n == 7))
                            wdt = sw - BORDER_WIDTH;
                        if((n == x) && (n == 8))
                            wdt = 2*sw/3 - BORDER_WIDTH;
                    } else
                    if(x >= 5) {
                        wdt = (sw/3) - BORDER_WIDTH;
                        ht  = (sh/2) - BORDER_WIDTH;
                        if((n == 1) || (n == 4))
                            xpos = 0;
                        if((n == 2) || (n == 5))
                            xpos = (sw/3) + BORDER_WIDTH;
                        if((n == 3) || (n == 6))
                            xpos = (2*(sw/3)) + BORDER_WIDTH;
                        if(n == 4)
                            y += (sh/2); // + BORDER_WIDTH;
                        if((n == x) && (n == 5))
                            wdt = 2*sw/3 - BORDER_WIDTH;

                    } else {
                        if(x > 2) {
                            if((n == 1) || (n == 2))
                                ht = (sh/2) + growth - BORDER_WIDTH;
                            if(n >= 3)
                                ht = (sh/2) - growth - 2*BORDER_WIDTH;
                        }
                        else
                            ht = sh - BORDER_WIDTH;
                        if((n == 1) || (n == 3)) {
                            xpos = 0;
                            wdt = master_size - BORDER_WIDTH;
                        }
                        if((n == 2) || (n == 4)) {
                            xpos = master_size+BORDER_WIDTH;
                            wdt = (sw - master_size) - 2*BORDER_WIDTH;
                        }
                        if(n == 3)
                            y += (sh/2) + growth + BORDER_WIDTH;
                        if((n == x) && (n == 3))
                            wdt = sw - BORDER_WIDTH;
                    }
                    XMoveResizeWindow(dis,c->win,xpos,y,wdt,ht);
                }
            }
            break;
            default:
                break;
        }
    }
}

void update_current() {
    client *c;

    for(c=head;c;c=c->next) {
        if((head->next == NULL) || (mode == 1))
            XSetWindowBorderWidth(dis,c->win,0);
        else
            XSetWindowBorderWidth(dis,c->win,BORDER_WIDTH);

        if(current == c) {
            // "Enable" current window
            XSetWindowBorder(dis,c->win,win_focus);
            if(transient == NULL) XSetInputFocus(dis,c->win,RevertToParent,CurrentTime);
            XRaiseWindow(dis,c->win);
            if(CLICK_TO_FOCUS == 0)
                XUngrabButton(dis, AnyButton, AnyModifier, c->win);
        }
        else {
            XSetWindowBorder(dis,c->win,win_unfocus);
            if(CLICK_TO_FOCUS == 0)
                XGrabButton(dis, AnyButton, AnyModifier, c->win, True, ButtonPressMask|ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
        }
    }
    if(transient != NULL) {
        XSetInputFocus(dis,transient->win,RevertToParent,CurrentTime);
        XRaiseWindow(dis,transient->win);
    }
    XSync(dis, False);
}

void switch_mode(const Arg arg) {
    client *c;

    if(mode == arg.i) return;
    growth = 0;
    if(mode == 1 && current != NULL && head->next != NULL) {
        XUnmapWindow(dis, current->win);
        for(c=head;c;c=c->next)
            XMapWindow(dis, c->win);
    }

    mode = arg.i;
    if(mode == 0 || mode == 3) master_size = sw * MASTER_SIZE;
    if(mode == 1 && current != NULL && head->next != NULL)
        for(c=head;c;c=c->next)
            XUnmapWindow(dis, c->win);

    if(mode == 2) master_size = sh * MASTER_SIZE;
    tile();
    update_current();
}

void resize_master(const Arg arg) {
    if(arg.i > 0) {
        if((mode != 2 && sw-master_size > 70) || (mode == 2 && sh-master_size > 70)) {
            master_size += arg.i;
            tile();
        }
    } else {
        if(master_size > 70) {
            master_size += arg.i;
            tile();
        }
    }
}

void resize_stack(const Arg arg) {
    if(desktops[current_desktop].numwins > 2) {
        int n = desktops[current_desktop].numwins-1;
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
        } else {
            panel_size = PANEL_HEIGHT;
            sh -= panel_size;
        }
        tile();
    }
}

/* ********************** Keyboard Management ********************** */
void grabkeys() {
    int i;
    KeyCode code;

    XUngrabKey(dis, AnyKey, AnyModifier, root);
    // For each shortcuts
    for(i=0;i<TABLENGTH(keys);++i) {
        code = XKeysymToKeycode(dis,keys[i].keysym);
        XGrabKey(dis, code, keys[i].mod, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dis, code, keys[i].mod | LockMask, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dis, code, keys[i].mod | numlockmask, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dis, code, keys[i].mod | numlockmask | LockMask, root, True, GrabModeAsync, GrabModeAsync);
    }
}

void keypress(XEvent *e) {
    static unsigned int len = sizeof keys / sizeof keys[0];
    unsigned int i;
    KeySym keysym;
    XKeyEvent *ev = &e->xkey;

    keysym = XKeycodeToKeysym(dis, (KeyCode)ev->keycode, 0);
    for(i = 0; i < len; i++) {
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

void configurenotify(XEvent *e) {
    // Do nothing for the moment
}

/* ********************** Signal Management ************************** */
void configurerequest(XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc;
    int y = 0;

    wc.x = ev->x;
    if(TOP_PANEL == 0) y = panel_size;
    wc.y = ev->y + y;
    if(ev->width < sw-BORDER_WIDTH)
        wc.width = ev->width;
    else
        wc.width = sw+BORDER_WIDTH;
    if(ev->height < sh-BORDER_WIDTH)
        wc.height = ev->height;
    else
        wc.height = sh+BORDER_WIDTH;
    wc.border_width = ev->border_width;
    wc.sibling = ev->above;
    wc.stack_mode = ev->detail;
    XConfigureWindow(dis, ev->window, ev->value_mask, &wc);
    XSync(dis, False);
}

void maprequest(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;

    XGetWindowAttributes(dis, ev->window, &attr);
    if(attr.override_redirect) return;

    // For fullscreen mplayer (and maybe some other program)
    client *c;

    for(c=head;c;c=c->next)
        if(ev->window == c->win) {
            XMapWindow(dis,ev->window);
            XMoveResizeWindow(dis,c->win,0,0,sw,sh);
            return;
        }

   	Window trans = None;
    if (XGetTransientForHint(dis, ev->window, &trans) && trans != None) {
        add_window(ev->window, 1);
        XMapWindow(dis, ev->window);
        XSetWindowBorderWidth(dis,ev->window,BORDER_WIDTH);
        XSetWindowBorder(dis,ev->window,win_focus);
        update_current();
        return;
    }

    XClassHint ch = {0};
    static unsigned int len = sizeof convenience / sizeof convenience[0];
    int i=0, j=0;
    int tmp = current_desktop;
    if(XGetClassHint(dis, ev->window, &ch))
        for(i=0;i<len;i++)
            if(strcmp(ch.res_class, convenience[i].class) == 0) {
                save_desktop(tmp);
                select_desktop(convenience[i].preferredd-1);
                for(c=head;c;c=c->next)
                    if(ev->window == c->win)
                        ++j;
                if(j < 1) add_window(ev->window, 0);
                if(tmp == convenience[i].preferredd-1) {
                    XMapWindow(dis, ev->window);
                    tile();
                    update_current();
                } else {
                    select_desktop(tmp);
                }
                if(convenience[i].followwin != 0) {
                    Arg a = {.i = convenience[i].preferredd-1};
                    change_desktop(a);
                }
                if(ch.res_class)
                    XFree(ch.res_class);
                if(ch.res_name)
                    XFree(ch.res_name);
                return;
            }

    add_window(ev->window, 0);
    XMapWindow(dis,ev->window);
    tile();
    update_current();
    warp_pointer();
}

void destroynotify(XEvent *e) {
    int i = 0;
    int tmp = current_desktop;
    client *c;
    XDestroyWindowEvent *ev = &e->xdestroywindow;

    if(transient != NULL && ev->window == transient->win) {
        remove_window(ev->window, 0);
        return;
    }
    save_desktop(tmp);
    for(i=0;i<TABLENGTH(desktops);++i) {
        select_desktop(i);
        for(c=head;c;c=c->next)
            if(ev->window == c->win) {
                remove_window(ev->window, 0);
                select_desktop(tmp);
                return;
            }
    }
    select_desktop(tmp);
}

void enternotify(XEvent *e) {
    client *c;
    XCrossingEvent *ev = &e->xcrossing;

    if(FOLLOW_MOUSE == 0) {
        if((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
            return;
        if(transient != NULL) return;
        for(c=head;c;c=c->next)
           if(ev->window == c->win) {
                current = c;
                update_current();
                return;
       }
   }
}

void buttonpressed(XEvent *e) {
    client *c;
    XButtonPressedEvent *ev = &e->xbutton;

    // change focus with LMB
    if(CLICK_TO_FOCUS == 0 && ev->window != current->win && ev->button == Button1)
        for(c=head;c;c=c->next)
            if(ev->window == c->win) {
                current = c;
                update_current();
                return;
            }
}

void unmapnotify(XEvent *e) { // for thunderbird's write window and maybe others
    XUnmapEvent *ev = &e->xunmap;
    int i = 0;
    int tmp = current_desktop;
    client *c;

    if(ev->send_event == 1) {
        save_desktop(tmp);
        for(i=0;i<TABLENGTH(desktops);++i) {
            select_desktop(i);
            for(c=head;c;c=c->next)
                if(ev->window == c->win) {
                    remove_window(ev->window, 1);
                    select_desktop(tmp);
                    return;
                }
        }
        select_desktop(tmp);
    }
}

void kill_client() {
    if(head == NULL) return;
    kill_client_now(current->win);
    remove_window(current->win, 0);
}

void kill_client_now(Window w) {
    Atom *protocols;
    int n, i;
    int can_delete = 0;
    XEvent ke;
    Atom wm_delete_window;
    wm_delete_window = XInternAtom(dis, "WM_DELETE_WINDOW", False); 

    if (XGetWMProtocols(dis, w, &protocols, &n) != 0)
        for (i=0;i<n;i++)
            if (protocols[i] == wm_delete_window) can_delete = 1;

    if(can_delete == 1) {
        ke.type = ClientMessage;
        ke.xclient.window = w;
        ke.xclient.message_type = XInternAtom(dis, "WM_PROTOCOLS", True);
        ke.xclient.format = 32;
        ke.xclient.data.l[0] = XInternAtom(dis, "WM_DELETE_WINDOW", True);
        ke.xclient.data.l[1] = CurrentTime;
        XSendEvent(dis, w, False, NoEventMask, &ke);
    } else XKillClient(dis, w);
}

void quit() {
    logger("\0.3.3;34mYou Quit : Thanks for using!");
    XUngrabKey(dis, AnyKey, AnyModifier, root);
    XDestroySubwindows(dis, root);
    XSync(dis, False);
    bool_quit = 1;
}

unsigned long getcolor(const char* color) {
    XColor c;
    Colormap map = DefaultColormap(dis,screen);

    if(!XAllocNamedColor(dis,map,color,&c,&c)) {
        logger("\0.3.3;31mError parsing color!");
        exit(1);
    }
    return c.pixel;
}

void logger(const char* e) {
    fprintf(stderr,"\n\033[0;34m:: dminiwm : %s \033[0;m\n", e);
}

void setup() {
    // Install a signal
    sigchld(0);

    // Screen and root window
    screen = DefaultScreen(dis);
    root = RootWindow(dis,screen);

    // Screen width and height
    panel_size = PANEL_HEIGHT;
    sw = XDisplayWidth(dis,screen) - BORDER_WIDTH;
    sh = (XDisplayHeight(dis,screen) - panel_size) - BORDER_WIDTH;

    // For having the panel shown at startup or not
    if(SHOW_BAR > 0) toggle_panel();
    // Colors
    win_focus = getcolor(FOCUS);
    win_unfocus = getcolor(UNFOCUS);

    // numlock workaround
    int j, k;
    XModifierKeymap *modmap;
    numlockmask = 0;
    modmap = XGetModifierMapping(dis);
    for (k = 0; k < 8; k++) {
        for (j = 0; j < modmap->max_keypermod; j++) {
            if(modmap->modifiermap[k * modmap->max_keypermod + j] == XKeysymToKeycode(dis, XK_Num_Lock))
                numlockmask = (1 << k);
        }
    }
    XFreeModifiermap(modmap);

    // Shortcuts
    grabkeys();

    // Default stack
    mode = DEFAULT_MODE;

    // List of client
    head = NULL;
    current = NULL;

    // Master size
    if(mode == 2)
        master_size = sh*MASTER_SIZE;
    else
        master_size = sw*MASTER_SIZE;

    // Set up all desktop
    int i;
    for(i=0;i<TABLENGTH(desktops);++i) {
        desktops[i].master_size = master_size;
        desktops[i].mode = mode;
        desktops[i].growth = 0;
        desktops[i].numwins = 0;
        desktops[i].head = head;
        desktops[i].current = current;
    }

    // Select first dekstop by default
    const Arg arg = {.i = 0};
    current_desktop = arg.i;
    change_desktop(arg);
    // To catch maprequest and destroynotify (if other wm running)
    XSelectInput(dis,root,SubstructureNotifyMask|SubstructureRedirectMask);
    XSetErrorHandler(xerror);
    // For exiting
    bool_quit = 0;
    logger("\0.3.3;32mWe're up and running!");
}

void sigchld(int unused) {
	if(signal(SIGCHLD, sigchld) == SIG_ERR) {
		logger("\0.3.3;31mCan't install SIGCHLD handler");
		exit(1);
        }
	while(0 < waitpid(-1, NULL, WNOHANG));
}

void spawn(const Arg arg) {
    if(fork() == 0) {
        if(fork() == 0) {
            if(dis)
                close(ConnectionNumber(dis));

            setsid();
            execvp((char*)arg.com[0],(char**)arg.com);
        }
        exit(0);
    }
}

/* There's no way to check accesses to destroyed windows, thus those cases are ignored (especially on UnmapNotify's).  Other types of errors call Xlibs default error handler, which may call exit.  */
int xerror(Display *dis, XErrorEvent *ee) {
    if(ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
        return 0;
    logger("\0.3.3;31mBad Window Error!");
    return xerrorxlib(dis, ee); /* may call exit */
}

void start() {
    XEvent ev;

    while(!bool_quit && !XNextEvent(dis,&ev)) {
        if(events[ev.type])
            events[ev.type](&ev);
    }
}


int main(int argc, char **argv) {
    // Open display
    if(!(dis = XOpenDisplay(NULL))) {
        logger("\0.3.3;31mCannot open display!");
        exit(1);
    }

    // Setup env
    setup();

    // Start wm
    start();

    // Close display
    logger("\0.3.3;35m BYE");
    XCloseDisplay(dis);

    return 0;
}

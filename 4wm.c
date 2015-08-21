// see license for copyright and license 

#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <stdarg.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <pwd.h>
#include <X11/keysym.h>
#include <X11/Xresource.h>
#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_ewmh.h>

/* set this to 1 to enable debug prints */
#if 0
#  define DEBUG(x)      fputs(x, stderr);
#  define DEBUGP(x,...) fprintf(stderr, x, ##__VA_ARGS__);
#else
#  define DEBUG(x)      ;
#  define DEBUGP(x,...) ;
#endif

#define USAGE           "usage: 4wm [-h] [-v]"
#define XCB_MOVE_RESIZE XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT
#define XCB_MOVE        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y
#define XCB_RESIZE      XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT
#define CLEANMASK(mask) (mask & ~(numlockmask | XCB_MOD_MASK_LOCK))
#define LENGTH(x) (sizeof(x)/sizeof(*x))
#define BUTTONMASK      XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE
#define INRECT(X,Y,RX,RY,RW,RH) ((X) >= (RX) && (X) < (RX) + (RW) && (Y) >= (RY) && (Y) < (RY) + (RH))
#define ISFT(c)        (c->isfloating || c->istransient)

enum { RESIZE, MOVE };
enum { TILE, MONOCLE, VIDEO, FLOAT };
enum { TLEFT, TRIGHT, TBOTTOM, TTOP, TDIRECS };
enum { WM_PROTOCOLS, WM_DELETE_WINDOW, WM_COUNT };
enum { NET_SUPPORTED, NET_FULLSCREEN, NET_WM_STATE, NET_ACTIVE, NET_WM_NAME, NET_COUNT };

/* a client is a wrapper to a window that additionally
 * holds some properties for that window
 *
 * isurgent    - set when the window received an urgent hint
 * istransient - set when the window is transient
 * isfloating  - set when the window is floating
 *
 * istransient is separate from isfloating as floating window can be reset
 * to their tiling positions, while the transients will always be floating
 */
typedef struct client {
    struct client *next;            // the client after this one, or NULL if the current is the last client
    int x, y, w, h;                 // actual window size
    int xp, yp, wp, hp;             // percent of monitor, before adjustment (percent is a int from 0-100)
    bool istransient, isfloating;   // property flags
    xcb_window_t win;               // the window this client is representing
    char *title;
} client;

/* properties of each desktop
 * mode         - the desktop's tiling layout mode
 * gap          - the desktops gap size
 * direction    - the direction to tile
 * count        - the number of clients on that desktop
 * head         - the start of the client list
 * current      - the currently highlighted window
 * prevfocus    - the client that previously had focus
 * dead         - the start of the dead client list
 * showpanel    - the visibility status of the panel
 */
typedef struct {
    int mode, gap, direction, count;
    client *head, *current, *prevfocus;
    bool showpanel;
} desktop;

typedef struct monitor {
    xcb_randr_output_t id;  // id
    int x, y, w, h;         // size in pixels
    struct monitor *next;   // the next monitor after this one
    int curr_dtop;          // which desktop the monitor is displaying
    bool haspanel;          // does this monitor display a panel
} monitor;

//argument structure to be passed to function by config.h 
typedef struct {
    const char** com;                                                       // a command to run
    const int i;                                                            // an integer to indicate different states
    const int p;                                                            // represents a percentage for resizing
    void (*m)(int*, client*, client**, desktop*);                           // for the move client command
    void (*r)(const int, const int, client*, desktop*, monitor*);     // for the resize client command
    char **list;                                                            // list for menus
} Arg;

// a key struct represents a combination of
typedef struct {
    unsigned int mod;           // a modifier mask
    xcb_keysym_t keysym;        // and the key pressed
    void (*func)(const Arg *);  // the function to be triggered because of the above combo
    const Arg arg;              // the argument to the function
} Key;

// a button struct represents a combination of
typedef struct {
    unsigned int mask, button;  // a modifier mask and the mouse button pressed
    void (*func)(const Arg *);  // the function to be triggered because of the above combo
    const Arg arg;              // the argument to the function
} Button;

typedef struct Menu {
    char **list;                // list to hold the original list of commands
    struct Menu *next;          // next menu incase of multiple
    struct Menu_Entry *head;
} Menu;

typedef struct Menu_Entry {
    char *cmd[2];                               // cmd to be executed
    int x, y;                                   // w and h will be default or defined
    struct Menu_Entry *next, *b, *l, *r, *t;    // next and neighboring entries
    xcb_rectangle_t *rectangles;                // tiles to draw
} Menu_Entry;

typedef struct Xresources {
    unsigned int color[12];
    xcb_gcontext_t gc_color[12];
    xcb_gcontext_t font_gc[12];
} Xresources;

// COMMANDS
void change_desktop(const Arg *arg);
void changegap(const Arg *arg);
void client_to_desktop(const Arg *arg);
void killclient();
void launchmenu(const Arg *arg);
void moveclient(const Arg *arg);
void moveclientup(int *num, client *c, client **list, desktop *d);
void moveclientleft(int *num, client *c, client **list, desktop *d);
void moveclientdown(int *num, client *c, client **list, desktop *d);
void moveclientright(int *num, client *c, client **list, desktop *d);
void movefocus(const Arg *arg);
void mousemotion(const Arg *arg);
void next_win();
void prev_win();
void pulltofloat();
void pushtotiling();
void quit(const Arg *arg);
void resizeclient(const Arg *arg);
void resizeclientbottom(const int grow, const int size, client *c, desktop *d, monitor *m);
void resizeclientleft(const int grow, const int size, client *c, desktop *d, monitor *m);
void resizeclientright(const int grow, const int size, client *c, desktop *d, monitor *m);
void resizeclienttop(const int grow, const int size, client *c, desktop *d, monitor *m);
void rotate(const Arg *arg);
void rotate_filled(const Arg *arg);
void spawn(const Arg *arg);
void switch_mode(const Arg *arg);
void switch_direction(const Arg *arg);

#include "config.h"

#if PRETTY_PRINT
typedef struct pp_data {
    char *ws;
    char *mode;
    char *dir;
} pp_data;
#endif

client** clientstothebottom(client *w, desktop *d, bool samesize);
client** clientstotheleft(client *w, desktop *d, bool samesize);
client** clientstotheright(client *w, desktop *d, bool samesize);
client** clientstothetop(client *w, desktop *d, bool samesize);
Menu_Entry* createmenuentry(int x, int y, int w, int h, char *cmd);
void deletewindow(xcb_window_t w);
#if PRETTY_PRINT
void desktopinfo(void);
#endif
void focus(client *c, desktop *d, const monitor *m);
void* malloc_safe(size_t size);
client* prev_client(client *c, desktop *d);
void removeclient(client *c, desktop *d, const monitor *m, bool delete);
void removeclientfromlist(client *c, desktop *d);
void retile(desktop *d, const monitor *m);
void setclientborders(client *c, const desktop *d, const monitor *m);
int setuprandr(void);
void sigchld();
void text_draw (xcb_gcontext_t gc, xcb_window_t window, int16_t x1, int16_t y1, const char *label);
void tilenew(client *n, client *o, desktop *d, const monitor *m);
void tileremove(client *dead, desktop *d, const monitor *m);
void unmapnotify(xcb_generic_event_t *e);
#if PRETTY_PRINT
void updatedir();
void updatemode();
void updatetitle(client *c);
void updatews();
#endif
client *wintoclient(xcb_window_t w);
monitor *wintomon(xcb_window_t w);

// variables
bool running = true;
int randrbase, retval = 0, nmons = 0;
unsigned int numlockmask = 0, win_unfocus, win_focus, win_outer, win_urgent, win_flt;
xcb_connection_t *dis;
xcb_screen_t *screen;
xcb_atom_t wmatoms[WM_COUNT], netatoms[NET_COUNT];
static desktop desktops[DESKTOPS];
monitor *mons = NULL, *selmon = NULL;
xcb_ewmh_connection_t *ewmh;
#if MENU
Menu *menus = NULL;
Xresources xres;
#endif
#if PRETTY_PRINT
pid_t pid;
pp_data pp;
#endif

// events array on receival of a new event, call the appropriate function to handle it
void (*events[XCB_NO_OPERATION])(xcb_generic_event_t *e);

client** (*clientstothe[TDIRECS])(client *w, desktop *d, bool samesize) = {
    [TBOTTOM] = clientstothebottom, [TLEFT] = clientstotheleft, [TRIGHT] = clientstotheright,
    [TTOP] = clientstothetop,
};

bool NOBORDER(const desktop *d) {
    return (d->count == 1 || d->mode == MONOCLE || d->mode == VIDEO);
}

void SETWINDOWX(client *s, const desktop *d, const monitor *m) {
    s->x = m->x + (float)s->xp/100 * m->w + (s->xp == 0 ? d->gap : d->gap/2);
}

void SETWINDOWY(client *s, const desktop *d, const monitor *m) {
    s->y = m->y + (float)s->yp/100 * m->h + (s->yp == 0 ? d->gap : d->gap/2);
}

void SETWINDOWW(client *s, const desktop *d, const monitor *m) {
    if(NOBORDER(d))
        s->w = (float)s->wp/100 * m->w - //gap 
                    ((s->xp + s->wp) == 100 ? d->gap : d->gap/2) - (s->xp == 0 ? d->gap : d->gap/2);
    else
        s->w = (float)s->wp/100 * m->w - 2*BORDER_WIDTH - //gap
                    ((s->xp + s->wp) == 100 ? d->gap : d->gap/2) - (s->xp == 0 ? d->gap : d->gap/2);
}

void SETWINDOWH(client *s, const desktop *d, const monitor *m) {
    if(NOBORDER(d))
        s->h = (float)s->hp/100 * m->h - //gap
                    ((s->yp + s->hp) == 100 ? d->gap : d->gap/2) - (s->yp == 0 ? d->gap : d->gap/2);
    else
        s->h = (float)s->hp/100 * m->h - 2*BORDER_WIDTH - //gap
                    ((s->yp + s->hp) == 100 ? d->gap : d->gap/2) - (s->yp == 0 ? d->gap : d->gap/2);
}

void SETWINDOW(client *w, const desktop *d, const monitor *m) {
    SETWINDOWX(w, d, m);
    SETWINDOWY(w, d, m);
    SETWINDOWW(w, d, m);
    SETWINDOWH(w, d, m);
}

inline void xcb_move_resize(client *w, const desktop *d, const monitor *m) {
    DEBUGP("xcb_move_resize: x: %d, y: %d, w: %d, h: %d\n", w->x, w->y, w->w, w->h);
    unsigned int pos[4] = { w->x, w->y, w->w, w->h };
    setclientborders(w, d, m);
    xcb_configure_window(dis, w->win, XCB_MOVE_RESIZE, pos);
}

inline void xcb_move_resize_monocle(client *w, const desktop *d, const monitor *m) {
    DEBUG("xcb_move_resize_monocle\n");
    unsigned int pos[4] = { d->mode == VIDEO ? m->x : (m->x + d->gap), 
                            d->mode == VIDEO ? (m->y - ((m->haspanel && TOP_PANEL) ? PANEL_HEIGHT:0)) : (m->y + d->gap), 
                            d->mode == VIDEO ? m->w : (m->w - 2*d->gap), 
                            d->mode == VIDEO ? (m->h + ((m->haspanel && !TOP_PANEL) ? PANEL_HEIGHT:0)) : (m->h - 2*d->gap)};
    setclientborders(w, d, m);
    xcb_configure_window(dis, w->win, XCB_MOVE_RESIZE, pos);
}

// wrapper to move window
inline void xcb_move(xcb_window_t win, int x, int y) {
    unsigned int pos[2] = { x, y };
    xcb_configure_window(dis, win, XCB_MOVE, pos);
}

// wrapper to resize window
inline void xcb_resize(xcb_window_t win, int w, int h) {
    unsigned int pos[2] = { w, h };
    xcb_configure_window(dis, win, XCB_RESIZE, pos);
}

// wrapper to lower window
inline void xcb_lower_window(xcb_window_t win) {
    unsigned int arg[1] = { XCB_STACK_MODE_BELOW };
    xcb_configure_window(dis, win, XCB_CONFIG_WINDOW_STACK_MODE, arg);
}

// wrapper to raise window
inline void xcb_raise_window(xcb_window_t win) {
    unsigned int arg[1] = { XCB_STACK_MODE_ABOVE };
    xcb_configure_window(dis, win, XCB_CONFIG_WINDOW_STACK_MODE, arg);
}

// wrapper to get atoms using xcb
void xcb_get_atoms(char **names, xcb_atom_t *atoms, unsigned int count) {
    xcb_intern_atom_cookie_t cookies[count];
    xcb_intern_atom_reply_t  *reply;

    for (unsigned int i = 0; i < count; i++) cookies[i] = xcb_intern_atom(dis, 0, strlen(names[i]), names[i]);
    for (unsigned int i = 0; i < count; i++) {
        reply = xcb_intern_atom_reply(dis, cookies[i], NULL); // TODO: Handle error
        if (reply) {
            DEBUGP("%s : %d\n", names[i], reply->atom);
            atoms[i] = reply->atom; free(reply);
        } else puts("WARN: 4wm failed to register %s atom.\nThings might not work right.");
    }
}

// wrapper to window get attributes using xcb */
void xcb_get_attributes(xcb_window_t *windows, xcb_get_window_attributes_reply_t **reply, unsigned int count) {
    xcb_get_window_attributes_cookie_t cookies[count];
    for (unsigned int i = 0; i < count; i++) cookies[i] = xcb_get_window_attributes(dis, windows[i]);
    for (unsigned int i = 0; i < count; i++) reply[i]   = xcb_get_window_attributes_reply(dis, cookies[i], NULL); // TODO: Handle error
}

// retieve RGB color from hex (think of html)
unsigned int xcb_get_colorpixel(char *hex) {
    char strgroups[3][3]  = {{hex[1], hex[2], '\0'}, {hex[3], hex[4], '\0'}, {hex[5], hex[6], '\0'}};
    unsigned int rgb16[3] = {(strtol(strgroups[0], NULL, 16)), (strtol(strgroups[1], NULL, 16)), (strtol(strgroups[2], NULL, 16))};
    return (rgb16[0] << 16) + (rgb16[1] << 8) + rgb16[2];
}

// wrapper to get xcb keycodes from keysymbol
xcb_keycode_t* xcb_get_keycodes(xcb_keysym_t keysym) {
    xcb_key_symbols_t *keysyms;
    xcb_keycode_t     *keycode;

    if (!(keysyms = xcb_key_symbols_alloc(dis))) return NULL;
        keycode = xcb_key_symbols_get_keycode(keysyms, keysym);
    xcb_key_symbols_free(keysyms);

    return keycode;
}

// wrapper to get xcb keysymbol from keycode
xcb_keysym_t xcb_get_keysym(xcb_keycode_t keycode) {
    xcb_key_symbols_t *keysyms;
    xcb_keysym_t       keysym;

    if (!(keysyms = xcb_key_symbols_alloc(dis))) return 0;
    keysym = xcb_key_symbols_get_keysym(keysyms, keycode, 0);
    xcb_key_symbols_free(keysyms);

    return keysym;
}

// get screen of display
xcb_screen_t *xcb_screen_of_display(xcb_connection_t *con, int screen) {
    xcb_screen_iterator_t iter;
    iter = xcb_setup_roots_iterator(xcb_get_setup(con));
    for (; iter.rem; --screen, xcb_screen_next(&iter)) if (screen == 0) return iter.data;
    return NULL;
}

// check if other wm exists
int xcb_checkotherwm(void) {
    xcb_generic_error_t *error;
    unsigned int values[1] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT|XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY|
                              XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_BUTTON_PRESS};
    error = xcb_request_check(dis, xcb_change_window_attributes_checked(dis, screen->root, XCB_CW_EVENT_MASK, values));
    xcb_flush(dis);
    if (error) return 1;
    return 0;
}

void growbyh(client *match, const int size, client *c, desktop *d, const monitor *m) {
    c->hp = match ? (match->yp - c->yp):(c->hp + size);
    SETWINDOW(c, d, m);
    xcb_move_resize(c, d, m);
}

void growbyw(client *match, const int size, client *c, desktop *d, const monitor *m) {
    c->wp = match ? (match->xp - c->xp):(c->wp + size);
    SETWINDOW(c, d, m);
    xcb_move_resize(c, d, m);
}

void growbyx(client *match, const int size, client *c, desktop *d, const monitor *m) {
    c->wp = match ? ((c->xp + c->wp) - (match->xp + match->wp)):(c->wp + size);
    c->xp = match ? (match->xp + match->wp):(c->xp - size);
    SETWINDOW(c, d, m);
    xcb_move_resize(c, d, m);
}

void growbyy(client *match, const int size, client *c, desktop *d, const monitor *m) {
    c->hp = match ? ((c->yp + c->hp) - (match->yp + match->hp)):(c->hp + size);
    c->yp = match ? (match->yp + match->hp):(c->yp - size);
    SETWINDOW(c, d, m);
    xcb_move_resize(c, d, m);
}

void shrinkbyh(client *match, const int size, client *c, desktop *d, const monitor *m) {
    c->hp = match ? (match->yp - c->yp):(c->hp - size);
    SETWINDOW(c, d, m);
    xcb_move_resize(c, d, m);
}

void shrinkbyw(client *match, const int size, client *c, desktop *d, const monitor *m) {
    c->wp = match ? (match->xp - c->xp):(c->wp - size);
    SETWINDOW(c, d, m);
    xcb_move_resize(c, d, m);
}

void shrinkbyx(client *match, const int size, client *c, desktop *d, const monitor *m) {
    c->wp = match ? ((c->xp + c->wp) - (match->xp + match->wp)):(c->wp - size);
    c->xp = match ? (match->xp + match->wp):(c->xp + size);
    SETWINDOW(c, d, m);
    xcb_move_resize(c, d, m);
}

void shrinkbyy(client *match, const int size, client *c, desktop *d, const monitor *m) {
    c->hp = match ? ((c->yp + c->hp) - (match->yp + match->hp)):(c->hp - size);
    c->yp = match ? (match->yp + match->hp):(c->yp + size);
    SETWINDOW(c, d, m);
    xcb_move_resize(c, d, m);
}


void addclienttolist(client *c, desktop *d) {
    client *p;
    for(p = d->head; p && p->next; p = p->next);
    if(!p)
        d->head = d->current = c;
    else {
        p->next = c;
        d->prevfocus = d->current;
        d->current = c;
    }
    c->next = NULL;
}

// create a new client and add the new window
// window should notify of property change events
client* addwindow(xcb_window_t w, desktop *d) {
    client *c;
    if (!(c = (client *)malloc_safe(sizeof(client)))) 
        err(EXIT_FAILURE, "cannot allocate client");

    addclienttolist(c, d);

    DEBUGP("addwindow: d->count = %d\n", d->count);

    unsigned int values[1] = { XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_ENTER_WINDOW };
    xcb_change_window_attributes_checked(dis, (c->win = w), XCB_CW_EVENT_MASK, values);
    return c;
}

// on the press of a button check to see if there's a binded function to call 
// TODO: if we make the mouse able to switch monitors we could eliminate a call
//       to wintomon
void buttonpress(xcb_generic_event_t *e) {
    xcb_button_press_event_t *ev = (xcb_button_press_event_t*)e; 
    monitor *m = wintomon(ev->event);
    client *c = wintoclient(ev->event);

    #if CLICK_TO_FOCUS 
    if (ev->detail == XCB_BUTTON_INDEX_1) {
        if (m && m != selmon) {
            monitor *mold = selmon;
            client *cold = desktops[selmon->curr_dtop].current;
            selmon = m;
            #if PRETTY_PRINT
            updatews();
            updatemode();
            updatedir();
            desktopinfo();
            #endif
            if (cold)
                setclientborders(cold, &desktops[mold->curr_dtop], mold);
        }
     
        if (c && c != desktops[m->curr_dtop].current) {
            desktops[m->curr_dtop].prevfocus = desktops[m->curr_dtop].current;
            desktops[m->curr_dtop].current = c;
            focus(c, &desktops[m->curr_dtop], m);
        }
    }
    #endif
    
    for (unsigned int i=0; i<LENGTH(buttons); i++)
        if (buttons[i].func && buttons[i].button == ev->detail && CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state)) {
            if (desktops[m->curr_dtop].current != c) focus(c, &desktops[m->curr_dtop], m);
            buttons[i].func(&(buttons[i].arg));
        }

    #if CLICK_TO_FOCUS
    xcb_allow_events(dis, XCB_ALLOW_REPLAY_POINTER, ev->time);
    xcb_flush(dis);
    #endif
}

// focus another desktop
//
// to avoid flickering
// first map the new windows
// first the current window and then all other
// then unmap the old windows
// first all others then the current
void change_desktop(const Arg *arg) {  
    int i;

    if (arg->i == selmon->curr_dtop || arg->i < 0 || arg->i >= DESKTOPS) { 
        DEBUG("change_desktop: not a valid desktop or the same desktop\n");
        return;
    }
 
    bool flag = false; monitor *m = NULL;
    for (i = 0, m = mons; i < nmons; m = m->next, i++)
        if ((m->curr_dtop == arg->i) && m != selmon) {
            m->curr_dtop = selmon->curr_dtop;
            flag = true;
            break;
        }

    desktop *d = &desktops[(selmon->curr_dtop)], *n = &desktops[(selmon->curr_dtop = arg->i)];
    
    if (flag) { // desktop exists on another monitor
        DEBUG("change_desktop: tiling current monitor, new desktop\n");
        retile(n, selmon);
        
        DEBUG("change_desktop: tiling other monitor, old desktop\n");
        retile(d, m);
    }
    else { 
        DEBUG("change_desktop: retiling new windows on current monitor\n");
        retile(n, selmon);
        DEBUG("change_desktop: mapping new windows on current monitor\n"); 
        if (n->current)
            xcb_map_window(dis, n->current->win);
        for (client *c = n->head; c; c = c->next)
            xcb_map_window(dis, c->win);
 
        DEBUG("change_desktop: unmapping old windows on current monitor\n");
        for (client *c = d->head; c; c = c->next) 
            if (c != d->current)
                xcb_unmap_window(dis, c->win);
        if (d->current)
            xcb_unmap_window(dis, d->current->win); 
    } 
  
    if(n->current)
        focus(n->current, n, selmon);
    else
        xcb_set_input_focus(dis, XCB_INPUT_FOCUS_POINTER_ROOT, screen->root, XCB_CURRENT_TIME);

    #if PRETTY_PRINT
    updatews();
    updatemode();
    updatedir();
    desktopinfo();
    #endif
}

// change
void changegap(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    if(d->gap + arg->i > 0) {
        d->gap += arg->i;
        retile(d, selmon);
    }
}

// remove all windows in all desktops by sending a delete message
void cleanup(void) {
    xcb_query_tree_reply_t  *query;
    xcb_window_t *c;

    xcb_ungrab_key(dis, XCB_GRAB_ANY, screen->root, XCB_MOD_MASK_ANY);
    if ((query = xcb_query_tree_reply(dis,xcb_query_tree(dis,screen->root),0))) {
        c = xcb_query_tree_children(query);
        for (unsigned int i = 0; i != query->children_len; ++i) deletewindow(c[i]);
        free(query);
    }
    xcb_set_input_focus(dis, XCB_INPUT_FOCUS_POINTER_ROOT, screen->root, XCB_CURRENT_TIME);
    
    xcb_ewmh_connection_wipe(ewmh);
    if(ewmh)
        free(ewmh);

    // free each monitor
    monitor *m, *t;
    for (m = mons; m; m = t){
        t = m->next;
        free(m);
    }
    #if MENU
    // free each menu and each menuentry
    Menu *men, *tmen;
    Menu_Entry *ent, *tent;
    for (men = menus; men; men = tmen) {
        tmen = men->next;
        for (ent = men->head; ent; ent = tent) {
            tent = ent->next;
            free(ent);
        }
        free(men);
    }
    #endif
    xcb_disconnect(dis);
    #if PRETTY_PRINT
    kill(pid, SIGKILL);
    free(pp.ws);
    free(pp.mode);
    free(pp.dir);
    #endif 
}

// move a client to another desktop
//
// remove the current client from the current desktop's client list
// and add it as last client of the new desktop's client list
void client_to_desktop(const Arg *arg) {
    if(arg->i == selmon->curr_dtop || !desktops[selmon->curr_dtop].current)
        return;

    //remove from current desktop
    desktop *d = &desktops[selmon->curr_dtop];
    client *o = d->current;
    removeclientfromlist(o, d);
    if(!ISFT(o))
        tileremove(o, d, selmon);
    if(d->current)
        focus(d->current, d, selmon);

    //move to new desktop
    desktop *n = &desktops[arg->i];
    addclienttolist(o, n);
    monitor *m;
    for(m = mons; m; m = m->next)
        if(n == &desktops[m->curr_dtop])
            break;
    if(!ISFT(o))
        tilenew(o, n->prevfocus, n, m);
    else if(m)
        retile(n, m);
    else
        xcb_unmap_window(dis, o->win);

    #if PRETTY_PRINT
    updatews();
    desktopinfo();
    #endif

    DEBUG("client_to_desktop: leaving\n");
}

client* clientbehindfloater(desktop *d) {
    client *c = NULL;
    // try to find the first one behind the pointer
    xcb_query_pointer_reply_t *pointer = xcb_query_pointer_reply(dis, xcb_query_pointer(dis, screen->root), 0);
    if (pointer) {
        int mx = pointer->root_x; int my = pointer->root_y;
        for (c = d->head; c; c = c->next)
            if(!ISFT(c) && INRECT(mx, my, c->x, c->y, c->w, c->h))
                break;
    }
    // just find the first tiled client.
    if (!c)
        for (c = d->head; c; c = c->next)
            if(!ISFT(c))
                break;
    return c;
}

// To change the state of a mapped window, a client MUST
// send a _NET_WM_STATE client message to the root window
// message_type must be _NET_WM_STATE
//   data.l[0] is the action to be taken
//   data.l[1] is the property to alter three actions:
//   - remove/unset _NET_WM_STATE_REMOVE=0
//   - add/set _NET_WM_STATE_ADD=1,
//   - toggle _NET_WM_STATE_TOGGLE=2
//
// check if window requested fullscreen or activation
void clientmessage(xcb_generic_event_t *e) {
    xcb_client_message_event_t *ev = (xcb_client_message_event_t*)e;
    client *c = wintoclient(ev->window);
    desktop *d = &desktops[selmon->curr_dtop]; 
    if (!c) 
        return;

    if (c && ev->type                      == netatoms[NET_WM_STATE]
          && ((unsigned)ev->data.data32[1] == netatoms[NET_FULLSCREEN]
          ||  (unsigned)ev->data.data32[2] == netatoms[NET_FULLSCREEN])) {
            if (!(c->isfloating || c->istransient) || !d->head->next)
                retile(d, selmon);
    } else if (c && ev->type == netatoms[NET_ACTIVE]) 
        focus(c, d, selmon);
}

desktop *clienttodesktop(client *c) {
    client *n = NULL; desktop *d = NULL;
    int i;
 
    for (i = 0; i < DESKTOPS; i++)
        for (d = &desktops[i], n = d->head; n; n = n->next)
            if(n == c) {
                DEBUGP("clienttodesktop: leaving, returning found desktop #%d\n", i);
                return d;
            }
    
    return NULL;
}

client** clientstothebottom(client *w, desktop *d, bool samesize)
{
    client **l = (client**)malloc_safe((d->count + 1) * sizeof(client*));
    int size = 0;
    int i = 0;

    for(client *x = d->head; x; x = x->next)
        if(!ISFT(x) && x->yp == (w->yp + w->hp)) //directly below
            if(samesize ? 
                    (x->xp >= w->xp && (x->xp + x->wp) <= (w->xp + w->wp)) :   //width == or <=
                    ((x->xp >= w->xp || (x->xp + x->wp) >= (w->xp + w->wp)) && x->xp < (w->xp + w->wp))) { 
                l[i++] = x;
                size += x->wp;
                if(samesize ? (size == w->wp) : true)
                    return l;
            }

    free(l);
    return NULL;
}

client** clientstotheleft(client *w, desktop *d, bool samesize)
{
    client **l = (client**)malloc_safe((d->count + 1) * sizeof(client*));
    int size = 0;
    int i = 0;

    for(client *x = d->head; x; x = x->next)
        if(!ISFT(x) && (x->xp + x->wp) == w->xp) //directly to the left
            if(samesize ? 
                    (x->yp >= w->yp && (x->yp + x->hp) <= (w->yp + w->hp)) :  //height == or <=
                    ((x->yp >= w->yp || (x->yp + x->hp) >= (w->yp + w->hp)) && x->yp < (w->yp + w->hp))) { 
                l[i++] = x;
                size += x->hp;
                if(samesize ? (size == w->hp) : true)
                    return l;
            }

    free(l);
    return NULL;
}

client** clientstotheright(client *w, desktop *d, bool samesize)
{
    client **l = (client**)malloc_safe((d->count + 1) * sizeof(client*));
    int size = 0;
    int i = 0;

    for(client *x = d->head; x; x = x->next)
        if(!ISFT(x) && (w->xp + w->wp) == x->xp) //directly to the right
            if(samesize ? 
                    (x->yp >= w->yp && (x->yp + x->hp) <= (w->yp + w->hp)) : //height == or <=
                    ((x->yp >= w->yp || (x->yp + x->hp) >= (w->yp + w->hp)) && x->yp < (w->yp + w->hp))) { 
                l[i++] = x;
                size += x->hp;
                if(samesize ? (size == w->hp) : true)
                    return l;
            }

    free(l);
    return NULL;
}

client** clientstothetop(client *w, desktop *d, bool samesize)
{
    client **l = (client**)malloc_safe((d->count + 1) * sizeof(client*));
    int size = 0;
    int i = 0;

    for(client *x = d->head; x; x = x->next) {
        if(!ISFT(x) && w->yp == (x->yp + x->hp)) //directly above
            if(samesize ? 
                    (x->xp >= w->xp && (x->xp + x->wp) <= (w->xp + w->wp)) : //width == or <=
                    ((x->xp >= w->xp || (x->xp + x->wp) >= (w->xp + w->wp)) && x->xp < (w->xp + w->wp))) { 
                l[i++] = x;
                size += x->wp;
                if(samesize ? (size == w->wp) : true)
                    return l;
            }
    }

    free(l);
    return NULL;
}

// a configure request means that the window requested changes in its geometry
// state. if the window doesnt have a client set the appropriate values as 
// requested, else fake it.
void configurerequest(xcb_generic_event_t *e) {
    xcb_configure_request_event_t *ev = (xcb_configure_request_event_t*)e; 
    unsigned int v[7];
    unsigned int i = 0;
    monitor *m; client *c; 
   
    if ((!(c = wintoclient(ev->window)) || c->istransient)) { // if it has no client, configure it or if it's transient let it configure itself
        if ((m = wintomon(ev->window))) {
            if (ev->value_mask & XCB_CONFIG_WINDOW_X) {
                if (ev->x > m->x) {
                    if(c)
                        c->x = ev->x;
                    v[i++] = ev->x;
                } else {
                    if(c)
                        c->x = (m->x + ev->x);
                    v[i++] = (m->x + ev->x);
                }
            }
            if (ev->value_mask & XCB_CONFIG_WINDOW_Y) {
                if (ev->y > m->y) {
                    if(c)
                        c->y = (ev->y + (desktops[m->curr_dtop].showpanel && TOP_PANEL) ? PANEL_HEIGHT : 0);
                    v[i++] = (ev->y + (desktops[m->curr_dtop].showpanel && TOP_PANEL) ? PANEL_HEIGHT : 0);
                } else {
                    if(c)
                        c->y = ((m->y + ev->y) + (desktops[m->curr_dtop].showpanel && TOP_PANEL) ? PANEL_HEIGHT : 0);
                    v[i++] = ((m->y + ev->y) + (desktops[m->curr_dtop].showpanel && TOP_PANEL) ? PANEL_HEIGHT : 0);
                }
            }
        } else {
            if (ev->value_mask & XCB_CONFIG_WINDOW_X) {
                if(c)
                    c->x = ev->x;
                v[i++] = ev->x;
            }
            if (ev->value_mask & XCB_CONFIG_WINDOW_Y) {
                if(c)
                    c->y = (ev->y + (desktops[selmon->curr_dtop].showpanel && TOP_PANEL) ? PANEL_HEIGHT : 0);
                v[i++] = (ev->y + (desktops[selmon->curr_dtop].showpanel && TOP_PANEL) ? PANEL_HEIGHT : 0);
            }
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
            if(c)
                c->w = (ev->width  < selmon->w - BORDER_WIDTH) ? ev->width  : selmon->w - BORDER_WIDTH;
            v[i++] = (ev->width  < selmon->w - BORDER_WIDTH) ? ev->width  : selmon->w - BORDER_WIDTH;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
            if(c)
                c->h = (ev->height < selmon->h - BORDER_WIDTH) ? ev->height : selmon->h - BORDER_WIDTH;
            v[i++] = (ev->height < selmon->h - BORDER_WIDTH) ? ev->height : selmon->h - BORDER_WIDTH;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) v[i++] = ev->border_width;
        if (ev->value_mask & XCB_CONFIG_WINDOW_SIBLING)      v[i++] = ev->sibling;
        if (ev->value_mask & XCB_CONFIG_WINDOW_STACK_MODE)   v[i++] = ev->stack_mode;
        xcb_configure_window_checked(dis, ev->window, ev->value_mask, v);
        if(c) {
            if (m)
                setclientborders(c, &desktops[m->curr_dtop], m);
            else
                setclientborders(c, &desktops[selmon->curr_dtop], selmon);
        }
    } else { // has a client, fake configure it
        xcb_send_event(dis, false, c->win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char*)ev);
    }
    xcb_flush(dis);
}

#if MENU
Menu* createmenu(char **list) {
    Menu *m = (Menu*)malloc_safe(sizeof(Menu));
    Menu_Entry *mentry, *sentry = NULL, *itr = NULL;
    int i, x, y;

    m->list = list;
    for (i = 0; list[i]; i++) {
        if (!m->head) {
            mentry = createmenuentry(selmon->w/2 - 50, selmon->h/2 - 30, 100, 60, list[i]);
            m->head = itr = sentry = mentry;
        } else {
            if (sentry->l && sentry->t && !sentry->b) {
                x = sentry->x;
                y = sentry->y + 60;
                DEBUGP("createmenu: x %d y %d \n", x, y);
                mentry = createmenuentry(x, y, 100, 60, list[i]);
                sentry->b = itr->next = mentry;
                mentry->t = sentry;
                itr = itr->next;
                if (sentry->r) {
                    if (!sentry->r->r) sentry = sentry->r;
                    else if (sentry->r->b) {
                        sentry->r->b->l = mentry;
                        mentry->r = sentry->r->b;
                        sentry = sentry->r->b;
                    }
                } 
            } else if (sentry->t && !sentry->l) {
                x = sentry->x - 100;
                y = sentry->y;
                DEBUGP("createmenu: x %d y %d \n", x, y);
                mentry = createmenuentry(x, y, 100, 60, list[i]);
                sentry->l = itr->next = mentry;
                mentry->r = sentry;
                itr = itr->next;
                if (sentry->b && sentry->b->l) {
                    sentry->b->l->t = mentry;
                    mentry->b = sentry->b->l;
                    sentry = sentry->b->l;
                }
            } else if (sentry->r && !sentry->t) {
                x = sentry->x;
                y = sentry->y - 60;
                DEBUGP("createmenu: x %d y %d \n", x, y);
                mentry = createmenuentry(x, y, 100, 60, list[i]);
                sentry->t = itr->next = mentry;
                mentry->b = sentry;
                itr = itr->next;
                if (sentry->l && sentry->l->t) {
                    sentry->l->t->r = mentry;
                    mentry->l = sentry->l->t;
                    sentry = sentry->l->t;
                }
            } else if (!sentry->r) { 
                x = sentry->x + 100;
                y = sentry->y;
                DEBUGP("createmenu: x %d y %d \n", x, y);
                mentry = createmenuentry(x, y, 100, 60, list[i]);
                sentry->r = itr->next = mentry;
                mentry->l = sentry;
                itr = itr->next;
                if (sentry->t && sentry->t->r) {
                    sentry->t->r->b = mentry;
                    mentry->t = sentry->t->r;
                    while (sentry->r) {
                        sentry = sentry->r;
                        if (sentry->t) sentry = sentry->t;
                    }
                }
            } 
        }
    }
    m->next = NULL;
    return m;
}

Menu_Entry* createmenuentry(int x, int y, int w, int h, char *cmd) {
    Menu_Entry *m = (Menu_Entry*)malloc_safe(sizeof(Menu_Entry));
    
    m->cmd[0] = cmd;
    m->cmd[1] = NULL; 
    m->rectangles = (xcb_rectangle_t*)malloc_safe(sizeof(xcb_rectangle_t));
    m->x = m->rectangles->x = x;
    m->y = m->rectangles->y = y;
    m->rectangles->width = w;
    m->rectangles->height = h;
    m->next = m->b = m->l = m->r = m->t = NULL;
    // we might also want to save coordinates for the string to print
    return m;
}
#endif

monitor* createmon(xcb_randr_output_t id, int x, int y, int w, int h, int dtop) {
    monitor *m = (monitor*)malloc_safe(sizeof(monitor));
    
    m->id = id;
    m->curr_dtop = (dtop - 1);
    m->haspanel = ((nmons == PANEL_MON) ? true:false);
    m->x = x;
    m->y = y + (m->haspanel && TOP_PANEL ? PANEL_HEIGHT:0);
    m->w = w;
    m->h = h - (m->haspanel && !TOP_PANEL ? PANEL_HEIGHT:0); 
    m->next = NULL;
    DEBUGP("createmon: creating monitor with x:%d y:%d w:%d h:%d desktop #:%d\n", m->x, m->y, m->w, m->h, (dtop - 1));
    return m;
}

// close the window
void deletewindow(xcb_window_t w) {
    xcb_client_message_event_t ev;
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = w;
    ev.format = 32;
    ev.sequence = 0;
    ev.type = wmatoms[WM_PROTOCOLS];
    ev.data.data32[0] = wmatoms[WM_DELETE_WINDOW];
    ev.data.data32[1] = XCB_CURRENT_TIME;
    xcb_send_event(dis, 0, w, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);
}

#if PRETTY_PRINT
// output info about the desktops on standard output stream
// once the info is printed, immediately flush the stream
void desktopinfo(void) {
    desktop *d = &desktops[selmon->curr_dtop];
    PP_PRINTF;
    fflush(stdout);
}
#endif

// a destroy notification is received when a window is being closed
// on receival, remove the appropriate client that held that window
void destroynotify(xcb_generic_event_t *e) {
    xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t*)e;
    
    client *c = wintoclient(ev->window); 
    if (c){
        desktop *d = clienttodesktop(c);
        monitor *m = wintomon(ev->window);
        removeclient(c, d, m, true);
    }
    
    #if PRETTY_PRINT
    desktopinfo();
    #endif
}

// TODO: we dont need this event for FOLLOW_MOUSE false
// when the mouse enters a window's borders
// the window, if notifying of such events (EnterWindowMask)
// will notify the wm and will get focus
void enternotify(xcb_generic_event_t *e) {
    xcb_enter_notify_event_t *ev = (xcb_enter_notify_event_t*)e;  

    if (ev->mode != XCB_NOTIFY_MODE_NORMAL || ev->detail == XCB_NOTIFY_DETAIL_INFERIOR) {
        DEBUG("enternotify: leaving under user FOLLOW_MOUSE setting or event rules to not enter\n");
        return;
    }

    desktop *d = &desktops[selmon->curr_dtop];

    if(!d->current || ev->event != d->current->win) {
        client *w = wintoclient(ev->event);
        monitor *m = NULL;
        if((m = wintomon(ev->event)) && m != selmon) {
            monitor *mold = selmon;
            client *cold = desktops[selmon->curr_dtop].current;
            selmon = m;
            #if PRETTY_PRINT
            updatews();
            updatemode();
            updatedir();
            #endif
            if (cold)
                setclientborders(cold, &desktops[mold->curr_dtop], mold);
        
        }

        d = &desktops[m->curr_dtop];
        d->prevfocus = d->current;
        d->current = w;

        DEBUGP("enternotify: c->x: %d c->y: %d c->w: %d c->h: %d\n", 
                d->current->x, d->current->y, d->current->w, d->current->h);
        focus(d->current, d, m);
    }
}

#if PRETTY_PRINT
// Expose event means we should redraw our windows
void expose(xcb_generic_event_t *e) { 
    monitor *m;
    xcb_expose_event_t *ev = (xcb_expose_event_t*)e;

    if(ev->count == 0 && (m = wintomon(ev->window))){
        // redraw windows - xcb_flush?
        desktopinfo();
    }
}    
#endif

// highlight borders and set active window and input focus
// if given current is NULL then delete the active window property
//
// stack order by client properties, top to bottom:
//  - current when floating or transient
//  - floating or trancient windows
//  - current when tiled
//  - current when fullscreen
//  - fullscreen windows
//  - tiled windows
//
// a window should have borders in any case, except if
//  - the window is the only window on screen
//  - the mode is MONOCLE or VIDEO
void focus(client *c, desktop *d, const monitor *m) {
     
    if(d->prevfocus)
        setclientborders(d->prevfocus, d, m);
    setclientborders(c, d, m);
        
    xcb_change_property(dis, XCB_PROP_MODE_REPLACE, screen->root, netatoms[NET_ACTIVE], XCB_ATOM_WINDOW, 32, 1, &c->win);
    xcb_set_input_focus(dis, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
    xcb_flush(dis);
     
    #if PRETTY_PRINT
    desktopinfo();
    #endif
}

// events which are generated by clients
void focusin(xcb_generic_event_t *e) {
    xcb_focus_in_event_t *ev = (xcb_focus_in_event_t*)e;

    if (ev->mode == XCB_NOTIFY_MODE_GRAB || ev->mode == XCB_NOTIFY_MODE_UNGRAB) {
        DEBUG("focusin: event for grab/ungrab, ignoring\n");
        return;
    }

    if (ev->detail == XCB_NOTIFY_DETAIL_POINTER) {
        DEBUG("focusin: notify detail is pointer, ignoring this event\n");
        return;
    }

    if(!desktops[selmon->curr_dtop].current && ev->event != desktops[selmon->curr_dtop].current->win)
        xcb_set_input_focus(dis, XCB_INPUT_FOCUS_POINTER_ROOT, desktops[selmon->curr_dtop].current->win,
                            XCB_CURRENT_TIME);
}

// get a pixel with the requested color
// to fill some window area - borders
unsigned int getcolor(char* color) {
    xcb_colormap_t map = screen->default_colormap;
    xcb_alloc_color_reply_t *c;
    unsigned int r, g, b, rgb, pixel;

    rgb = xcb_get_colorpixel(color);
    r = rgb >> 16; g = rgb >> 8 & 0xFF; b = rgb & 0xFF;
    c = xcb_alloc_color_reply(dis, xcb_alloc_color(dis, map, r * 257, g * 257, b * 257), NULL);
    if (!c)
        errx(EXIT_FAILURE, "error: cannot allocate color '%s'\n", color);

    pixel = c->pixel; free(c);
    return pixel;
}

void getoutputs(xcb_randr_output_t *outputs, const int len, xcb_timestamp_t timestamp) {
    // Walk through all the RANDR outputs (number of outputs == len) there
    // was at time timestamp.
    xcb_randr_get_crtc_info_cookie_t icookie;
    xcb_randr_get_crtc_info_reply_t *crtc = NULL;
    xcb_randr_get_output_info_reply_t *output;
    xcb_randr_get_output_info_cookie_t ocookie[len];
    monitor *m;
    int i, n;
    bool flag;
 
    // get output cookies
    for (i = 0; i < len; i++) 
        ocookie[i] = xcb_randr_get_output_info(dis, outputs[i], timestamp);

    for (i = 0; i < len; i ++) { /* Loop through all outputs. */
        output = xcb_randr_get_output_info_reply(dis, ocookie[i], NULL);

        if (output == NULL) 
            continue;
        //asprintf(&name, "%.*s",xcb_randr_get_output_info_name_length(output),xcb_randr_get_output_info_name(output));

        if (XCB_NONE != output->crtc) {
            icookie = xcb_randr_get_crtc_info(dis, output->crtc, timestamp);
            crtc    = xcb_randr_get_crtc_info_reply(dis, icookie, NULL);

            if (NULL == crtc) 
                return; 

            flag = true;

            //check for uniqeness or update old
            for (n = 0, m = mons; m; m = m->next, n++) {
                if (outputs[i] == m->id) {
                    flag = false;
                    //if they are the same check to see if the dimensions have
                    //changed. and retile
                    DEBUGP("%d %d %d %d %d %d %d %d\n", crtc->x, m->x,(crtc->y + ((SHOW_PANEL && TOP_PANEL) ? PANEL_HEIGHT:0)), 
                            m->y, crtc->width, m->w, (crtc->height - (SHOW_PANEL ? PANEL_HEIGHT:0)), m->h);
                    if (crtc->x != m->x||(crtc->y + ((SHOW_PANEL && TOP_PANEL) ? PANEL_HEIGHT:0)) != m->y||
                        crtc->width != m->w|| (crtc->height - (SHOW_PANEL ? PANEL_HEIGHT:0)) != m->h) {
                        DEBUG("getoutputs: adjusting monitor\n");
                        m->x = crtc->x;
                        m->y = crtc->y;
                        m->w = crtc->width;
                        m->h = crtc->height;
                        retile(&desktops[m->curr_dtop], m);
                    }
                    break;
                }
            }
            // if unique, add it to the list, give it a desktop/workspace
            if (flag){
                DEBUG("getoutputs: adding monitor\n");
                for(m = mons; m && m->next; m = m->next);
                if(m) {
                    DEBUG("getoutputs: entering m->next = createmon\n");
                    m->next = createmon(outputs[i], crtc->x, crtc->y, crtc->width, crtc->height, ++nmons);
                }
                else {
                    DEBUG("getoutputs: entering mon = createmon\n");
                    mons = createmon(outputs[i], crtc->x, crtc->y, crtc->width, crtc->height, ++nmons);
                }
            }
        }
        else {
            //find monitor and delete
            for (m = mons; m; m = m->next) {
                if (m->id == outputs[i]) { //monitor found
                    if (m == mons)
                        mons = mons->next;
                    if (m == selmon)
                        selmon = mons;
                    DEBUG("getoutputs: deleting monitor\n");
                    free(m);
                    nmons--;
                    break;
                } 
            }
        }
        free(output);
    }
}

void getrandr(void) { // Get RANDR resources and figure out how many outputs there are.
    xcb_randr_get_screen_resources_current_cookie_t rcookie = xcb_randr_get_screen_resources_current(dis, screen->root);
    xcb_randr_get_screen_resources_current_reply_t *res = xcb_randr_get_screen_resources_current_reply(dis, rcookie, NULL);
    if (NULL == res) return;
    xcb_timestamp_t timestamp = res->config_timestamp;
    int len     = xcb_randr_get_screen_resources_current_outputs_length(res);
    xcb_randr_output_t *outputs = xcb_randr_get_screen_resources_current_outputs(res);
    /* Request information for all outputs. */
    getoutputs(outputs, len, timestamp);
    free(res);
}

bool getrootptr(int *x, int *y) {
    xcb_query_pointer_reply_t *reply = xcb_query_pointer_reply(dis, xcb_query_pointer(dis, screen->root), NULL);

    *x = reply->root_x;
    *y = reply->root_y;

    free(reply);

    return true;
}

// set the given client to listen to button events (presses / releases)
void grabbuttons(client *c) {
    unsigned int i, j, modifiers[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask|XCB_MOD_MASK_LOCK }; 
    xcb_ungrab_button(dis, XCB_BUTTON_INDEX_ANY, c->win, XCB_GRAB_ANY);
    for(i = 0; i < LENGTH(buttons); i++)
        for(j = 0; j < LENGTH(modifiers); j++)
            #if CLICK_TO_FOCUS
            xcb_grab_button(dis, false, c->win, BUTTONMASK, XCB_GRAB_MODE_SYNC,
                                XCB_GRAB_MODE_ASYNC, XCB_WINDOW_NONE, XCB_CURSOR_NONE,
                                XCB_BUTTON_INDEX_ANY, XCB_BUTTON_MASK_ANY);
    
            #else
            xcb_grab_button(dis, false, c->win, BUTTONMASK, XCB_GRAB_MODE_SYNC,
                                XCB_GRAB_MODE_ASYNC, XCB_WINDOW_NONE, XCB_CURSOR_NONE,
                                buttons[i].button, buttons[i].mask | modifiers[j]);
            #endif
}

// the wm should listen to key presses
void grabkeys(void) {
    xcb_keycode_t *keycode;
    unsigned int modifiers[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask|XCB_MOD_MASK_LOCK };
    xcb_ungrab_key(dis, XCB_GRAB_ANY, screen->root, XCB_MOD_MASK_ANY);
    for (unsigned int i=0; i<LENGTH(keys); i++) {
        keycode = xcb_get_keycodes(keys[i].keysym);
        for (unsigned int k=0; keycode[k] != XCB_NO_SYMBOL; k++)
            for (unsigned int m=0; m<LENGTH(modifiers); m++)
                xcb_grab_key(dis, 1, screen->root, keys[i].mod | modifiers[m], keycode[k], XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    }
}

#if MENU
void initializexresources() {
    //we should also go ahead and intitialize all the font gc's
    uint32_t            value_list[3];
    uint32_t            gcvalues[2];
    xcb_void_cookie_t   cookie_font;
    xcb_void_cookie_t   cookie_gc;
    xcb_generic_error_t *error;
    xcb_font_t          font;
    uint32_t            mask, font_mask;
    xcb_drawable_t      win = screen->root;
    XrmValue value;
    char *str_type[20];
    char buffer[20];
    char *names[] = { "*color1", "*color2",  "*color3", "*color4", "*color5", "*color6", 
                    "*color9", "*color10", "*color11", "*color12", "*color13", "*color14", NULL };
    char *class[] = { "*Color1", "*Color2",  "*Color3", "*Color4", "*Color5", "*Color6", 
                    "*Color9", "*Color10", "*Color11", "*Color12", "*Color13", "*Color14", NULL };
    struct passwd *pw = getpwuid(getuid());
    char *xdefaults = pw->pw_dir;
    strcat(xdefaults, "/.Xdefaults");

    // initialize font
    // TODO: user font
    font = xcb_generate_id (dis);
    cookie_font = xcb_open_font_checked (dis, font, strlen ("7x13"), "7x13");
    error = xcb_request_check (dis, cookie_font);
    if (error) {
        fprintf (stderr, "ERROR: can't open font : %d\n", error->error_code);
        xcb_disconnect (dis);
    }

    // initialize some values used to get the font gc's
    font_mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT;
    value_list[0] = screen->black_pixel;
    value_list[2] = font;
    // initialize some values for foreground gc's
    mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
    gcvalues[1] = 0;

    // initialize Xresources
    XrmInitialize();
    XrmDatabase dbase = XrmGetFileDatabase(xdefaults);

    for(int i = 0; i < 12; i++) {
        // now get all the colors from xresources and make the gc's
        if (XrmGetResource(dbase, names[i], class[i], str_type, &value)) {
            // getting color
            strncpy(buffer, value.addr, (int) value.size);
            xres.color[i] = getcolor(buffer);
            
            // getting rectangle foreground colors
            xres.gc_color[i] = xcb_generate_id(dis);
            gcvalues[0] = xres.color[i];
            xcb_create_gc (dis, xres.gc_color[i], win, mask, gcvalues);
            
            // getting font gc
            xres.font_gc[i] = xcb_generate_id(dis);
            value_list[1] = xres.color[i];
            cookie_gc = xcb_create_gc_checked (dis, xres.font_gc[i], win, font_mask, value_list);
            error = xcb_request_check (dis, cookie_gc);
            if (error) {
                fprintf (stderr, "ERROR: can't create gc : %d\n", error->error_code);
                xcb_disconnect (dis);
                exit (-1);
            } 
        }
    } 

    cookie_font = xcb_close_font_checked (dis, font);
    error = xcb_request_check (dis, cookie_font);
    if (error) {
        fprintf (stderr, "ERROR: can't close font : %d\n", error->error_code);
        xcb_disconnect (dis);
        exit (-1);
    }
}
#endif

// on the press of a key check to see if there's a binded function to call
void keypress(xcb_generic_event_t *e) {
    xcb_key_press_event_t *ev       = (xcb_key_press_event_t *)e;
    xcb_keysym_t           keysym   = xcb_get_keysym(ev->detail);
    DEBUGP("xcb: keypress: code: %d mod: %d\n", ev->detail, ev->state);
    for (unsigned int i=0; i < LENGTH(keys); i++)
        if (keysym == keys[i].keysym && CLEANMASK(keys[i].mod) == CLEANMASK(ev->state) && keys[i].func)
                keys[i].func(&keys[i].arg);
}

// explicitly kill a client - close the highlighted window
// send a delete message and remove the client
void killclient() {
    desktop *d = &desktops[selmon->curr_dtop];
    if (!d->current) return;
    xcb_icccm_get_wm_protocols_reply_t reply; unsigned int n = 0; bool got = false;
    if (xcb_icccm_get_wm_protocols_reply(dis,
        xcb_icccm_get_wm_protocols(dis, d->current->win, wmatoms[WM_PROTOCOLS]),
        &reply, NULL)) { // TODO: Handle error?
        for(; n != reply.atoms_len; ++n) 
            if ((got = reply.atoms[n] == wmatoms[WM_DELETE_WINDOW])) 
                break;
        xcb_icccm_get_wm_protocols_reply_wipe(&reply);
    }
    if (got) deletewindow(d->current->win);
    else xcb_kill_client(dis, d->current->win);
}

#if MENU
void launchmenu(const Arg *arg) {
    xcb_drawable_t win;
    xcb_generic_event_t *e;
    uint32_t mask = 0;
    uint32_t winvalues[1];
    bool waitforevents = true, found = false; 
    Menu *m = NULL;
    int i, x, y;

    //find which menu
    for (m = menus; m; m = m->next)
        if (strcmp(m->list[0], arg->list[0]) == 0) {
            DEBUG("launchmenu: found menu\n");
            break;
        }
    
    // Create black (foreground) graphic context
    win = screen->root; 

    // Ask for our window's Id
    win = xcb_generate_id(dis);

    // Create the window
    mask = XCB_CW_EVENT_MASK;
    winvalues[0] = XCB_EVENT_MASK_EXPOSURE;
    xcb_create_window (dis, XCB_COPY_FROM_PARENT, win, screen->root, selmon->x, 
                        selmon->y, selmon->w, selmon->h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, 
                        screen->root_visual, mask, winvalues);

    // Map the window on the screen
    xcb_map_window (dis, win);
     
    xcb_change_property(dis, XCB_PROP_MODE_REPLACE, screen->root, netatoms[NET_ACTIVE], XCB_ATOM_WINDOW, 32, 1, &win);
    xcb_set_input_focus(dis, XCB_INPUT_FOCUS_POINTER_ROOT, win, XCB_CURRENT_TIME);
    
    // We flush the request
    xcb_flush (dis);

    while (waitforevents && (e = xcb_wait_for_event (dis))) {
        switch (e->response_type & ~0x80) {
            case XCB_EXPOSE: {
                DEBUG("launchmenu: entering XCB_EXPOSE\n");
                // loop through menu_entries
                i = 0;
                for (Menu_Entry *mentry = m->head; mentry; mentry = mentry->next) {
                    DEBUG("launchmenu: drawing iteration\n");
                    // We draw the rectangles
                    xcb_poly_fill_rectangle(dis, win, xres.gc_color[i], 1, mentry->rectangles);
                    // we also want to draw the command/program
                    text_draw(xres.font_gc[i], win, mentry->x + 10, mentry->y + 30, mentry->cmd[0]);
                    if (i == 11) i = 0;
                    else i++;
                }
                // We flush the request
                xcb_flush (dis);
                break;
            }
            case XCB_BUTTON_PRESS: {
                DEBUG("launchmenu: entering XCB_BUTTON_PRESS\n");
                waitforevents = false;
                xcb_unmap_window (dis, win);
                if (getrootptr(&x, &y)) {
                x -= selmon->x;
                y -= selmon->y;
                DEBUGP("launchmenu: x %d y %d\n", x, y);
                    for (Menu_Entry *mentry = m->head; mentry; ) {
                        DEBUGP("launchmenu: mentry->x %d mentry->y %d\n", mentry->x, mentry->y);
                        if (INRECT(x, y, mentry->x, mentry->y, 100, 60)) { 
                            found = true;
                            if (fork()) return;
                            if (dis) close(screen->root);
                            setsid();
                            execvp(mentry->cmd[0], mentry->cmd);
                            break; // exit loop
                        }
                        
                        if (x < mentry->x) {
                            if (!mentry->l) break;
                            else mentry = mentry->l;
                        } else if (x > mentry->x + 100) { 
                            if (!mentry->r) break;
                            else mentry = mentry->r;
                        }
                        
                        if (y < mentry->y) {
                            if (!mentry->t) break;
                            else mentry = mentry->t;
                        } else if (y > mentry->y + 60) {
                            if (!mentry->b) break;
                            else mentry = mentry->b;
                        }
                    }
                }
    
                if (!found)
                    focus(desktops[selmon->curr_dtop].current, &desktops[selmon->curr_dtop], selmon);

                break;
            }
            case XCB_KEY_RELEASE: {
                DEBUG("launchmenu: entering XCB_KEY_RELEASE\n");
                break; 
            }
            default: {
                DEBUG("launchmenu: unknown event\n");
                // Unknown event type, ignore it
                break;
            }
        }
        // Free the Generic Event
        free (e);
    }
}
#endif

void* malloc_safe(size_t size) {
    void *ret;
    if(!(ret = malloc(size)))
        puts("malloc_safe: fatal: could not malloc()");
    memset(ret, 0, size);
    return ret;
}

void mappingnotify(xcb_generic_event_t *e) {
    xcb_mapping_notify_event_t *ev = (xcb_mapping_notify_event_t*)e;
    //xcb_keysym_t           keysym   = xcb_get_keysym(ev->detail);

    //xcb_refresh_keyboard_mapping(keysym, ev);
    if(ev->request == XCB_MAPPING_NOTIFY)
        grabkeys();
}

// a map request is received when a window wants to display itself
// if the window has override_redirect flag set then it should not be handled
// by the wm. if the window already has a client then there is nothing to do.
//
// get the window class and name instance and try to match against an app rule.
// create a client for the window, that client will always be current.
// check for transient state, and fullscreen state and the appropriate values.
// if the desktop in which the window was spawned is the current desktop then
// display the window, else, if set, focus the new desktop.
void maprequest(xcb_generic_event_t *e) {
    client *c = NULL; 
    xcb_map_request_event_t            *ev = (xcb_map_request_event_t*)e;
    xcb_window_t                       windows[] = { ev->window }, transient = 0;
    xcb_get_window_attributes_reply_t  *attr[1];
    xcb_ewmh_get_atoms_reply_t         type;

    xcb_get_attributes(windows, attr, 1);
    if (!attr[0] || attr[0]->override_redirect) 
        return;
    c = wintoclient(ev->window);
    if (c) 
        return; 
 
    desktop *d = &desktops[selmon->curr_dtop];
    c = addwindow(ev->window, d);

    xcb_icccm_get_wm_transient_for_reply(dis, xcb_icccm_get_wm_transient_for_unchecked(dis, ev->window), &transient, NULL); // TODO: error handling
    c->istransient = transient?true:false;
    if (xcb_ewmh_get_wm_window_type_reply(ewmh, xcb_ewmh_get_wm_window_type(ewmh, ev->window), &type, NULL) == 1) {
        for (unsigned int i = 0; i < type.atoms_len; i++) {
            xcb_atom_t a = type.atoms[i];
            if (a == ewmh->_NET_WM_WINDOW_TYPE_SPLASH
                || a == ewmh->_NET_WM_WINDOW_TYPE_DIALOG
                || a == ewmh->_NET_WM_WINDOW_TYPE_DROPDOWN_MENU
                || a == ewmh->_NET_WM_WINDOW_TYPE_POPUP_MENU
                || a == ewmh->_NET_WM_WINDOW_TYPE_TOOLTIP
                || a == ewmh->_NET_WM_WINDOW_TYPE_NOTIFICATION) {
                c->istransient = true;
            }
        }
        xcb_ewmh_get_atoms_reply_wipe(&type);
    }
    c->isfloating  = d->mode == FLOAT || c->istransient;

    if (c->isfloating) {
        c->x = selmon->x + selmon->w / 4;
        c->y = selmon->y + selmon->h / 4;
        c->w = selmon->w / 2;
        c->h = selmon->h / 2;
    } 
 
    tilenew(d->current, d->prevfocus, d, selmon); 
    xcb_map_window(dis, c->win);
        
    if(c->istransient)
        retile(d, selmon); 

    focus(c, d, selmon);
     
    grabbuttons(c);
    
    #if PRETTY_PRINT
    updatetitle(c);
    desktopinfo();
    #endif
}

// each window should cover all the available screen space
void monocle(const desktop *d, const monitor *m) {
    if(d->head){
        for (client *c = d->head; c; c = c->next)
            if(c != d->current) {
                if(!c->isfloating)
                    xcb_move_resize_monocle(c, d, m);
            }

        xcb_move_resize_monocle(d->current, d, m);
        xcb_raise_window(d->current->win);
    }
}

// grab the pointer and get it's current position
// all pointer movement events will be reported until it's ungrabbed
// until the mouse button has not been released,
// grab the interesting events - button press/release and pointer motion
// and on on pointer movement resize or move the window under the curson.
// if the received event is a map request or a configure request call the
// appropriate handler, and stop listening for other events.
// Ungrab the poitner and event handling is passed back to run() function.
// Once a window has been moved or resized, it's marked as floating.
void mousemotion(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    client *c = d->current;

    xcb_get_geometry_reply_t  *geometry;
    xcb_query_pointer_reply_t *pointer;
    xcb_grab_pointer_reply_t  *grab_reply;
    int mx, my, winx, winy, winw, winh, xw, yh;

    if (!c) return;
    geometry = xcb_get_geometry_reply(dis, xcb_get_geometry(dis, c->win), NULL); // TODO: error handling
    if (geometry) {
        winx = geometry->x;     winy = geometry->y;
        winw = geometry->width; winh = geometry->height;
        free(geometry);
    } else return;

    pointer = xcb_query_pointer_reply(dis, xcb_query_pointer(dis, screen->root), 0);
    if (!pointer) return;
    mx = pointer->root_x; my = pointer->root_y;

    grab_reply = xcb_grab_pointer_reply(dis, xcb_grab_pointer(dis, 0, screen->root, BUTTONMASK|XCB_EVENT_MASK_BUTTON_MOTION|XCB_EVENT_MASK_POINTER_MOTION,
            XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE, XCB_CURRENT_TIME), NULL);
    if (!grab_reply || grab_reply->status != XCB_GRAB_STATUS_SUCCESS) return;

    xcb_generic_event_t *e = NULL;
    xcb_motion_notify_event_t *ev = NULL;
    bool ungrab = c->isfloating ? false:true;
    while (!ungrab && c) {
        if (e) 
            free(e); 
        xcb_flush(dis);
        while(!(e = xcb_wait_for_event(dis))) 
            xcb_flush(dis);
        switch (e->response_type & ~0x80) {
            case XCB_CONFIGURE_REQUEST: 
            case XCB_MAP_REQUEST:
                events[e->response_type & ~0x80](e);
                break;
            case XCB_MOTION_NOTIFY:
                ev = (xcb_motion_notify_event_t*)e;
                xw = (arg->i == MOVE ? winx : winw) + ev->root_x - mx;
                yh = (arg->i == MOVE ? winy : winh) + ev->root_y - my;
                if (arg->i == RESIZE) { 
                    xcb_resize(c->win, (c->w = xw>MINWSZ?xw:winw), ( c->h = yh>MINWSZ?yh:winh));
                    setclientborders(d->current, d, selmon);
                } else if (arg->i == MOVE) {  
                    xcb_move(c->win, (c->x = xw), (c->y = yh));
            
                    // handle floater moving monitors
                    if (!INRECT(xw, yh, selmon->x, selmon->y, selmon->w, selmon->h)) {
                        monitor *m = NULL;
                        for (m = mons; m && !INRECT(xw, yh, m->x, m->y, m->w, m->h); m = m->next);
                        if (m) { // we found a monitor
                            desktop *n = &desktops[m->curr_dtop];
                            removeclientfromlist(c, d);   
                            addclienttolist(c, n);
                            selmon = m;
                            //focus(c, n, m); //readjust focus for new desktop
                            d = &desktops[m->curr_dtop];
                            if(d->mode == MONOCLE || d->mode == VIDEO)
                                xcb_raise_window(c->win);
                            #if PRETTY_PRINT
                            updatews();
                            updatemode();
                            updatedir();
                            desktopinfo();
                            #endif
                        }
                    }
                }
                xcb_flush(dis);
                break;
            case XCB_KEY_PRESS:
            case XCB_KEY_RELEASE:
            case XCB_BUTTON_PRESS:
            case XCB_BUTTON_RELEASE:
                ungrab = true;
        }
    }
    xcb_ungrab_pointer(dis, XCB_CURRENT_TIME);

    if(d->mode == MONOCLE || d->mode == VIDEO)
        retile(d, selmon);
}

void moveclient(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    client *c = d->current, **list; 

    if (!c || c->isfloating) {
        DEBUG("moveclient: leaving, no d->current or c->isfloating\n");
        return;
    }

    if((d->mode == TILE || d->mode == FLOAT) && !c->isfloating) { //capable of having windows below?
        DEBUGP("moveclient: d->count = %d\n", d->count);
        if((list = clientstothe[arg->i](c, d, false))) {
            DEBUG("MOVING\n");
            client *o;
            o->xp = c->xp; o->yp = c->yp; o->wp = c->wp; o->hp = c->hp;
            c->xp = list[0]->xp; c->yp = list[0]->yp; c->wp = list[0]->wp; c->hp = list[0]->hp;
            list[0]->xp = o->xp; list[0]->yp = o->yp; list[0]->wp = o->wp; list[0]->hp = o->hp;
           
            SETWINDOW(list[0], d, selmon);
            SETWINDOW(c, d, selmon);
            xcb_move_resize(list[0], d, selmon);
            xcb_move_resize(c, d, selmon);
            
            free(list);
        }
    }
}

void movefocus(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    client *c = d->current, **list;

    if((d->mode == TILE || d->mode == FLOAT) && !c->isfloating) {
        DEBUGP("movefocus: d->count = %d\n", d->count);
        if ((list = clientstothe[arg->i](c, d, false))) {
            d->prevfocus = d->current;
            d->current = list[0];
            focus(d->current, d, selmon);
            free(list);
        }
    } else if (d->mode == MONOCLE || d->mode == VIDEO || d->mode == FLOAT) {
        DEBUG("movefocus: monocle or video\n"); 
        d->prevfocus = d->current;

        if(arg->i == TTOP || arg->i == TRIGHT){ 
            if(c->next) d->current = c->next;
            else        d->current = d->head;
        } else {
            client *n;
            for(n = d->head; n->next && n->next != c; n = n->next);
            d->current = n;
        }

        focus(d->current, d, selmon);
        retile(d, selmon);
    }
}

// cyclic focus the next window
// if the window is the last on stack, focus head
void next_win() {
    desktop *d = &desktops[selmon->curr_dtop];
    
    if (!d->current || !d->head->next) 
        return;
    d->prevfocus = d->current;
    d->current = d->current->next ? d->current->next:d->head;
    focus(d->current, d, selmon);
    
    if(d->mode == VIDEO || d->mode == MONOCLE)
        retile(d, selmon);
}

// get the previous client from the given
// if no such client, return NULL
client* prev_client(client *c, desktop *d) {
    if (!c || !d->head || !d->head->next)
        return NULL;
    client *p;
    for (p = d->head; p->next && p->next != c; p = p->next);
    return p;
}

// cyclic focus the previous window
// if the window is the head, focus the last stack window
void prev_win() {
    desktop *d = &desktops[selmon->curr_dtop];
    
    if (!d->current || !d->head->next) 
        return;
    d->current = prev_client(d->prevfocus = d->current, d);
    focus(d->current, d, selmon);

    if(d->mode == VIDEO || d->mode == MONOCLE)
        retile(d, selmon);
}

// property notify is called when one of the window's properties
// is changed, such as an urgent hint is received
void propertynotify(xcb_generic_event_t *e) {
    xcb_property_notify_event_t *ev = (xcb_property_notify_event_t*)e;
    client *c;

    c = wintoclient(ev->window);
    if (!c) { 
        DEBUG("propertynotify: leaving, NULL client\n");
        return;
    }

    #if PRETTY_PRINT
    if (ev->atom == XCB_ATOM_WM_NAME) {
        DEBUG("propertynotify: ev->atom == XCB_ATOM_WM_NAME\n");
        updatetitle(c);
        desktopinfo(); 
    }
    #endif
    if (ev->atom != XCB_ICCCM_WM_ALL_HINTS) {
        DEBUG("propertynotify: leaving, ev->atom != XCB_ICCCM_WM_ALL_HINTS\n");
        return;
    }
}

monitor* ptrtomon(int x, int y) {
    monitor *m;
    int i;

    for(i = 0, m = mons; i < nmons; m = m->next, i++)
        if(INRECT(x, y, m->x, m->y, m->w, m->h))
            return m;
    return selmon;
}

void pulltofloat() {
    desktop *d = &desktops[selmon->curr_dtop];
    client *c = d->current;

    if (!c->isfloating) {
        tileremove(c, d, selmon);
        c->isfloating = true;
    
        // move it to the center of the screen
        c->x = selmon->w / 4;
        c->y = selmon->h / 4;
        c->w = selmon->w / 2;
        c->h = selmon->h / 2;
        retile(d, selmon);
    }
}

void pushtotiling() {
    desktop *d = &desktops[selmon->curr_dtop];
    client *c = d->prevfocus, *n = d->current; // the client to push
    
    if (!n->isfloating && !n->istransient) // then it must already be tiled
        return;

    n->isfloating = false;
    n->istransient = false;

    if (c && c->isfloating)
        c = clientbehindfloater(d);

    tilenew(n, c, d, selmon);
    xcb_lower_window(n->win);
}

// to quit just stop receiving events
// run() is stopped and control is back to main()
void quit(const Arg *arg) {
    retval = arg->i;
    running = false;
}

// remove the specified client
//
// note, the removing client can be on any desktop,
// we must return back to the current focused desktop.
// if c was the previously focused, prevfocus must be updated
// else if c was the current one, current must be updated.
void removeclient(client *c, desktop *d, const monitor *m, bool delete) {
    removeclientfromlist(c, d);

    if (!c->isfloating)
        tileremove(c, d, m);
    else if (d->current)
        setclientborders(d->current, d, m);

    if(delete)
        deletewindow(c->win);

    if(d->current && m)
        focus(d->current, d, m);
    else
        xcb_set_input_focus(dis, XCB_INPUT_FOCUS_POINTER_ROOT, screen->root, XCB_CURRENT_TIME);

    free(c->title);
    free(c); c = NULL; 
    #if PRETTY_PRINT
    updatews();
    desktopinfo();
    #endif
}

void removeclientfromlist(client *c, desktop *d) {
    client **p = NULL;
    for (p = &d->head; *p && (*p != c); p = &(*p)->next);
    if (!p) 
        return;
    else 
        *p = c->next;
    if (c == d->prevfocus) 
        d->prevfocus = prev_client(d->current, d);
    if (c == d->current) {
        d->current = d->prevfocus ? d->prevfocus:d->head;
        d->prevfocus = prev_client(d->current, d);
    }
}

void resizeclient(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    client *c;
    
    if(d->mode != VIDEO && d->mode != MONOCLE) {
        c = d->current;
        if (!c) {
            DEBUG("resizeclient: leaving, no d->current\n");
            return;
        }
        monitor *m = wintomon(c->win);

        (arg->r)(arg->i, arg->p, c, d, m);
    }
} 

void resizeclientbottom(const int grow, const int size, client *c, desktop *d, monitor *m) {
    client** list; 
    if ((list = clientstothe[TBOTTOM](c, d, true))) {
        if (grow) {
            for (int i = 0; list[i]; i++) //client in list y increases and height decreases
                shrinkbyy(NULL, size, list[i], d, m);
            growbyh(list[0], size, c, d, m); //current windows height increases
        } else {
            shrinkbyh(NULL, size, c, d, m);
            for (int i = 0; list[i]; i++)
                growbyy(c, size, list[i], d, m);
        }
    } else if ((list = clientstothe[TTOP](c, d, true))) {
        if (grow) {
            shrinkbyy(NULL, size, c, d, m); //current windows y increases and height decreases
            for (int i = 0; list[i]; i++) //client in list height increases
                growbyh(c, size, list[i], d, m);
        } else {
            for (int i = 0; list[i]; i++)
                shrinkbyh(NULL, size, list[i], d, m);
            growbyy(list[0], size, c, d, m);
        }
    }

    free(list);
}

void resizeclientleft(const int grow, const int size, client *c, desktop *d, monitor *m) {
    client** list;
    if ((list = clientstothe[TLEFT](c, d, true))) {
        if (grow) {
            for (int i = 0; list[i]; i++) //client in list width decreases
                shrinkbyw(NULL, size, list[i], d, m);
            growbyx(list[0], size, c, d, m); //the current windows x decreases and width increases
        } else {
            shrinkbyx(NULL, size, c, d, m);
            for (int i = 0; list[i]; i++)
                growbyw(c, size, list[i], d, m);
        }
    } else if ((list = clientstothe[TRIGHT](c, d, true))) {
        if (grow) {
            shrinkbyw(NULL, size, c, d, m); //current windows width decreases
            for (int i = 0; list[i]; i++) //clients in list x decreases width increases
                growbyx(c, size, list[i], d, m);
        } else { 
            for (int i = 0; list[i]; i++)
                shrinkbyx(NULL, size, list[i], d, m);
            growbyw(list[0], size, c, d, m);
        }
    }

    free(list);
}

void resizeclientright(const int grow, const int size, client *c, desktop *d, monitor *m) {
    client** list;
    if ((list = clientstothe[TRIGHT](c, d, true))) { 
        if (grow) {
            for (int i = 0; list[i]; i++) //clients in list x increases and width decrease
                shrinkbyx(NULL, size, list[i], d, m);
            growbyw(list[0], size, c, d, m); //the current windows width increases
        } else {
            shrinkbyw(NULL, size, c, d, m);
            for (int i = 0; list[i]; i++)
                growbyx(c, size, list[i], d, m);
        }
    } else if ((list = clientstothe[TLEFT](c, d, true))) {
        if (grow) {
            shrinkbyx(NULL, size, c, d, m); //current windows x increases and width decreases
            for (int i = 0; list[i]; i++) //other windows width increases
                growbyw(c, size, list[i], d, m);
        } else {
            for (int i = 0; list[i]; i++)
                shrinkbyw(NULL, size, list[i], d, m);
            growbyx(list[0], size, c, d, m);
        }
    }

    free(list);
}

void resizeclienttop(const int grow, const int size, client *c, desktop *d, monitor *m) {
    client** list;
    if ((list = clientstothe[TTOP](c, d, true))) {
        if (grow) {
            for (int i = 0; list[i]; i++) //client in list height decreases
                shrinkbyh(NULL, size, list[i], d, m);
            growbyy(list[0], size, c, d, m); //current windows y decreases and height increases
        } else {
            shrinkbyy(NULL, size, c, d, m);
            for (int i = 0; list[i]; i++)
                growbyh(c, size, list[i], d, m);
        }
    } else if ((list = clientstothe[TBOTTOM](c, d, true))) {
        if (grow) { 
            shrinkbyh(NULL, size, c, d, m); //current windows height decreases
            for (int i = 0; list[i]; i++) //client in list y decreases and height increases
               growbyy(c, size, list[i], d, m);
        }else { 
            for (int i = 0; list[i]; i++)
                shrinkbyy(NULL, size, list[i], d, m);
            growbyh(list[0], size, c, d, m);
        }
    }

    free(list);
}

void retile(desktop *d, const monitor *m) {
    if (d->mode == TILE || d->mode == FLOAT) {
        DEBUGP("retile: d->count = %d\n", d->count);
       
        for (client *c = d->head; c; c=c->next) {

            if (!c->isfloating) {
                //xcb_lower_window(dis, c->win);
                SETWINDOW(c, d, m);
                xcb_move_resize(c, d, m);
            } else {
                for ( ; c->x >= m->x + m->w; c->x -= m->w);
                for ( ; c->y >= m->y + m->h; c->y -= m->h);

                for ( ; c->x < m->x; c->x += m->w);
                for ( ; c->y < m->y; c->y += m->h);
                
                xcb_raise_window(c->win);
                xcb_move_resize(c, d, m);
            }
        } 
    }
    else
        monocle(d, m);
}

// jump and focus the next or previous desktop
void rotate(const Arg *arg) {
    change_desktop(&(Arg){.i = (DESKTOPS + selmon->curr_dtop + arg->i) % DESKTOPS});
}

// jump and focus the next or previous desktop that has clients
void rotate_filled(const Arg *arg) {
    int n = arg->i;
    while (n < DESKTOPS && !desktops[(DESKTOPS + selmon->curr_dtop + n) % DESKTOPS].head) (n += arg->i);
    change_desktop(&(Arg){.i = (DESKTOPS + selmon->curr_dtop + n) % DESKTOPS});
}

// main event loop - on receival of an event call the appropriate event handler
void run(void) {
    xcb_generic_event_t *ev; 
    while(running) {
        DEBUG("run: running\n");
        xcb_flush(dis);
        if (xcb_connection_has_error(dis)) {
            DEBUG("run: x11 connection got interrupted\n");
            err(EXIT_FAILURE, "error: X11 connection got interrupted\n");
        }
        if ((ev = xcb_wait_for_event(dis))) {
            if (ev->response_type==randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
                DEBUG("run: entering getrandr()\n");
                getrandr();
            }
            if (events[ev->response_type & ~0x80]) {
                DEBUGP("run: entering event %d\n", ev->response_type & ~0x80);
                events[ev->response_type & ~0x80](ev);
            }
            else {DEBUGP("xcb: unimplented event: %d\n", ev->response_type & ~0x80);}
            free(ev);
        }
    }
}

void setclientborders(client *c, const desktop *d, const monitor *m) {
    unsigned int values[1];  /* this is the color maintainer */
    unsigned int zero[1];
    int half;
    
    zero[0] = 0;
    values[0] = BORDER_WIDTH; // Set border width.

    // find n = number of windows with set borders
    int n = d->count;
    DEBUGP("setclientborders: d->count = %d\n", d->count);

    // rules for no border
    if ((!c->isfloating && n == 1) || (d->mode == MONOCLE) || (d->mode == VIDEO)) {
        xcb_configure_window(dis, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, zero);
    }
    else {
        xcb_configure_window(dis, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, values);
        half = OUTER_BORDER;
        const xcb_rectangle_t rect_inner[] = {
            { c->w,0, BORDER_WIDTH-half,c->h+BORDER_WIDTH-half},
            { c->w+BORDER_WIDTH+half,0, BORDER_WIDTH-half,c->h+BORDER_WIDTH-half},
            { 0,c->h,c->w+BORDER_WIDTH-half,BORDER_WIDTH-half},
            { 0, c->h+BORDER_WIDTH+half,c->w+BORDER_WIDTH-half,BORDER_WIDTH-half},
            { c->w+BORDER_WIDTH+half,BORDER_WIDTH+c->h+half,BORDER_WIDTH,BORDER_WIDTH }
        };
        const xcb_rectangle_t rect_outer[] = {
            {c->w+BORDER_WIDTH-half,0,half,c->h+BORDER_WIDTH*2},
            {c->w+BORDER_WIDTH,0,half,c->h+BORDER_WIDTH*2},
            {0,c->h+BORDER_WIDTH-half,c->w+BORDER_WIDTH*2,half},
            {0,c->h+BORDER_WIDTH,c->w+BORDER_WIDTH*2,half}
        };
        xcb_pixmap_t pmap = xcb_generate_id(dis);
        // 2bwm test have shown that drawing the pixmap directly on the root 
        // window is faster then drawing it on the window directly
        xcb_create_pixmap(dis, screen->root_depth, pmap, c->win, c->w+(BORDER_WIDTH*2), c->h+(BORDER_WIDTH*2));
        xcb_gcontext_t gc = xcb_generate_id(dis);
        xcb_create_gc(dis, gc, pmap, 0, NULL);
        
        xcb_change_gc(dis, gc, XCB_GC_FOREGROUND, (c->isfloating ? &win_flt:&win_outer));
        xcb_poly_fill_rectangle(dis, pmap, gc, 4, rect_outer);

        xcb_change_gc(dis, gc, XCB_GC_FOREGROUND, (c == d->current && m == selmon ? &win_focus:&win_unfocus));
        xcb_poly_fill_rectangle(dis, pmap, gc, 5, rect_inner);
        xcb_change_window_attributes(dis,c->win, XCB_CW_BORDER_PIXMAP, &pmap);
        // free the memory we allocated for the pixmap
        xcb_free_pixmap(dis,pmap);
        xcb_free_gc(dis,gc);
    }
    xcb_flush(dis);
    DEBUG("setclientborders: leaving\n");
}

// get numlock modifier using xcb
int setup_keyboard(void) {
    xcb_get_modifier_mapping_reply_t *reply;
    xcb_keycode_t                    *modmap;
    xcb_keycode_t                    *numlock;

    reply   = xcb_get_modifier_mapping_reply(dis, xcb_get_modifier_mapping_unchecked(dis), NULL); /* TODO: error checking */
    if (!reply) return -1;

    modmap = xcb_get_modifier_mapping_keycodes(reply);
    if (!modmap) return -1;

    numlock = xcb_get_keycodes(XK_Num_Lock);
    for (unsigned int i=0; i<8; i++)
       for (unsigned int j=0; j<reply->keycodes_per_modifier; j++) {
           xcb_keycode_t keycode = modmap[i * reply->keycodes_per_modifier + j];
           if (keycode == XCB_NO_SYMBOL) continue;
           for (unsigned int n=0; numlock[n] != XCB_NO_SYMBOL; n++)
               if (numlock[n] == keycode) {
                   DEBUGP("xcb: found num-lock %d\n", 1 << i);
                   numlockmask = 1 << i;
                   break;
               }
       }

    return 0;
}

// set initial values
// root window - screen height/width - atoms - xerror handler
// set masks for reporting events handled by the wm
// and propagate the suported net atoms
int setup(int default_screen) {
    sigchld();
    screen = xcb_screen_of_display(dis, default_screen);
    if (!screen) err(EXIT_FAILURE, "error: cannot aquire screen\n");
    
    randrbase = setuprandr();
    //DEBUG("exited setuprandr, continuing setup\n");

    selmon = mons; 
    for (unsigned int i=0; i<DESKTOPS; i++)
        desktops[i] = (desktop){ .mode = DEFAULT_MODE, .direction = DEFAULT_DIRECTION, .showpanel = SHOW_PANEL, .gap = GAP, .count = 0, };

    win_focus   = getcolor(FOCUS);
    win_unfocus = getcolor(UNFOCUS);
    win_outer   = getcolor(OTRBRDRCOL);
    win_flt     = getcolor(FLTBRDCOL);

    #if MENU
    // initialize the menu 
    Menu *m = NULL;
    Menu *itr = NULL;
    char **menulist[] = MENUS;
    for (int i = 0; menulist[i]; i++) {
        m = createmenu(menulist[i]);
        if (!menus)
            menus = m;
        else {
            for (itr = menus; itr->next; itr = itr->next); 
            itr->next = m;
        }
    }

    initializexresources();
    #endif

    /* setup keyboard */
    if (setup_keyboard() == -1)
        err(EXIT_FAILURE, "error: failed to setup keyboard\n");

    /* set up atoms for dialog/notification windows */
    char *WM_ATOM_NAME[]   = { "WM_PROTOCOLS", "WM_DELETE_WINDOW" };
    char *NET_ATOM_NAME[]  = { "_NET_SUPPORTED", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE", "_NET_WM_NAME", "_NET_ACTIVE_WINDOW" };
    xcb_get_atoms(WM_ATOM_NAME, wmatoms, WM_COUNT);
    xcb_get_atoms(NET_ATOM_NAME, netatoms, NET_COUNT);

    /* check if another wm is running */
    if (xcb_checkotherwm())
        err(EXIT_FAILURE, "error: other wm is running\n");

    /* initialize EWMH */
    ewmh = malloc_safe(sizeof(xcb_ewmh_connection_t));
    if (!ewmh)
        err(EXIT_FAILURE, "error: failed to set ewmh atoms\n");
    xcb_ewmh_init_atoms_replies(ewmh, xcb_ewmh_init_atoms(dis, ewmh), (void *)0);

    xcb_change_property(dis, XCB_PROP_MODE_REPLACE, screen->root, netatoms[NET_SUPPORTED], XCB_ATOM_ATOM, 32, NET_COUNT, netatoms);
    grabkeys();

    /* set events */
    for (unsigned int i=0; i<XCB_NO_OPERATION; i++) events[i] = NULL;
    events[XCB_BUTTON_PRESS]                = buttonpress;
    events[XCB_CLIENT_MESSAGE]              = clientmessage;
    events[XCB_CONFIGURE_REQUEST]           = configurerequest;
    //events[XCB_CONFIGURE_NOTIFY]            = configurenotify;
    events[XCB_DESTROY_NOTIFY]              = destroynotify;
    events[XCB_ENTER_NOTIFY]                = enternotify;
    #if PRETTY_PRINT
    events[XCB_EXPOSE]                      = expose;
    #endif
    events[XCB_FOCUS_IN]                    = focusin;
    events[XCB_KEY_PRESS|XCB_KEY_RELEASE]   = keypress;
    events[XCB_MAPPING_NOTIFY]              = mappingnotify;
    events[XCB_MAP_REQUEST]                 = maprequest;
    events[XCB_PROPERTY_NOTIFY]             = propertynotify;
    events[XCB_UNMAP_NOTIFY]                = unmapnotify;
    events[XCB_NONE]                        = NULL;

    //DEBUG("setup: about to switch to default desktop\n");
    if (DEFAULT_DESKTOP >= 0 && DEFAULT_DESKTOP < DESKTOPS)
        change_desktop(&(Arg){.i = DEFAULT_DESKTOP});
    
    // new pipe to messenger, panel, dzen
    #if PRETTY_PRINT
    updatews();
    updatemode();
    updatedir();
    
    int pfds[2]; 

    if (pipe(pfds) < 0) {
        perror("pipe failed");
        return EXIT_FAILURE;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return EXIT_FAILURE;
    } else if (pid == 0) { // child
        close(pfds[0]); // close unused read end
        // set write end of pipe as stdout for this child process
        dup2(pfds[1], STDOUT_FILENO);
        //pp_out = fdopen(pfds[1], "w");
        close(pfds[1]);

        desktopinfo();
    } else /* if (pid > 0) */ { // parent
        char *args[] = PP_CMD;
        close(pfds[1]); // close unused write end
        // set read end of pipe as stdin for this process
        dup2(pfds[0], STDIN_FILENO);
        //pp_in = fdopen(pfds[0], "r");
        close(pfds[0]); // already redirected to stdin

        execvp(args[0], args);
        perror("exec failed");
        exit(EXIT_FAILURE);
    }
    #endif

    return 0;
}

int setuprandr(void) { // Set up RANDR extension. Get the extension base and subscribe to
    // events.
    const xcb_query_extension_reply_t *extension = xcb_get_extension_data(dis, &xcb_randr_id);
    if (!extension->present) return -1;
    else getrandr();
    int base = extension->first_event;
    xcb_randr_select_input(dis, screen->root,XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
            XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE |XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
            XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);
    return base;
}

void sigchld() {
    if (signal(SIGCHLD, sigchld) == SIG_ERR)
        err(EXIT_FAILURE, "cannot install SIGCHLD handler");
    while(0 < waitpid(-1, NULL, WNOHANG));
}

// execute a command
void spawn(const Arg *arg) {
    if (fork()) return;
    if (dis) close(screen->root);
    setsid();
    execvp((char*)arg->com[0], (char**)arg->com);
    fprintf(stderr, "error: execvp %s", (char *)arg->com[0]);
    perror(" failed"); // also prints the err msg
    exit(EXIT_SUCCESS);
}

void splitwindows(client *n, client *o, const desktop *d, const monitor *m)
{
    switch(d->direction) {
        case TBOTTOM:
            n->xp = o->xp;
            n->yp = o->yp + (o->hp / 2);
            n->wp = o->wp;
            n->hp = (o->yp + o->hp) - n->yp;

            o->hp = n->yp - o->yp;
            break;

        case TLEFT:
            n->xp = o->xp;
            n->yp = o->yp;
            n->wp = o->wp / 2;
            n->hp = o->hp;

            o->xp = n->xp + n->wp;
            o->wp = (n->xp + o->wp) - o->xp;
            break;

        case TRIGHT:
            n->xp = o->xp + (o->wp / 2);
            n->yp = o->yp;
            n->wp = (o->xp + o->wp) - n->xp;
            n->hp = o->hp;
            
            o->wp = n->xp - o->xp;
            break;

        case TTOP:
            n->xp = o->xp;
            n->yp = o->yp;
            n->wp = o->wp;
            n->hp = o->hp / 2;

            o->yp = n->yp + n->hp;
            o->hp = (n->yp + o->hp) - o->yp;
            
            break;

        default:
            break;
    }

    if(m) {
        SETWINDOW(o, d, m);
        SETWINDOW(n, d, m);
    }
}

// switch the tiling direction
void switch_direction(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    if (d->mode != TILE) {
        d->mode = TILE;
        retile(d, selmon);
    }
    if (d->direction != arg->i) d->direction = arg->i;
    #if PRETTY_PRINT
    updatemode();
    updatedir();
    desktopinfo();
    #endif
}

// switch the tiling mode or to floating mode,
void switch_mode(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    if (d->mode != arg->i) d->mode = arg->i;
    retile(d, selmon); // we need to retile when switching from video/monocle to tile/float
    #if PRETTY_PRINT
    updatemode();
    desktopinfo();
    #endif
}

#if MENU
void text_draw (xcb_gcontext_t gc, xcb_window_t window, int16_t x1, int16_t y1, const char *label) {
    //xcb_void_cookie_t    cookie_gc;
    xcb_void_cookie_t    cookie_text;
    xcb_generic_error_t *error;
    uint8_t              length;

    length = strlen(label);

    cookie_text = xcb_image_text_8_checked(dis, length, window, gc, x1, y1, label);
    error = xcb_request_check(dis, cookie_text);
    if (error) {
        fprintf(stderr, "ERROR: can't paste text : %d\n", error->error_code);
        xcb_disconnect(dis);
        exit(-1);
    }
}
#endif

//TDOD: we need to make sure we arent splitting a floater
void tilenew(client *n, client *o, desktop *d, const monitor *m) {
    if (ISFT(n)) {
        xcb_move_resize(n, d, m);
        xcb_raise_window(n->win);
    } else {
        d->count++;
        if (d->count == 1) {
            DEBUG("tilenew: tiling empty monitor\n");
            n->xp = 0; n->yp = 0; n->wp = 100; n->hp = 100; 
            if (m) {
                SETWINDOW(n, d, m);
                if (d->mode == VIDEO) {
                    xcb_move_resize_monocle(n, d, m);
                    xcb_raise_window(n->win);
                } else {
                    xcb_move_resize(n, d, m); 
                    xcb_lower_window(n->win);
                }
            } else xcb_unmap_window(dis, n->win);
        } else {
            if(o->isfloating)
                o = clientbehindfloater(d);
            splitwindows(n, o, d, m);
            if (m) { 
                if (d->mode != MONOCLE && d->mode != VIDEO) {
                    xcb_move_resize(n, d, m);
                    xcb_lower_window(n->win);

                    xcb_move_resize(o, d, m);
                    xcb_lower_window(o->win);
                }
                else monocle(d, m);
            } else xcb_unmap_window(dis, n->win);
        }
    }
}

void tileremove(client *r, desktop *d, const monitor *m) {
    d->count--;
    DEBUGP("tileremove: d->count = %d\n", d->count);

    client **l;

    if((l = clientstotheleft(r, d, true)))
        for(int i = 0; l[i]; i++) {
            l[i]->wp += r->wp;
            if(m) {
                SETWINDOW(l[i], d, m);
                if(d->mode == TILE || d->mode == FLOAT)
                    xcb_move_resize(l[i], d, m);
            }
        }
    else if((l = clientstothetop(r, d, true)))
        for(int i = 0; l[i]; i++) {
            l[i]->hp += r->hp;
            if(m) {
                SETWINDOW(l[i], d, m);
                if(d->mode == TILE || d->mode == FLOAT)
                    xcb_move_resize(l[i], d, m);
            }
        }
    else if((l = clientstotheright(r, d, true)))
        for(int i = 0; l[i]; i++) {
            l[i]->xp = r->xp;
            l[i]->wp += r->wp;
            if(m) {
                SETWINDOW(l[i], d, m);
                if(d->mode == TILE || d->mode == FLOAT)
                    xcb_move_resize(l[i], d, m);
            }
        }
    else if((l = clientstothebottom(r, d, true)))
        for(int i = 0; l[i]; i++) {
            l[i]->yp = r->yp;
            l[i]->hp += r->hp;
            if(m) {
                SETWINDOW(l[i], d, m);
                if(d->mode == TILE || d->mode == FLOAT)
                    xcb_move_resize(l[i], d, m);
            }
        }

    free(l);
    DEBUG("tileremove: leaving\n");
}

// windows that request to unmap should lose their
// client, so no invisible windows exist on screen
void unmapnotify(xcb_generic_event_t *e) {
    xcb_unmap_notify_event_t *ev = (xcb_unmap_notify_event_t *)e;
    client *c = wintoclient(ev->window); 
    if (c){
        monitor *m = wintomon(ev->window);
        if(m)
            removeclient(c, &desktops[m->curr_dtop], m, false);
    }
    #if PRETTY_PRINT
    desktopinfo();
    #endif
}

#if PRETTY_PRINT
void updatedir() {
    desktop *d = &desktops[selmon->curr_dtop];
    char *tags_dir[] = PP_TAGS_DIR;
    char temp[512];
     
    if (tags_dir[d->direction]) {
        snprintf(temp, 512, "^fg(%s)%s ", PP_COL_DIR, tags_dir[d->direction]);
        pp.dir = realloc(pp.dir, strlen(temp));
        sprintf(pp.dir, "^fg(%s)%s ", PP_COL_DIR, tags_dir[d->direction]);
    } else {
        snprintf(temp, 512, "^fg(%s)%d ", PP_COL_DIR, d->direction);
        pp.dir = realloc(pp.dir, strlen(temp));
        sprintf(pp.dir, "^fg(%s)%d ", PP_COL_DIR, d->direction);
    }
}

void updatemode() {
    desktop *d = &desktops[selmon->curr_dtop];
    char *tags_mode[] = PP_TAGS_MODE;
    char temp[512];
    
    if (tags_mode[d->mode]) {
        snprintf(temp, 512, "^fg(%s)%s ", PP_COL_MODE, tags_mode[d->mode]);
        pp.mode = realloc(pp.mode, strlen(temp));
        sprintf(pp.mode, "^fg(%s)%s ", PP_COL_MODE, tags_mode[d->mode]);
    } else {
        snprintf(temp, 512, "^fg(%s)%d ", PP_COL_MODE, d->mode);
        pp.mode = realloc(pp.mode, strlen(temp));
        sprintf(pp.mode, "^fg(%s)%d ", PP_COL_MODE, d->mode);
    }
}

void updatetitle(client *c) {
    xcb_icccm_get_text_property_reply_t reply;
    xcb_generic_error_t *err = NULL;

    if(!xcb_icccm_get_text_property_reply(dis, xcb_icccm_get_text_property(dis, c->win, netatoms[NET_WM_NAME]), &reply, &err))
        if(!xcb_icccm_get_text_property_reply(dis, xcb_icccm_get_text_property(dis, c->win, XCB_ATOM_WM_NAME), &reply, &err))
            return;

    if(err) {
        DEBUG("updatetitle: leaving, error\n");
        free(err);
        return;
    }

    // TODO: encoding
    if(!reply.name || !reply.name_len)
        return;
     
    free(c->title);
    c->title = malloc_safe(reply.name_len+1);  
    strncpy(c->title, reply.name, reply.name_len); 
    xcb_icccm_get_text_property_reply_wipe(&reply);
}

void updatews() { 
    char *tags_ws[] = PP_TAGS_WS;
    char t1[512] = { "" };
    char t2[512] = { "" };

    for (int i = 0; i < DESKTOPS; i++) {
        if (tags_ws[i])
            snprintf(t2, 512, "^fg(%s)%s ", 
                    i == selmon->curr_dtop ? PP_COL_CURRENT : desktops[i].head ? PP_COL_VISIBLE : PP_COL_HIDDEN, 
                    tags_ws[i]);
        else 
            snprintf(t2, 512, "^fg(%s)%d ", 
                    i == selmon->curr_dtop ? PP_COL_CURRENT : desktops[i].head ? PP_COL_VISIBLE : PP_COL_HIDDEN,
                    i + 1);
        strncat(t1, t2, strlen(t2)); 
    }
    pp.ws = (char *)realloc(pp.ws, strlen(t1) + 1);
    strncpy(pp.ws, t1, strlen(t1));
}
#endif

// find which client the given window belongs to
client *wintoclient(xcb_window_t w) {
    client *c = NULL;

    for (int i = 0; i < DESKTOPS; i++)
        for (c = desktops[i].head; c; c = c->next) {
            if(c->win == w) {
                DEBUG("wintoclient: leaving, returning found client\n");
                return c;
            }
        }
   
    DEBUG("wintoclient: leaving, returing NULL\n");
    return NULL;
}

// find which monitor the given window belongs to
monitor *wintomon(xcb_window_t w) {
    int x, y;
    client *c;

    if(w == screen->root && getrootptr(&x, &y)) {
        DEBUG("wintomon: leaving, returning ptrtomon\n");
        return ptrtomon(x, y);
    }
     
    for (monitor *m = mons; m; m = m->next)
        for (c = desktops[m->curr_dtop].head; c; c = c->next)
            if(c->win == w) {
                DEBUG("wintomon: leaving, returning found monitor\n");
                return m;
            }
    
    DEBUG("wintomon: leaving, returning NULL monitor\n");
    return NULL;
}

int main(int argc, char *argv[]) {
    int default_screen;
    if (argc == 2 && argv[1][0] == '-') {
        switch (argv[1][1]) { 
            case 'v': errx(EXIT_SUCCESS, "by dct");
            case 'h': errx(EXIT_SUCCESS, "%s", USAGE);
            default: errx(EXIT_FAILURE, "%s", USAGE);
        }
    } else if (argc != 1) 
        errx(EXIT_FAILURE, "%s", USAGE);
    if (xcb_connection_has_error((dis = xcb_connect(NULL, &default_screen))))
        errx(EXIT_FAILURE, "error: cannot open display\n");
    if (setup(default_screen) != -1) {
      #if PRETTY_PRINT
      desktopinfo(); // zero out every desktop on (re)start
      #endif
      run();
    }
    cleanup(); 
    return retval;
}

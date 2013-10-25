/* see license for copyright and license */

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

/* TODO: Reduce SLOC */

/* set this to 1 to enable debug prints */
#if 0
#  define DEBUG(x)      puts(x);
#  define DEBUGP(x,...) printf(x, ##__VA_ARGS__);
#else
#  define DEBUG(x)      ;
#  define DEBUGP(x,...) ;
#endif

/* upstream compatility */
#define XCB_MOVE_RESIZE XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT

#define LENGTH(x) (sizeof(x)/sizeof(*x))
#define BUTTONMASK      XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE
#define INRECT(X,Y,RX,RY,RW,RH) ((X) >= (RX) && (X) < (RX) + (RW) && (Y) >= (RY) && (Y) < (RY) + (RH))
#define ISFFT(c)        (c->isfullscrn || c->isfloating || c->istransient)

enum { RESIZE, MOVE };
enum { TILE, MONOCLE, VIDEO, FLOAT };
enum { TLEFT, TRIGHT, TBOTTOM, TTOP, TDIRECS };
enum { WM_PROTOCOLS, WM_DELETE_WINDOW, WM_COUNT };
enum { NET_SUPPORTED, NET_FULLSCREEN, NET_WM_STATE, NET_ACTIVE, NET_WM_NAME, NET_COUNT };

// define behavior of certain applications
typedef struct {
    const char *class;              // the class or name of the instance
    const int desktop;              // what desktop it should be spawned at
    const bool follow, floating;    // whether to change desktop focus to the specified desktop
} AppRule;

/* a client is a wrapper to a window that additionally
 * holds some properties for that window
 *
 * isurgent    - set when the window received an urgent hint
 * istransient - set when the window is transient
 * isfullscrn  - set when the window is fullscreen
 * isfloating  - set when the window is floating
 *
 * istransient is separate from isfloating as floating window can be reset
 * to their tiling positions, while the transients will always be floating
 */
typedef struct client {
    struct client *next;                                // the client after this one, or NULL if the current is the last client
    int x, y, w, h;                                     // actual window size
    float xp, yp, wp, hp;                               // percent of monitor, before adjustment
    int gapx, gapy, gapw, gaph;                         // gap sizes
    bool isurgent, istransient, isfullscrn, isfloating; //
    xcb_window_t win;                                   // the window this client is representing
    char title[256];
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
    client *head, *current, *prevfocus, *dead;
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
    const char** com;                                                               // a command to run
    const int i;                                                                    // an integer to indicate different states
    const float d;                                                                  // a float to do stuff with
    void (*m)(int*, client*, client**, desktop*);                                   // for the move client command
    void (*r)(desktop*, const int, int*, const float, client*, monitor*, client**); // for the resize client command
    char **list;                                                                    // list for menus
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

/* Exposed function prototypes sorted alphabetically */

static void change_desktop(const Arg *arg);
static void client_to_desktop(const Arg *arg);
static void decreasegap(const Arg *arg);
static void focusurgent();
static void increasegap(const Arg *arg);
static void killclient();
static void launchmenu(const Arg *arg);
static void moveclient(const Arg *arg);
static void moveclientup(int *num, client *c, client **list, desktop *d);
static void moveclientleft(int *num, client *c, client **list, desktop *d);
static void moveclientdown(int *num, client *c, client **list, desktop *d);
static void moveclientright(int *num, client *c, client **list, desktop *d);
static void movefocus(const Arg *arg);
static void mousemotion(const Arg *arg);
static void pushtotiling();
static void quit(const Arg *arg);
static void resizeclient(const Arg *arg);
static void resizeclientbottom(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list);
static void resizeclientleft(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list);
static void resizeclientright(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list);
static void resizeclienttop(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list);
static void rotate(const Arg *arg);
static void rotate_filled(const Arg *arg);
static void spawn(const Arg *arg);
static void switch_mode(const Arg *arg);
static void switch_direction(const Arg *arg);
static void togglepanel();

#include "config.h"

/* Hidden function prototypes sorted alphabetically */
static client* addwindow(xcb_window_t w, desktop *d);
static void adjustclientgaps(const int gap, client *c);
static void buttonpress(xcb_generic_event_t *e);
static void cleanup(void);
static void clientmessage(xcb_generic_event_t *e);
static bool clientstouchingbottom(desktop *d, client *c, client **list, int *num);
static bool clientstouchingleft(desktop *d, client *c, client **list, int *num);
static bool clientstouchingright(desktop *d, client *c, client **list, int *num);
static bool clientstouchingtop(desktop *d, client *c, client **list, int *num);
static desktop *clienttodesktop(client *c);
static void configurerequest(xcb_generic_event_t *e);
static monitor* createmon(xcb_randr_output_t id, int x, int y, int w, int h, int dtop);
static void deletewindow(xcb_window_t w);
static void desktopinfo(void);
static void destroynotify(xcb_generic_event_t *e);
static void enternotify(xcb_generic_event_t *e);
static void expose(xcb_generic_event_t *e);
static void focus(client *c, desktop *d);
static void focusin(xcb_generic_event_t *e);
static unsigned int getcolor(char* color);
static bool getrootptr(int *x, int *y);
static void gettitle(client *c);
static void grabbuttons(client *c);
static void grabkeys(void);
static void growbyh(client *match, const float size, client *c, monitor *m);
static void growbyw(client *match, const float size, client *c, monitor *m);
static void growbyx(client *match, const float size, client *c, monitor *m);
static void growbyy(client *match, const float size, client *c, monitor *m);
static void initializexresources();
static void keypress(xcb_generic_event_t *e);
static void mappingnotify(xcb_generic_event_t *e);
static void maprequest(xcb_generic_event_t *e);
static void monocle(int x, int y, int w, int h, const desktop *d, const monitor *m);
static client* prev_client(client *c, desktop *d);
static void propertynotify(xcb_generic_event_t *e);
static monitor* ptrtomon(int x, int y);
static void removeclient(client *c, desktop *d, const monitor *m);
static void retile(desktop *d, const monitor *m);
static void run(void);
static void setborders(desktop *d);
static int setup(int default_screen);
static int  setuprandr(void);
static void shrinkbyh(client *match, const float size, client *c, monitor *m);
static void shrinkbyw(client *match, const float size, client *c, monitor *m);
static void shrinkbyx(client *match, const float size, client *c, monitor *m);
static void shrinkbyy(client *match, const float size, client *c, monitor *m);
static void sigchld();
static void text_draw(unsigned int color, xcb_window_t window, int16_t x1, int16_t y1, const char *label);
static void tilenew(desktop *d, const monitor *m);
static void tilenewbottom(client *n, client *c);
static void tilenewleft(client *n, client *c);
static void tilenewright(client *n, client *c);
static void tilenewtop(client *n, client *c);
static void tileremove(desktop *d, const monitor *m);
static void unmapnotify(xcb_generic_event_t *e);
static client *wintoclient(xcb_window_t w);
static monitor *wintomon(xcb_window_t w);

/* variables */
static bool running = true;
static int randrbase, retval = 0, nmons = 0;
static unsigned int numlockmask = 0, win_unfocus, win_focus, win_outer, win_urgent;
static xcb_connection_t *dis;
static xcb_screen_t *screen;
static xcb_atom_t wmatoms[WM_COUNT], netatoms[NET_COUNT];
static desktop desktops[DESKTOPS];
static monitor *mons = NULL, *selmon = NULL;
static Menu *menus = NULL;
static Xresources xres;

/* events array
 * on receival of a new event, call the appropriate function to handle it
 */
static void (*events[XCB_NO_OPERATION])(xcb_generic_event_t *e);

static void* malloc_safe(size_t size)
{
    void *ret;
    if(!(ret = malloc(size)))
        puts("malloc_safe: fatal: could not malloc()");
    memset(ret, 0, size);
    return ret;
}

static bool (*findtouchingclients[TDIRECS])(desktop *d, client *c, client **list, int *num) = {
    [TBOTTOM] = clientstouchingbottom, [TLEFT] = clientstouchingleft, [TRIGHT] = clientstouchingright, [TTOP] = clientstouchingtop,
};

static void (*tiledirection[TDIRECS])(client *n, client *c) = {
    [TBOTTOM] = tilenewbottom, [TLEFT] = tilenewleft, [TRIGHT] = tilenewright, [TTOP] = tilenewtop,
};

/* wrapper to move and resize window */
static inline void xcb_move_resize(xcb_connection_t *con, xcb_window_t win, int x, int y, int w, int h) {
    unsigned int pos[4] = { x, y, w, h };
    xcb_configure_window(con, win, XCB_MOVE_RESIZE, pos);
}

/* wrapper to raise window */
static inline void xcb_raise_window(xcb_connection_t *con, xcb_window_t win) {
    unsigned int arg[1] = { XCB_STACK_MODE_ABOVE };
    xcb_configure_window(con, win, XCB_CONFIG_WINDOW_STACK_MODE, arg);
}

/* wrapper to get xcb keycodes from keysymbol */
static xcb_keycode_t* xcb_get_keycodes(xcb_keysym_t keysym) {
    xcb_key_symbols_t *keysyms;
    xcb_keycode_t     *keycode;

    if (!(keysyms = xcb_key_symbols_alloc(dis))) return NULL;
    keycode = xcb_key_symbols_get_keycode(keysyms, keysym);
    xcb_key_symbols_free(keysyms);

    return keycode;
}

/* vim: set ts=4 sw=4 :*/

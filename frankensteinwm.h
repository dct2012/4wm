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

// COMMANDS
extern void change_desktop(const Arg *arg);
extern void client_to_desktop(const Arg *arg);
extern void decreasegap(const Arg *arg);
extern void focusurgent();
extern void increasegap(const Arg *arg);
extern void killclient();
extern void launchmenu(const Arg *arg);
extern void moveclient(const Arg *arg);
extern void moveclientup(int *num, client *c, client **list, desktop *d);
extern void moveclientleft(int *num, client *c, client **list, desktop *d);
extern void moveclientdown(int *num, client *c, client **list, desktop *d);
extern void moveclientright(int *num, client *c, client **list, desktop *d);
extern void movefocus(const Arg *arg);
extern void mousemotion(const Arg *arg);
extern void pushtotiling();
extern void quit(const Arg *arg);
extern void resizeclient(const Arg *arg);
extern void resizeclientbottom(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list);
extern void resizeclientleft(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list);
extern void resizeclientright(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list);
extern void resizeclienttop(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list);
extern void rotate(const Arg *arg);
extern void rotate_filled(const Arg *arg);
extern void spawn(const Arg *arg);
extern void switch_mode(const Arg *arg);
extern void switch_direction(const Arg *arg);
extern void togglepanel();

#include "config.h"

// UTILITIES
extern void adjustclientgaps(const int gap, client *c);
extern bool clientstouchingbottom(desktop *d, client *c, client **list, int *num);
extern bool clientstouchingleft(desktop *d, client *c, client **list, int *num);
extern bool clientstouchingright(desktop *d, client *c, client **list, int *num);
extern bool clientstouchingtop(desktop *d, client *c, client **list, int *num);
extern void deletewindow(xcb_window_t w);
extern void desktopinfo(void);
extern void focus(client *c, desktop *d);
extern bool getrootptr(int *x, int *y);
extern void grabbuttons(client *c);
extern void grabkeys(void);
extern void* malloc_safe(size_t size);
extern client* prev_client(client *c, desktop *d);
extern void setborders(desktop *d);
extern monitor *wintomon(xcb_window_t w);
extern xcb_keycode_t* xcb_get_keycodes(xcb_keysym_t keysym);

// EVENTS
extern void buttonpress(xcb_generic_event_t *e);
extern void clientmessage(xcb_generic_event_t *e);
extern void configurerequest(xcb_generic_event_t *e);
extern void destroynotify(xcb_generic_event_t *e);
extern void enternotify(xcb_generic_event_t *e);
extern void expose(xcb_generic_event_t *e);
extern void focusin(xcb_generic_event_t *e);
extern void keypress(xcb_generic_event_t *e);
extern void mappingnotify(xcb_generic_event_t *e);
extern void maprequest(xcb_generic_event_t *e);
extern void propertynotify(xcb_generic_event_t *e);
extern void unmapnotify(xcb_generic_event_t *e);

// TILING
extern void monocle(int x, int y, int w, int h, const desktop *d, const monitor *m);
extern void retile(desktop *d, const monitor *m);
extern void tilenew(desktop *d, const monitor *m);
extern void tilenewbottom(client *n, client *c);
extern void tilenewleft(client *n, client *c);
extern void tilenewright(client *n, client *c);
extern void tilenewtop(client *n, client *c);
extern void tileremove(desktop *d, const monitor *m);

/* vim: set ts=4 sw=4 :*/

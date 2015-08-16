#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/randr.h>
#include <X11/keysym.h>
#include <X11/Xresource.h>


// MACROS
#if 1
#  define DEBUG(x)      fputs(x, stderr);
#  define DEBUGP(x,...) fprintf(stderr, x, ##__VA_ARGS__);
#else
#  define DEBUG(x)      ;
#  define DEBUGP(x,...) ;
#endif

#define XCB_MOVE_RESIZE XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT

#define LENGTH(x) (sizeof(x)/sizeof(*x))


// ENUMS
enum { TILE, MONOCLE, VIDEO, FLOAT };
enum { TLEFT, TRIGHT, TBOTTOM, TTOP, TDIRECS };
enum { WM_PROTOCOLS, WM_DELETE_WINDOW, WM_COUNT };
//enum { NET_SUPPORTED, NET_FULLSCREEN, NET_WM_STATE, NET_ACTIVE, NET_WM_NAME, NET_COUNT };


// TYPES
typedef struct window {
    struct window *next;                                // the client after this one, or NULL if the current is the last client
    int x, y, w, h;                                     // actual window size
    int xp, yp, wp, hp;                                 // percent of monitor, before adjustment (percent is a int from 0-100)
    int gapx, gapy, gapw, gaph;                         // gap sizes
    bool isurgent, istransient, isfloating;             // property flags
    xcb_window_t win;                                   // the window this client is representing
    char *title;
} window;

typedef struct {
    int mode, gap, direction, count;
    window *head, *current, *prevfocus;
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
    const int p;                                                                    // represents a percentage for resizing
    void (*m)(int*, window*, window**, desktop*);                                   // for the move client command
    void (*r)(desktop*, const int, int*, const int, window*, monitor*, window**);   // for the resize client command
    char **list;                                                                    // list for menus
} Arg;

// a key struct represents a combination of
typedef struct {
    unsigned int mod;           // a modifier mask
    xcb_keysym_t keysym;        // and the key pressed
    void (*func)(const Arg *);  // the function to be triggered because of the above combo
    const Arg arg;              // the argument to the function
} Key;


// FOWARD DECLARATIONS
void deletewindow(window *r, desktop *d);
void killwindow();
void* malloc_safe(size_t size);
window* prev_window(window *w, desktop *d);
void quit();
void setup_desktops();
void setup_events();
bool setup_keyboard();
void setup_monitors();
void sigchld();
void spawn(const Arg *arg);
void tilenew(window *n, desktop *d, monitor *m);
void tileremove(window *r, desktop *d);
void unmapnotify(xcb_generic_event_t *e);
window** windowstotheleft(window *w, desktop *d);
window *wintowin(xcb_window_t w);
int xcb_checkotherwm();
xcb_screen_t *xcb_screen_of_display(xcb_connection_t *con, int screen);


#include "config.h"


// VARIABLES
int nmons = 0;
bool running = true;
xcb_connection_t *con;
xcb_screen_t *screen;
monitor *mons = NULL, *selmon = NULL;
xcb_atom_t wmatoms[WM_COUNT]/*, netatoms[NET_COUNT]*/;
static desktop desktops[DESKTOPS];
void (*events[XCB_NO_OPERATION])(xcb_generic_event_t *e);


// WRAPPERS
// wrapper to get atoms using xcb
void xcb_get_atoms(char **names, xcb_atom_t *atoms, unsigned int count)
{
    xcb_intern_atom_cookie_t cookies[count];
    xcb_intern_atom_reply_t  *reply;

    for (unsigned int i = 0; i < count; i++) 
        cookies[i] = xcb_intern_atom(con, 0, strlen(names[i]), names[i]);
    
    for (unsigned int i = 0; i < count; i++) {
        reply = xcb_intern_atom_reply(con, cookies[i], NULL); // TODO: Handle error
        if (reply) {
            DEBUGP("%s : %d\n", names[i], reply->atom);
            atoms[i] = reply->atom; 
            free(reply);
        } else puts("WARN: 4wm failed to register %s atom.\nThings might not work right.");
    }
}

// wrapper to window get attributes using xcb */
xcb_get_window_attributes_reply_t* xcb_get_attributes(xcb_window_t w) 
{
    xcb_get_window_attributes_cookie_t cookie;
    cookie = xcb_get_window_attributes(con, w);
    return xcb_get_window_attributes_reply(con, cookie, NULL); // TODO: Handle error
}

// wrapper to get xcb keycodes from keysymbol
xcb_keycode_t* xcb_get_keycodes(xcb_keysym_t keysym) 
{
    xcb_key_symbols_t *keysyms;
    xcb_keycode_t     *keycode;

    if (!(keysyms = xcb_key_symbols_alloc(con)))
        return NULL;
    keycode = xcb_key_symbols_get_keycode(keysyms, keysym);
    xcb_key_symbols_free(keysyms);

    return keycode;
}

xcb_keysym_t xcb_get_keysym(xcb_keycode_t keycode) 
{
    xcb_key_symbols_t *keysyms;
    xcb_keysym_t       keysym;

    if (!(keysyms = xcb_key_symbols_alloc(con))) 
        return 0;
    keysym = xcb_key_symbols_get_keysym(keysyms, keycode, 0);
    xcb_key_symbols_free(keysyms);

    return keysym;
}

/* wrapper to move and resize window */
inline void xcb_move_resize(xcb_connection_t *con, window *w) 
{
    printf("xcb_move_resize: x: %d, y: %d, w: %d, h: %d\n", w->x, w->y, w->w, w->h);
    unsigned int pos[4] = { w->x, w->y, w->w, w->h };
    xcb_configure_window(con, w->win, XCB_MOVE_RESIZE, pos);
}

// wrapper to lower window
inline void xcb_lower_window(xcb_connection_t *con, window *w) 
{
    unsigned int arg[1] = { XCB_STACK_MODE_BELOW };
    xcb_configure_window(con, w->win, XCB_CONFIG_WINDOW_STACK_MODE, arg);
}

// wrapper to raise window
inline void xcb_raise_window(xcb_connection_t *con, window *w) 
{
    unsigned int arg[1] = { XCB_STACK_MODE_ABOVE };
    xcb_configure_window(con, w->win, XCB_CONFIG_WINDOW_STACK_MODE, arg);
}

// FUNCTION DEFINITIONS


void addwindow(xcb_window_t w, desktop *d, monitor *m) 
{
    window *c;
    if (!(c = (window *)malloc_safe(sizeof(window))))
        return;
    
    window *l;
    for(l = d->head; l && l->next; l = l->next);
    if(!l)
        d->head = d->current = c;
    else {
        l->next = c;
        d->prevfocus = d->current;
        d->current = c;
    }

    DEBUGP("addwindow: d->count = %d\n", d->count);

    unsigned int values[1] = { XCB_EVENT_MASK_PROPERTY_CHANGE|(FOLLOW_MOUSE?XCB_EVENT_MASK_ENTER_WINDOW:0) };
    xcb_change_window_attributes_checked(con, (c->win = w), XCB_CW_EVENT_MASK, values);
    
    if(d->mode == TILE)
        tilenew(c, d, m);
}

// on press of a button we should check if 
//      we should click to focus
//      or move/resize a floating window
void buttonpress(xcb_generic_event_t *e)
{
    xcb_button_press_event_t *ev = (xcb_button_press_event_t*)e;

    //check click to focus

    //check move/resize floater
}

void clean()
{
    // free each monitor
    monitor *m, *t;
    for (m = mons; m; m = t){
        t = m->next;
        free(m);
    }

    window *w;
    for (int i = 0; i < DESKTOPS; i++)
        for (w = desktops[i].head; w; w = w->next)
            deletewindow(w, &desktops[i]);
}

// we'll ignore this for now
void clientmessage(xcb_generic_event_t *e)
{
    xcb_client_message_event_t *ev = (xcb_client_message_event_t*)e;
}

// window wants to change it's geometry
//  for now we aren't going to let it,
//  so we'll fake it.
void configurerequest(xcb_generic_event_t *e)
{
    DEBUG("configurerequest\n");
    xcb_configure_request_event_t *ev = (xcb_configure_request_event_t*)e;

    window *w;
    if (!(w = wintowin(ev->window))) {
        unsigned int v[7];
        unsigned int i = 0;
        if (ev->value_mask & XCB_CONFIG_WINDOW_X)              v[i++] = ev->x;
        if (ev->value_mask & XCB_CONFIG_WINDOW_Y)              v[i++] = ev->y;
        if (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH)          v[i++] = ev->width;
        if (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT)         v[i++] = ev->height;
        if (ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)   v[i++] = ev->border_width;
        if (ev->value_mask & XCB_CONFIG_WINDOW_SIBLING)        v[i++] = ev->sibling;
        if (ev->value_mask & XCB_CONFIG_WINDOW_STACK_MODE)     v[i++] = ev->stack_mode;
        xcb_configure_window_checked(con, ev->window, ev->value_mask, v);
    } else 
        xcb_send_event(con, false, ev->window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char*)ev);

    xcb_flush(con);
}

monitor* createmon(xcb_randr_output_t id, int x, int y, int w, int h, int dtop)
{
    monitor *m = (monitor*)malloc_safe(sizeof(monitor));

    m->id = id;
    m->curr_dtop = (dtop - 1);
    m->haspanel = ((nmons == PANEL_MON) ? true:false);
    m->x = x;
    m->y = y + (m->haspanel && TOP_PANEL ? PANEL_HEIGHT:0);
    m->w = w;
    m->h = h - (m->haspanel && !TOP_PANEL ? PANEL_HEIGHT:0); 
    m->next = NULL;

    return m;
}

//TODO: finish this, make it less complicated
void deletewindow(window *r, desktop *d) 
{
    window **p = NULL;
    for (p = &d->head; *p && (*p != r); p = &(*p)->next);
    if (!p) 
        return; 
    else 
        *p = r->next;
    
    if (r == d->prevfocus) 
        d->prevfocus = prev_window(d->current, d);
    if (r == d->current) {
        d->current = d->prevfocus ? d->prevfocus:d->head;
        d->prevfocus = prev_window(d->current, d);
    }

    // handle retile
    // since we're only doing tiling right now
    // whichever window is next to it will gain its space
    tileremove(r, d);
  
    xcb_client_message_event_t ev;
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = r->win;
    ev.format = 32;
    ev.sequence = 0;
    ev.type = wmatoms[WM_PROTOCOLS];
    ev.data.data32[0] = wmatoms[WM_DELETE_WINDOW];
    ev.data.data32[1] = XCB_CURRENT_TIME;
    xcb_send_event(con, 0, r->win, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);

    xcb_unmap_window(con, r->win);

    free(r->title);
    free(r); 
    r = NULL; 
}

// window is being closed
//  we should delete it
void destroynotify(xcb_generic_event_t *e)
{
    DEBUG("destroynotify\n");
    xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t*)e;

    window *w;
    for (int i = 0; i < DESKTOPS; i++)
        for (w = desktops[i].head; w; w = w->next)
            if(w->win == ev->window) {
                deletewindow(w, &desktops[i]);
                return;
            }

}

// mouse has entered a different window, we should check
//      if follow mouse is selected and change focused window
void enternotify(xcb_generic_event_t *e)
{
    DEBUG("enternotify\n");
    xcb_enter_notify_event_t *ev = (xcb_enter_notify_event_t*)e;
}

// window thinks it needs to be redrawn (repainted)
void expose(xcb_generic_event_t *e)
{
    DEBUG("expose\n");
    xcb_expose_event_t *ev = (xcb_expose_event_t*)e;
}

// winodw wants focus or input focus
//      ignore for now
void focusin(xcb_generic_event_t *e)
{
    DEBUG("focusin\n");
    xcb_focus_in_event_t *ev = (xcb_focus_in_event_t*)e;
}

void getoutputs(xcb_randr_output_t *outputs, const int len, xcb_timestamp_t timestamp) 
{
    // Walk through all the RANDR outputs (number of outputs == len) there
    // was at time timestamp.
    xcb_randr_get_crtc_info_cookie_t icookie;
    xcb_randr_get_crtc_info_reply_t *crtc = NULL;
    xcb_randr_get_output_info_reply_t *output;
    xcb_randr_get_output_info_cookie_t ocookie[len];
    monitor *m;
    int i;
 
    // get output cookies
    for (i = 0; i < len; i++) 
        ocookie[i] = xcb_randr_get_output_info(con, outputs[i], timestamp);

    for (i = 0; i < len; i ++) { /* Loop through all outputs. */
        output = xcb_randr_get_output_info_reply(con, ocookie[i], NULL);

        if (output == NULL) 
            continue;

        if (XCB_NONE != output->crtc) {
            icookie = xcb_randr_get_crtc_info(con, output->crtc, timestamp);
            crtc    = xcb_randr_get_crtc_info_reply(con, icookie, NULL);

            if (NULL == crtc) 
                return; 

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
        free(output);
    }
}

void getrandr() // Get RANDR resources and figure out how many outputs there are.
{
    xcb_randr_get_screen_resources_current_cookie_t rcookie = xcb_randr_get_screen_resources_current(con, screen->root);
    xcb_randr_get_screen_resources_current_reply_t *res = xcb_randr_get_screen_resources_current_reply(con, rcookie, NULL);
    
    if (NULL == res) 
        return;

    xcb_timestamp_t timestamp = res->config_timestamp;
    int len = xcb_randr_get_screen_resources_current_outputs_length(res);
    xcb_randr_output_t *outputs = xcb_randr_get_screen_resources_current_outputs(res);
    /* Request information for all outputs. */
    getoutputs(outputs, len, timestamp);
    free(res);
}

void grabkeys() 
{
    xcb_keycode_t *keycode;
    xcb_ungrab_key(con, XCB_GRAB_ANY, screen->root, XCB_MOD_MASK_ANY);
    for (unsigned int i = 0; i < LENGTH(keys); i++) {
        keycode = xcb_get_keycodes(keys[i].keysym);
        for (unsigned int k = 0; keycode[k] != XCB_NO_SYMBOL; k++)
            xcb_grab_key(con, 1, screen->root, keys[i].mod, keycode[k], XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    }
}

// keys are being pressed. we need to check for
//      a command associated with that key press
void keypress(xcb_generic_event_t *e)
{
    xcb_key_press_event_t *ev = (xcb_key_press_event_t *)e;
    xcb_keysym_t keysym = xcb_get_keysym(ev->detail);

    for (unsigned int i = 0; i < LENGTH(keys); i++)
        if (keysym == keys[i].keysym && keys[i].func)
            keys[i].func(&keys[i].arg);
}

// if you find the window kill it
// else call xb_kill_client
void killwindow()
{
    DEBUG("killwindow\n");
    desktop *d = &desktops[selmon->curr_dtop];

    if (!d->current) 
        return;

    deletewindow(d->current, d);
}

void* malloc_safe(size_t size) 
{
    void *ret;
    if(!(ret = malloc(size)))
        puts("malloc_safe: fatal: could not malloc()");
    memset(ret, 0, size);
    return ret;
}

// ignore for now
void mappingnotify(xcb_generic_event_t *e)
{
    DEBUG("mappingnotify\n");
    xcb_mapping_notify_event_t *ev = (xcb_mapping_notify_event_t*)e;
}

// window wants to display itself. we need to check
//      if it already exists
//          if it does do nothing
//      else create a new window and tile or float it
void maprequest(xcb_generic_event_t *e)
{
    
    xcb_map_request_event_t *ev = (xcb_map_request_event_t*)e;

    xcb_get_window_attributes_reply_t *attr = NULL;
    attr = xcb_get_attributes(ev->window);
    if (!attr || attr->override_redirect) 
        return;

    window *w = wintowin(ev->window);
    if(w)
        return;
    DEBUG("maprequest\n");

    addwindow(ev->window, &desktops[selmon->curr_dtop], selmon);
}

// get the previous window from the given
// if no such window, return NULL
window* prev_window(window *w, desktop *d) 
{
    if (!w || !d->head->next)
        return NULL;
    window *p;
    for (p = d->head; p->next && p->next != w; p = p->next);
    return p;
}

// the windows properties have changed
//      likely just change desktop info
void propertynotify(xcb_generic_event_t *e)
{
    DEBUG("propertynotify\n");
    xcb_property_notify_event_t *ev = (xcb_property_notify_event_t*)e;
}

void quit()
{
    running = false;
}

void run()
{
    xcb_generic_event_t *e; 

    while(running) {
        xcb_flush(con);
        if(xcb_connection_has_error(con))
            return;
        if((e = xcb_wait_for_event(con))) {
            if(events[e->response_type & ~0x08])
                events[e->response_type & ~0x08](e);
            free(e);
        }
    }
}

bool setup()
{    
    sigchld();
    
    int default_screen;
    if(xcb_connection_has_error((con = xcb_connect(NULL, &default_screen))))
        return false;

    if(!(screen = xcb_screen_of_display(con, default_screen)))
        return false;

    setup_monitors();
    selmon->curr_dtop = 0; 

    setup_desktops();
    
    if(!setup_keyboard())
        return false;

    char *WM_ATOM_NAME[] = { "WM_PROTOCOLS", "WM_DELETE_WINDOW" };
    //char *NET_ATOM_NAME[]  = { "_NET_SUPPORTED", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE", 
    //                           "_NET_WM_NAME", "_NET_ACTIVE_WINDOW" };
    xcb_get_atoms(WM_ATOM_NAME, wmatoms, WM_COUNT);
    //xcb_get_atoms(NET_ATOM_NAME, netatoms, NET_COUNT);

    if (xcb_checkotherwm())
        return false;

    grabkeys();

    setup_events();

    return true;
}

void setup_events()
{
    for (int i = 0; i < XCB_NO_OPERATION; i++) 
        events[i] = NULL;

    events[XCB_BUTTON_PRESS]                = buttonpress;
    events[XCB_CLIENT_MESSAGE]              = clientmessage;
    events[XCB_CONFIGURE_REQUEST]           = configurerequest;
    events[XCB_DESTROY_NOTIFY]              = destroynotify;
    events[XCB_ENTER_NOTIFY]                = enternotify;
    events[XCB_EXPOSE]                      = expose;
    events[XCB_FOCUS_IN]                    = focusin;
    events[XCB_KEY_PRESS|XCB_KEY_RELEASE]   = keypress;
    events[XCB_MAPPING_NOTIFY]              = mappingnotify;
    events[XCB_MAP_REQUEST]                 = maprequest;
    events[XCB_PROPERTY_NOTIFY]             = propertynotify;
    events[XCB_UNMAP_NOTIFY]                = unmapnotify;
    events[XCB_NONE]                        = NULL;
}

void setup_desktops()
{
    for(int i = 0; i < DESKTOPS; i++)
        desktops[i] = (desktop){ .mode = DEFAULT_MODE, .direction = DEFAULT_DIRECTION, 
                                 .showpanel = SHOW_PANEL, .gap = GAP, .count = 0, };   
}

bool setup_keyboard()
{
    return true;
}

void sigchld()
{
    signal(SIGCHLD, sigchld);
    while(0 < waitpid(-1, NULL, WNOHANG));
}

void setup_monitors()
{
    const xcb_query_extension_reply_t *extension = xcb_get_extension_data(con, &xcb_randr_id);
    if (!extension->present)
        return;
    else getrandr();
    xcb_randr_select_input(con, screen->root, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
            XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE | XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
            XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);

    selmon = mons; 
}

void spawn(const Arg *arg)
{
    DEBUG("spawn\n");
    if (fork()) 
        return;

    if (con) 
        close(screen->root);

    setsid();
    execvp((char*)arg->com[0], (char**)arg->com);
    fprintf(stderr, "error: execvp %s", (char *)arg->com[0]);
    perror(" failed"); // also prints the err msg
    exit(EXIT_SUCCESS);
}

void splitwindows(window *n, window *o, desktop *d, monitor *m)
{
    switch(d->direction) {
        case TBOTTOM:
            n->xp = o->xp;
            n->yp = o->yp + (o->hp / 2);
            n->wp = o->wp;
            n->hp = (o->yp + o->hp) - n->yp;

            o->hp = n->yp - o->yp;
            o->h = (float)o->hp/100 * m->h;
            break;

        case TLEFT:
            n->xp = o->xp;
            n->yp = o->yp;
            n->wp = o->wp / 2;
            n->hp = o->hp;

            o->xp = n->xp + n->wp;
            o->x = m->x + (float)o->xp/100 * m->w;
            o->wp = (n->xp + o->wp) - o->xp;
            o->w = (float)o->wp/100 * m->w;
            break;

        case TRIGHT:
            n->xp = o->xp + (o->wp / 2);
            n->yp = o->yp;
            n->wp = (o->xp + o->wp) - n->xp;
            n->hp = o->hp;
            
            o->wp = n->xp - o->xp;
            o->w = (float)o->wp/100 * m->w;
            break;

        case TTOP:
            n->xp = o->xp;
            n->yp = o->yp;
            n->wp = o->wp;
            n->hp = o->hp / 2;

            o->yp = n->yp + n->hp;
            o->y = m->y + (float)o->yp/100 * m->h;
            o->hp = (n->yp + o->hp) - o->yp;
            o->h = (float)o->hp/100 * m->h;
            break;

        default:
            break;
    }

    n->x = m->x + (float)n->xp/100 * m->w;
    n->y = m->y + (float)n->yp/100 * m->h;
    n->w = (float)n->wp/100 * m->w;
    n->h = (float)n->hp/100 * m->h;
}

void tilenew(window *n, desktop *d, monitor *m)
{ 
    if(!d->prevfocus) { // it's the first
        DEBUG("tilenew first\n");
        n->xp = 0;
        n->yp = 0;
        n->wp = 100;
        n->hp = 100;
        n->x = m->x + n->xp/100 * m->w;
        n->y = m->y + n->yp/100 * m->h;
        n->w = n->wp/100 * m->w;
        n->h = n->hp/100 * m->h;

        xcb_move_resize(con, n);
        //xcb_raise_window(con, n);
    } else {
        splitwindows(n, d->prevfocus, d, m);

        xcb_move_resize(con, n);
        //xcb_raise_window(con, n);

        xcb_move_resize(con, d->prevfocus);
        //xcb_raise_window(con, d->prevfocus);
    }

    xcb_map_window(con, n->win);
}

void tileremove(window *r, desktop *d)
{
    window **l;
    if((l = windowstotheleft(r, d)))
        for(int i = 0; l[i]; i++)
            l[i]->hp += r->hp;

}

//window is being unmapped. we should delete it
void unmapnotify(xcb_generic_event_t *e)
{
    DEBUG("unmapnotify\n");
    xcb_unmap_notify_event_t *ev = (xcb_unmap_notify_event_t *)e;
    
    window *w;
    for (int i = 0; i < DESKTOPS; i++)
        for (w = desktops[i].head; w; w = w->next)
            if(w->win == ev->window) {
                deletewindow(w, &desktops[i]);
                return;
            }
}

window** windowstotheleft(window *w, desktop *d)
{
    window **l = NULL;
    int size = 0;
    int i = 0;

    for(window *x = d->head; x; x = x->next) {
        if((x->xp + x->wp) == w->xp) //directly to the left
            if(x->yp >= w->yp && (x->yp + x->hp) <= (w->yp + w->hp)) {
                l[i++] = x;
                size += x->hp;
                if(size == w->hp)
                    return l;
            }
    }

    return NULL;
}

window *wintowin(xcb_window_t w) 
{
    window *c = NULL;
 
    for (int i = 0; i < DESKTOPS; i++)
        for (c = desktops[i].head; c; c = c->next)
            if(c->win == w) {
                DEBUG("wintoclient: leaving, returning found client\n");
                return c;
            }
    
    return NULL;
}

int xcb_checkotherwm() 
{
    xcb_generic_error_t *error;
    unsigned int values[1] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT|XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY|
                              XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_BUTTON_PRESS};
    error = xcb_request_check(con, xcb_change_window_attributes_checked(con, screen->root, XCB_CW_EVENT_MASK, values));
    xcb_flush(con);
    if (error) 
        return 1;
    return 0;
}

xcb_screen_t *xcb_screen_of_display(xcb_connection_t *con, int screen) 
{
    xcb_screen_iterator_t iter;
    iter = xcb_setup_roots_iterator(xcb_get_setup(con));
    
    for (; iter.rem; --screen, xcb_screen_next(&iter)) 
        if (screen == 0) 
            return iter.data;
    
    return NULL;
}

int main(void)
{
    if(setup())
        run();
    clean();
    xcb_disconnect(con);
    return 0;
}

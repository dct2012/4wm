#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/randr.h>
#include <X11/keysym.h>
#include <X11/Xresource.h>


// MACROS
#if 0
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
void killwindow();
void* malloc_safe(size_t size);
window* prev_window(window *w, desktop *d);
void setup_desktops();
void setup_events();
bool setup_keyboard();
void setup_monitors();
void spawn(const Arg *arg);
void tilenew(window *n);
void unmapnotify(xcb_generic_event_t *e);
window *wintowin(xcb_window_t w);
int xcb_checkotherwm();
xcb_screen_t *xcb_screen_of_display(xcb_connection_t *con, int screen);


#include "config.h"


// VARIABLES
int nmons = 0;
bool running = true;
xcb_connection_t *con;
xcb_screen_t *screen;
desktop desktops[DESKTOPS];
monitor *mons = NULL, *selmon = NULL;
void (*events[XCB_NO_OPERATION])(xcb_generic_event_t *e);


// WRAPPERS
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
    unsigned int pos[4] = { w->x, w->y, w->w, w->h };
    xcb_configure_window(con, w->win, XCB_MOVE_RESIZE, pos);
}

// wrapper to lower window
inline void xcb_lower_window(xcb_connection_t *con, window *w) {
    unsigned int arg[1] = { XCB_STACK_MODE_BELOW };
    xcb_configure_window(con, w->win, XCB_CONFIG_WINDOW_STACK_MODE, arg);
}

// wrapper to raise window
inline void xcb_raise_window(xcb_connection_t *con, window *w) {
    unsigned int arg[1] = { XCB_STACK_MODE_ABOVE };
    xcb_configure_window(con, w->win, XCB_CONFIG_WINDOW_STACK_MODE, arg);
}

// FUNCTION DEFINITIONS


void addwindow(xcb_window_t w, desktop *d) {
    window *c;
    if (!(c = (window *)malloc_safe(sizeof(window))))
        return;

    for(window *l = d->head; l && l->next; l = l->next) {
        if(!l)
            l = c; //should be the same as d->head = c
        else {
            l->next = c;
            d->prevfocus = d->current;
            d->current = c;
        }
    } 

    DEBUGP("addwindow: d->count = %d\n", d->count);

    unsigned int values[1] = { XCB_EVENT_MASK_PROPERTY_CHANGE|(FOLLOW_MOUSE?XCB_EVENT_MASK_ENTER_WINDOW:0) };
    xcb_change_window_attributes_checked(con, (c->win = w), XCB_CW_EVENT_MASK, values);
    
    if(d->mode == TILE)
        tilenew(c);
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
    xcb_configure_request_event_t *ev = (xcb_configure_request_event_t*)e;

    window *w;
    if (!(w = wintowin(ev->window)))
        xcb_configure_window_checked(con, ev->window, ev->value_mask, NULL);
    else
        xcb_send_event(con, false, ev->window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char*)ev);
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

void deletewindow(window *r, desktop *d) {
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
    
    free(r->title);
    free(r); 
    r = NULL; 
}

// window is being closed
//  we should delete it
void destroynotify(xcb_generic_event_t *e)
{
    xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t*)e;

    //find window
    //  delete window
}

// mouse has entered a different window, we should check
//      if follow mouse is selected and change focused window
void enternotify(xcb_generic_event_t *e)
{
    xcb_enter_notify_event_t *ev = (xcb_enter_notify_event_t*)e;
}

// window thinks it needs to be redrawn (repainted)
void expose(xcb_generic_event_t *e)
{
    xcb_expose_event_t *ev = (xcb_expose_event_t*)e;
}

// winodw wants focus or input focus
//      ignore for now
void focusin(xcb_generic_event_t *e)
{
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
    xcb_mapping_notify_event_t *ev = (xcb_mapping_notify_event_t*)e;
}

// window wants to display itself. we need to check
//      if it already exists
//          if it does do nothing
//      else create a new window and tile or float it
void maprequest(xcb_generic_event_t *e)
{
    xcb_map_request_event_t *ev = (xcb_map_request_event_t*)e;

    window *w = wintowin(ev->window);
    if(w)
        return;

    addwindow(ev->window, &desktops[selmon->curr_dtop]);
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
    xcb_property_notify_event_t *ev = (xcb_property_notify_event_t*)e;
}

void run()
{
    xcb_generic_event_t *e;

    while(running) {
        if((e = xcb_wait_for_event(con))) {
            if(events[e->response_type & ~0x08])
                events[e->response_type & ~0x08](e);

            free(e);
        }
    }
}

bool setup()
{   
    int default_screen;
    if(xcb_connection_has_error((con = xcb_connect(NULL, &default_screen))))
        return false;

    if(!(screen = xcb_screen_of_display(con, default_screen)))
        return false;

    if(!setup_keyboard())
        return false;

    if (xcb_checkotherwm())
        return false;

    grabkeys();

    setup_events();

    setup_monitors();
    setup_desktops();

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

void tilenew(window *n)
{
    desktop *d = &desktops[selmon->curr_dtop];
    
    if(!d->prevfocus) { // it's the first
        n->xp = 100;
        n->yp = 100;
        n->wp = 100;
        n->hp = 100;
        n->x = n->xp/100 * selmon->x;
        n->y = n->yp/100 * selmon->y;
        n->w = n->wp/100 * selmon->w;
        n->h = n->hp/100 * selmon->h;

        xcb_move_resize(con, n);
        xcb_raise_window(con, n);
    } else {
        d->prevfocus->wp /= 2;

        n->xp = d->prevfocus->wp;
        n->yp = 100;
        n->wp = d->prevfocus->wp;
        n->hp = 100;
        n->x = n->xp/100 * selmon->x;
        n->y = n->yp/100 * selmon->y;
        n->w = n->wp/100 * selmon->w;
        n->h = n->hp/100 * selmon->h;

        xcb_move_resize(con, n);
        xcb_raise_window(con, n);

        xcb_move_resize(con, d->prevfocus);
        xcb_raise_window(con, d->prevfocus);
    }

}

//window is being unmapped. we should delete it
void unmapnotify(xcb_generic_event_t *e)
{
    xcb_unmap_notify_event_t *ev = (xcb_unmap_notify_event_t *)e;
}

window *wintowin(xcb_window_t w) 
{
    window *c = NULL;
    int i;
 
    for (i = 0; i < DESKTOPS; i++)
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
}

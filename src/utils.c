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
#define XCB_MOVE        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y
#define XCB_RESIZE      XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT

static char *WM_ATOM_NAME[]   = { "WM_PROTOCOLS", "WM_DELETE_WINDOW" };
static char *NET_ATOM_NAME[]  = { "_NET_SUPPORTED", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE", "_NET_WM_NAME", "_NET_ACTIVE_WINDOW" };

#define LENGTH(x) (sizeof(x)/sizeof(*x))
#define CLEANMASK(mask) (mask & ~(numlockmask | XCB_MOD_MASK_LOCK))
#define BUTTONMASK      XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE
#define INRECT(X,Y,RX,RY,RW,RH) ((X) >= (RX) && (X) < (RX) + (RW) && (Y) >= (RY) && (Y) < (RY) + (RH))
#define ISFFT(c)        (c->isfullscrn || c->isfloating || c->istransient)
#define USAGE           "usage: frankensteinwm [-h] [-v]"

enum { RESIZE, MOVE };
enum { TILE, MONOCLE, VIDEO, FLOAT };
enum { TLEFT, TRIGHT, TBOTTOM, TTOP, TDIRECS };
enum { WM_PROTOCOLS, WM_DELETE_WINDOW, WM_COUNT };
enum { NET_SUPPORTED, NET_FULLSCREEN, NET_WM_STATE, NET_ACTIVE, NET_WM_NAME, NET_COUNT };

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

/* get screen of display */
static xcb_screen_t *xcb_screen_of_display(xcb_connection_t *con, int screen) {
    xcb_screen_iterator_t iter;
    iter = xcb_setup_roots_iterator(xcb_get_setup(con));
    for (; iter.rem; --screen, xcb_screen_next(&iter)) if (screen == 0) return iter.data;
    return NULL;
}

/* wrapper to move and resize window */
static inline void xcb_move_resize(xcb_connection_t *con, xcb_window_t win, int x, int y, int w, int h) {
    unsigned int pos[4] = { x, y, w, h };
    xcb_configure_window(con, win, XCB_MOVE_RESIZE, pos);
}

/* wrapper to move window */
static inline void xcb_move(xcb_connection_t *con, xcb_window_t win, int x, int y) {
    unsigned int pos[2] = { x, y };
    xcb_configure_window(con, win, XCB_MOVE, pos);
}

/* wrapper to resize window */
static inline void xcb_resize(xcb_connection_t *con, xcb_window_t win, int w, int h) {
    unsigned int pos[2] = { w, h };
    xcb_configure_window(con, win, XCB_RESIZE, pos);
}

/* wrapper to raise window */
static inline void xcb_raise_window(xcb_connection_t *con, xcb_window_t win) {
    unsigned int arg[1] = { XCB_STACK_MODE_ABOVE };
    xcb_configure_window(con, win, XCB_CONFIG_WINDOW_STACK_MODE, arg);
}

/* wrapper to get xcb keysymbol from keycode */
static xcb_keysym_t xcb_get_keysym(xcb_keycode_t keycode) {
    xcb_key_symbols_t *keysyms;
    xcb_keysym_t       keysym;

    if (!(keysyms = xcb_key_symbols_alloc(dis))) return 0;
    keysym = xcb_key_symbols_get_keysym(keysyms, keycode, 0);
    xcb_key_symbols_free(keysyms);

    return keysym;
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

/* retieve RGB color from hex (think of html) */
static unsigned int xcb_get_colorpixel(char *hex) {
    char strgroups[3][3]  = {{hex[1], hex[2], '\0'}, {hex[3], hex[4], '\0'}, {hex[5], hex[6], '\0'}};
    unsigned int rgb16[3] = {(strtol(strgroups[0], NULL, 16)), (strtol(strgroups[1], NULL, 16)), (strtol(strgroups[2], NULL, 16))};
    return (rgb16[0] << 16) + (rgb16[1] << 8) + rgb16[2];
}

/* wrapper to get atoms using xcb */
static void xcb_get_atoms(char **names, xcb_atom_t *atoms, unsigned int count) {
    xcb_intern_atom_cookie_t cookies[count];
    xcb_intern_atom_reply_t  *reply;

    for (unsigned int i = 0; i < count; i++) cookies[i] = xcb_intern_atom(dis, 0, strlen(names[i]), names[i]);
    for (unsigned int i = 0; i < count; i++) {
        reply = xcb_intern_atom_reply(dis, cookies[i], NULL); /* TODO: Handle error */
        if (reply) {
            DEBUGP("%s : %d\n", names[i], reply->atom);
            atoms[i] = reply->atom; free(reply);
        } else puts("WARN: frankensteinwm failed to register %s atom.\nThings might not work right.");
    }
}

/* wrapper to window get attributes using xcb */
static void xcb_get_attributes(xcb_window_t *windows, xcb_get_window_attributes_reply_t **reply, unsigned int count) {
    xcb_get_window_attributes_cookie_t cookies[count];
    for (unsigned int i = 0; i < count; i++) cookies[i] = xcb_get_window_attributes(dis, windows[i]);
    for (unsigned int i = 0; i < count; i++) reply[i]   = xcb_get_window_attributes_reply(dis, cookies[i], NULL); /* TODO: Handle error */
}

/* check if other wm exists */
static int xcb_checkotherwm(void) {
    xcb_generic_error_t *error;
    unsigned int values[1] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT|XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY|
                              XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_BUTTON_PRESS};
    error = xcb_request_check(dis, xcb_change_window_attributes_checked(dis, screen->root, XCB_CW_EVENT_MASK, values));
    xcb_flush(dis);
    if (error) return 1;
    return 0;
}

/* create a new client and add the new window
 * window should notify of property change events
 */
client* addwindow(xcb_window_t w, desktop *d) {
    DEBUG("addwindow: entering");
    client *c, *t = prev_client(d->head, d);
 
    if (!(c = (client *)malloc_safe(sizeof(client)))) err(EXIT_FAILURE, "cannot allocate client");

    if (!d->head) d->head = c;
    else if (t) t->next = c; 
    else d->head->next = c;

    DEBUGP("addwindow: d->count = %d\n", d->count);

    unsigned int values[1] = { XCB_EVENT_MASK_PROPERTY_CHANGE|(FOLLOW_MOUSE?XCB_EVENT_MASK_ENTER_WINDOW:0) };
    xcb_change_window_attributes_checked(dis, (c->win = w), XCB_CW_EVENT_MASK, values);
    DEBUG("addwindow: leaving");
    return c;
}

void adjustclientgaps(const int gap, client *c) {
        if (c->xp == 0) c->gapx = gap;
        else c->gapx = gap/2;
        if (c->yp == 0) c->gapy = gap;
        else c->gapy = gap/2;
        if ((c->xp + c->wp) == 1) c->gapw = gap;
        else c->gapw = gap/2;
        if ((c->yp + c->hp) == 1) c->gaph = gap;
        else c->gaph = gap/2;
}


/* remove all windows in all desktops by sending a delete message */
void cleanup(void) {
    DEBUG("cleanup: entering");
    xcb_query_tree_reply_t  *query;
    xcb_window_t *c;

    xcb_ungrab_key(dis, XCB_GRAB_ANY, screen->root, XCB_MOD_MASK_ANY);
    if ((query = xcb_query_tree_reply(dis,xcb_query_tree(dis,screen->root),0))) {
        c = xcb_query_tree_children(query);
        for (unsigned int i = 0; i != query->children_len; ++i) deletewindow(c[i]);
        free(query);
    }
    
    free(mons);
    free(menus);
    xcb_set_input_focus(dis, XCB_INPUT_FOCUS_POINTER_ROOT, screen->root, XCB_CURRENT_TIME);
    xcb_flush(dis);
    DEBUG("cleanup: leaving");
}

bool clientstouchingbottom(desktop *d, client *c, client **list, int *num) {
    DEBUG("clientstouchingbottom: entering");
    if((c->yp + c->hp) < 1) { //capable of having windows below?
        float width;
        (*num) = 0;
        width = c->wp;
        for (client *n = d->head; n; n = n->next) {
            if ((c != n ) && !ISFFT(c) && (n->yp == (c->yp + c->hp))) { // directly below
                if ((n->xp + n->wp) <= (c->xp + c->wp)) { // width equivalent or less than
                    if ((n->xp == c->xp) && (n->wp == c->wp)) { //direct match?
                        DEBUG("clientstouchingbottom: found direct match");
                        list[(*num)] = n;
                        (*num)++;
                        DEBUG("clientstouchingbottom: leaving, found direct match");
                        return true;
                    }
                    else if (n->xp >= c->xp) { //part
                        width -= n->wp;
                        list[(*num)] = n;
                        (*num)++;
                        if (width == 0) {
                            DEBUG("clientstouchingbottom: leaving true");
                            return true;
                        }
                        if (width < 0) {
                            DEBUG("clientstouchingbottom: leaving false");
                            return false;
                        }
                    }
                }
                
                if ((n->xp <= c->xp) && ((n->xp + n->wp) >= (c->xp + c->wp))) { 
                    // width exceeds, but we should go ahead and make sure list isnt NULL
                    list[(*num)] = n;
                    DEBUG("clientstouchingbottom: leaving false");
                    return false;
                }
            }
        }
    }
    DEBUG("clientstouchingbottom: leaving error");
    return false;
}

bool clientstouchingleft(desktop *d, client *c, client **list, int *num) {
    DEBUG("clientstouchingleft: entering");
    if(c->xp > 0) { //capable of having windows to the left?
        float height;
        (*num) = 0;
        height = c->hp;
        for (client *n = d->head; n; n = n->next) {
            DEBUGP("clientstouchingleft: %f == %f\n", c->xp, n->xp + n->wp);
            if ((c != n ) && !ISFFT(c) && (c->xp == (n->xp + n->wp))) { // directly to the left
                DEBUGP("clientstouchingleft: %f <= %f\n",n->yp + n->hp, c->yp + c->hp);
                if ((n->yp + n->hp) <= (c->yp + c->hp)) { // height equivalent or less than
                    if ((n->yp == c->yp) && (n->hp == c->hp)) { //direct match?
                        list[(*num)] = n;
                        (*num)++;
                        DEBUG("clientstouchingleft: leaving found direct match");
                        return true;
                    }
                    else if (n->yp >= c->yp) { //part
                        height -= n->hp;
                        list[(*num)] = n;
                        (*num)++;
                        if (height == 0) {
                            DEBUG("clientstouchingleft: leaving true");
                            return true;
                        }
                        if (height < 0) {
                            DEBUG("clientstouchingleft: leaving false");
                            return false;
                        }
                    }
                }
                
                if ((n->yp <= c->yp) && ((n->yp + n->hp) >= (c->yp + c->hp))) { 
                    // height exceeds, but we should go ahead and make sure list isnt NULL
                    list[(*num)] = n;
                    DEBUG("clientstouchingleft: leaving false");
                    return false;
                }
            }
        }
    }
    DEBUG("clientstouchingleft: leaving error");
    return false;
}

bool clientstouchingright(desktop *d, client *c, client **list, int *num) {
    DEBUG("clientstouchingright: entering");
    if((c->xp + c->wp) < 1) { //capable of having windows to the right?
        float height;
        (*num) = 0;
        height = c->hp;
        for (client *n = d->head; n; n = n->next) {
            if ((c != n ) && !ISFFT(c) && (n->xp == (c->xp + c->wp))) { //directly to the right
                if ((n->yp + n->hp) <= (c->yp + c->hp)) { // height equivalent or less than
                    if ((n->yp == c->yp) && (n->hp == c->hp)) { //direct match?
                        list[(*num)] = n;
                        (*num)++;
                        DEBUG("clientstouchingright: leaving, found direct match");
                        return true;
                    }
                    else if (n->yp >= c->yp) { //part
                        height -= n->hp;
                        list[(*num)] = n;
                        (*num)++;
                        if (height == 0) {
                            DEBUG("clientstouchingright: leaving true");
                            return true;
                        }
                        if (height < 0) {
                            DEBUG("clientstouchingright: leaving false");
                            return false;
                        }
                    }
                }
                // y is less than or equal, overall height 
                if ((n->yp <= c->yp) && ((n->yp + n->hp) >= (c->yp + c->hp))) { 
                    // height exceeds, but we should go ahead and make sure list isnt NULL
                    list[(*num)] = n;
                    DEBUG("clientstouchingright: leaving false");
                    return false;
                }
            }
        }
    }
    DEBUG("clientstouchingright: leaving error");
    return false;
}

bool clientstouchingtop(desktop *d, client *c, client **list, int *num) {
    DEBUG("clientstouchingtop: entering");
    if(c->yp > 0) { //capable of having windows above?
        float width;
        (*num) = 0;
        width = c->wp;
        for (client *n = d->head; n; n = n->next) {
            if ((c != n) && !ISFFT(c) && (c->yp == (n->yp + n->hp))) {// directly above
                if ((n->xp + n->wp) <= (c->xp + c->wp)) { //width equivalent or less than
                    if ((n->xp == c->xp) && (n->wp == c->wp)) { //direct match?
                        list[(*num)] = n;
                        (*num)++;
                        DEBUG("clientstouchingtop: leaving found direct match");
                        return true;
                    }
                    else if (n->xp >= c->xp) { //part
                        width -= n->wp;
                        list[(*num)] = n;
                        (*num)++;
                        if (width == 0) {
                            DEBUG("clientstouchingtop: leaving true");
                            return true;
                        }
                        if (width < 0) {
                            DEBUG("clientstouchingtop: leaving false");
                            return false;
                        }
                    }
                }
                
                if ((n->xp <= c->xp) && ((n->xp + n->wp) >= (c->xp + c->wp))) { 
                    // width exceeds, but we should go ahead and make sure list isnt NULL
                    list[(*num)] = n;
                    DEBUG("clientstouchingtop: leaving false");
                    return false;
                }
            }
        }
    }
    DEBUG("clientstouchingtop: leaving error");
    return false;
}

desktop *clienttodesktop(client *c) {
    DEBUG("clienttodesktop: entering");
    client *n = NULL; desktop *d = NULL;
    int i;
 
    for (i = 0; i < DESKTOPS; i++)
        for (d = &desktops[i], n = d->head; n; n = n->next)
            if(n == c) {
                DEBUGP("clienttodesktop: leaving, returning found desktop #%d\n", i);
                return d;
            }
    
    DEBUG("clienttodesktop: leaving, returning NULL desktop");
    return NULL;
}


Menu_Entry* createmenuentry(int x, int y, int w, int h, char *cmd) {
    DEBUG("createmenuentry: entering");
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
    DEBUG("createmenuentry: leaving");
    return m;
}

Menu* createmenu(char **list) {
    DEBUG("createmenu: entering");
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
    DEBUG("createmenu: leaving");
    return m;
}

monitor* createmon(xcb_randr_output_t id, int x, int y, int w, int h, int dtop) {
    DEBUG("createmon: entering");
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
    DEBUG("createmon: leaving");
    return m;
}

/* close the window */
void deletewindow(xcb_window_t w) {
    DEBUG("deletewindow: entering");
    xcb_client_message_event_t ev;
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = w;
    ev.format = 32;
    ev.sequence = 0;
    ev.type = wmatoms[WM_PROTOCOLS];
    ev.data.data32[0] = wmatoms[WM_DELETE_WINDOW];
    ev.data.data32[1] = XCB_CURRENT_TIME;
    xcb_send_event(dis, 0, w, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);
    DEBUG("deletewindow: leaving");
}

/* output info about the desktops on standard output stream
 *
 * the info is a list of ':' separated values for neach desktop
 * desktop to desktop info is separated by ' ' single spaces
 * the info values are
 *   the desktop number/id
 *   the desktop's client count
 *   the desktop's tiling layout mode/id
 *   whether the desktop is the current focused (1) or not (0)
 *   whether any client in that desktop has received an urgent hint
 *
 * once the info is collected, immediately flush the stream */
// if a monitor is displaying a empty desktop go ahead and say it has a window
void desktopinfo(void) {
    DEBUG("desktopinfo: entering");
    desktop *d = NULL; client *c = NULL; monitor *m = NULL;
    bool urgent = false; 

    for (int w = 0, i = 0; i < DESKTOPS; i++, w = 0, urgent = false) {
        for (d = &desktops[i], c = d->head; c; urgent |= c->isurgent, ++w, c = c->next); 
        for (m = mons; m; m = m->next)
            if (i == m->curr_dtop && w == 0)
                w++;
        printf("%d:%d:%d:%d:%d%c", i, w, d->direction, i == selmon->curr_dtop, urgent, (i < DESKTOPS - 1) ? ' ':'|');
    }
    printf("%s\n", desktops[selmon->curr_dtop].current ? desktops[selmon->curr_dtop].current->title :"");
    fflush(stdout);
    DEBUG("desktopinfo: leaving");
}

/* highlight borders and set active window and input focus
 * if given current is NULL then delete the active window property
 *
 * stack order by client properties, top to bottom:
 *  - current when floating or transient
 *  - floating or trancient windows
 *  - current when tiled
 *  - current when fullscreen
 *  - fullscreen windows
 *  - tiled windows
 *
 * a window should have borders in any case, except if
 *  - the window is the only window on screen
 *  - the window is fullscreen
 *  - the mode is MONOCLE and the window is not floating or transient */
void focus(client *c, desktop *d) {
    DEBUG("focus: entering"); 
    
    if (!c) {
        xcb_delete_property(dis, screen->root, netatoms[NET_ACTIVE]);
        d->current = d->prevfocus = NULL;
        return;
    }  
    if (c == d->prevfocus && d->current != c->next) { 
        d->prevfocus = prev_client(d->current = c, d);
    } else if (c != d->current) { 
        d->prevfocus = d->current; 
        d->current = c; 
    }
    setborders(d);
    gettitle(c);
    if (CLICK_TO_FOCUS) 
        grabbuttons(c);
    if (ISFFT(c))
        xcb_raise_window(dis, c->win);

    xcb_change_property(dis, XCB_PROP_MODE_REPLACE, screen->root, netatoms[NET_ACTIVE], XCB_ATOM_WINDOW, 32, 1, &d->current->win);
    xcb_set_input_focus(dis, XCB_INPUT_FOCUS_POINTER_ROOT, d->current->win, XCB_CURRENT_TIME); 
    xcb_flush(dis);
    
    desktopinfo();
    DEBUG("focus: leaving");
}

/* get a pixel with the requested color
 * to fill some window area - borders */
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



bool getrootptr(int *x, int *y) {
    xcb_query_pointer_reply_t *reply = xcb_query_pointer_reply(dis, xcb_query_pointer(dis, screen->root), NULL);

    *x = reply->root_x;
    *y = reply->root_y;

    free(reply);

    return true;
}

void gettitle(client *c) {
    xcb_icccm_get_text_property_reply_t reply;
    xcb_generic_error_t *err = NULL;

    if(!xcb_icccm_get_text_property_reply(dis, xcb_icccm_get_text_property(dis, c->win, netatoms[NET_WM_NAME]), &reply, &err))
        if(!xcb_icccm_get_text_property_reply(dis, xcb_icccm_get_text_property(dis, c->win, XCB_ATOM_WM_NAME), &reply, &err))
            return;

    if(err) {
        free(err);
        return;
    }

    // TODO: encoding
    if(!reply.name || !reply.name_len)
        return;

    strncpy(c->title, reply.name, (reply.name_len+1 < 256 ? reply.name_len+1:256));
    c->title[(reply.name_len + 1 < 255 ? reply.name_len +1:255)] = '\0';
    xcb_icccm_get_text_property_reply_wipe(&reply);
}

// TODO work on CLICK_TO_FOCUS
/* set the given client to listen to button events (presses / releases) */
void grabbuttons(client *c) {
    DEBUG("grabbuttons: entering");
    unsigned int i, j, modifiers[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask|XCB_MOD_MASK_LOCK };
    
    xcb_ungrab_button(dis, XCB_BUTTON_INDEX_ANY, c->win, XCB_GRAB_ANY);
    if(c == desktops[selmon->curr_dtop].current) {
        for(i = 0; i < LENGTH(buttons); i++)
            //if(buttons[i].click == ClkClientWin)
                for(j = 0; j < LENGTH(modifiers); j++)
                    xcb_grab_button(dis, false, c->win, BUTTONMASK, XCB_GRAB_MODE_SYNC,
                                    XCB_GRAB_MODE_ASYNC, XCB_WINDOW_NONE, XCB_CURSOR_NONE,
                                    buttons[i].button, buttons[i].mask | modifiers[j]);
    }
    else
        xcb_grab_button(dis, false, c->win, BUTTONMASK, XCB_GRAB_MODE_ASYNC,
                        XCB_GRAB_MODE_SYNC, XCB_WINDOW_NONE, XCB_CURSOR_NONE,
                        XCB_BUTTON_INDEX_ANY, XCB_BUTTON_MASK_ANY);

    DEBUG("grabbuttons: leaving");
}

/* the wm should listen to key presses */
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

void growbyh(client *match, const float size, client *c, monitor *m) {
    c->hp = match ? (match->yp - c->yp):(c->hp + size);
    xcb_move_resize(dis, c->win, c->x, c->y, c->w, (c->h = (m->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph));
}

void growbyw(client *match, const float size, client *c, monitor *m) {
    c->wp = match ? (match->xp - c->xp):(c->wp + size);
    xcb_move_resize(dis, c->win, c->x, c->y, (c->w  = (m->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw), c->h);
}

void growbyx(client *match, const float size, client *c, monitor *m) {
    c->wp = match ? ((c->xp + c->wp) - (match->xp + match->wp)):(c->wp + size);
    c->xp = match ? (match->xp + match->wp):(c->xp - size);
    xcb_move_resize(dis, c->win, (c->x = m->x + (m->w * c->xp) + c->gapx), c->y, 
                    (c->w  = (m->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw), c->h);
}

void growbyy(client *match, const float size, client *c, monitor *m) {
    c->hp = match ? ((c->yp + c->hp) - (match->yp + match->hp)):(c->hp + size);
    c->yp = match ? (match->yp + match->hp):(c->yp - size);
    xcb_move_resize(dis, c->win, c->x, (c->y = m->y + (m->h * c->yp) + c->gapy), 
                    c->w, (c->h = (m->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph));
}

// switch the current client with the first client we find below it
void moveclientdown(int *num, client *c, client **list, desktop *d) { 
    DEBUG("moveclientdown: entering");
    client *cold;
    findtouchingclients[TBOTTOM](d, c, list, num);
    // switch stuff
    if (list[0] != NULL) {
        cold->xp = c->xp; cold->yp = c->yp; cold->wp = c->wp; cold->hp = c->hp;
        c->xp = list[0]->xp; c->yp = list[0]->yp; c->wp = list[0]->wp; c->hp = list[0]->hp;
        adjustclientgaps(d->gap, c);
        list[0]->xp = cold->xp; list[0]->yp = cold->yp; list[0]->wp = cold->wp; list[0]->hp = cold->hp;
        adjustclientgaps(d->gap, list[0]);
        // move stuff
        xcb_move_resize(dis, list[0]->win, 
                        (list[0]->x = selmon->x + (selmon->w * list[0]->xp) + list[0]->gapx), 
                        (list[0]->y = selmon->y + (selmon->h * list[0]->yp) + list[0]->gapy), 
                        (list[0]->w = (selmon->w * list[0]->wp) - 2*BORDER_WIDTH - list[0]->gapx - list[0]->gapw), 
                        (list[0]->h = (selmon->h * list[0]->hp) - 2*BORDER_WIDTH - list[0]->gapy - list[0]->gaph));
        xcb_move_resize(dis, c->win, 
                        (c->x = selmon->x + (selmon->w * c->xp) + c->gapx), 
                        (c->y = selmon->y + (selmon->h * c->yp) + c->gapy), 
                        (c->w = (selmon->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw), 
                        (c->h = (selmon->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph)); 
        setborders(d);
    }
    DEBUG("moveclientdown: leaving");
}

// switch the current client with the first client we find to the left of it
void moveclientleft(int *num, client *c, client **list, desktop *d) { 
    DEBUG("moveclientleft: entering");
    client *cold; 
    findtouchingclients[TLEFT](d, c, list, num);
    // switch stuff
    if (list[0] != NULL) {
        cold->xp = c->xp; cold->yp = c->yp; cold->wp = c->wp; cold->hp = c->hp;
        c->xp = list[0]->xp; c->yp = list[0]->yp; c->wp = list[0]->wp; c->hp = list[0]->hp;
        adjustclientgaps(d->gap, c);
        list[0]->xp = cold->xp; list[0]->yp = cold->yp; list[0]->wp = cold->wp; list[0]->hp = cold->hp;
        adjustclientgaps(d->gap, list[0]);
        // move stuff
        xcb_move_resize(dis, list[0]->win, 
                        (list[0]->x = selmon->x + (selmon->w * list[0]->xp) + list[0]->gapx), 
                        (list[0]->y = selmon->y + (selmon->h * list[0]->yp) + list[0]->gapy), 
                        (list[0]->w = (selmon->w * list[0]->wp) - 2*BORDER_WIDTH - list[0]->gapx - list[0]->gapw), 
                        (list[0]->h = (selmon->h * list[0]->hp) - 2*BORDER_WIDTH - list[0]->gapy - list[0]->gaph));
        xcb_move_resize(dis, c->win, 
                        (c->x = selmon->x + (selmon->w * c->xp) + c->gapx), 
                        (c->y = selmon->y + (selmon->h * c->yp) + c->gapy), 
                        (c->w = (selmon->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw), 
                        (c->h = (selmon->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph));
        setborders(d);
    }
    DEBUG("moveclientleft: leaving");
}

// switch the current client with the first client we find to the right of it
void moveclientright(int *num, client *c, client **list, desktop *d) { 
    DEBUG("moveclientright: entering");
    client *cold;
    findtouchingclients[TRIGHT](d, c, list, num);
    // switch stuff
    if (list[0] != NULL) {
        cold->xp = c->xp; cold->yp = c->yp; cold->wp = c->wp; cold->hp = c->hp;
        c->xp = list[0]->xp; c->yp = list[0]->yp; c->wp = list[0]->wp; c->hp = list[0]->hp;
        adjustclientgaps(d->gap, c);
        list[0]->xp = cold->xp; list[0]->yp = cold->yp; list[0]->wp = cold->wp; list[0]->hp = cold->hp;
        adjustclientgaps(d->gap, list[0]);
        // move stuff
        xcb_move_resize(dis, list[0]->win, 
                        (list[0]->x = selmon->x + (selmon->w * list[0]->xp) + list[0]->gapx), 
                        (list[0]->y = selmon->y + (selmon->h * list[0]->yp) + list[0]->gapy), 
                        (list[0]->w = (selmon->w * list[0]->wp) - 2*BORDER_WIDTH - list[0]->gapx - list[0]->gapw), 
                        (list[0]->h = (selmon->h * list[0]->hp) - 2*BORDER_WIDTH - list[0]->gapy - list[0]->gaph));
        xcb_move_resize(dis, c->win, 
                        (c->x = selmon->x + (selmon->w * c->xp) + c->gapx), 
                        (c->y = selmon->y + (selmon->h * c->yp) + c->gapy), 
                        (c->w = (selmon->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw), 
                        (c->h = (selmon->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph));
        setborders(d);
    }
    DEBUG("moveclientright: leaving");
}

// switch the current client with the first client we find above it
void moveclientup(int *num, client *c, client **list, desktop *d) { 
    DEBUG("moveclientup: entering");
    client *cold; 
    findtouchingclients[TTOP](d, c, list, num); // even if it not a direct match it should return with something touching
    // switch stuff
    if (list[0] != NULL) {
        cold->xp = c->xp; cold->yp = c->yp; cold->wp = c->wp; cold->hp = c->hp;
        adjustclientgaps(d->gap, list[0]);
        c->xp = list[0]->xp; c->yp = list[0]->yp; c->wp = list[0]->wp; c->hp = list[0]->hp;
        adjustclientgaps(d->gap, c);
        list[0]->xp = cold->xp; list[0]->yp = cold->yp; list[0]->wp = cold->wp; list[0]->hp = cold->hp;
        adjustclientgaps(d->gap, list[0]);
        // move stuff
        xcb_move_resize(dis, list[0]->win, 
                        (list[0]->x = selmon->x + (selmon->w * list[0]->xp) + list[0]->gapx), 
                        (list[0]->y = selmon->y + (selmon->h * list[0]->yp) + list[0]->gapy), 
                        (list[0]->w = (selmon->w * list[0]->wp) - 2*BORDER_WIDTH - list[0]->gapx - list[0]->gapw), 
                        (list[0]->h = (selmon->h * list[0]->hp) - 2*BORDER_WIDTH - list[0]->gapy - list[0]->gaph));
        xcb_move_resize(dis, c->win, 
                        (c->x = selmon->x + (selmon->w * c->xp) + c->gapx), 
                        (c->y = selmon->y + (selmon->h * c->yp) + c->gapy), 
                        (c->w = (selmon->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw), 
                        (c->h = (selmon->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph)); 
        setborders(d);
    }
    DEBUG("moveclientup: leaving");
}


/* get the previous client from the given
 * if no such client, return NULL */
client* prev_client(client *c, desktop *d) {
    if (!c || !d->head->next)
        return NULL;
    client *p;
    for (p = d->head; p->next && p->next != c; p = p->next);
    return p;
}



monitor* ptrtomon(int x, int y) {
    monitor *m;
    int i;

    for(i = 0, m = mons; i < nmons; m = m->next, i++)
        if(INRECT(x, y, m->x, m->y, m->w, m->h))
            return m;
    return selmon;
}

void pushtotiling() {
    DEBUG("pushtotiling: entering");
    desktop *d = &desktops[selmon->curr_dtop];
    int gap = d->gap;
    client *c = NULL, *n = d->current; // the client to push
    monitor *m = selmon;
    
    n->isfloating = false;
    n->istransient = false;
    n->isfullscrn = false;

    if (d->prevfocus)
        c = d->prevfocus;
    else if (d->head && (d->head != n))
        c = d->head;
    else { // it must be the only client on this desktop
        n->xp = 0; n->yp = 0; n->wp = 1; n->hp = 1;
        adjustclientgaps(gap, n);
        d->count += 1;
        xcb_move_resize(dis, n->win, 
                            (n->x = m->x + n->gapx), 
                            (n->y = m->y + n->gapy), 
                            (n->w = m->w - 2*n->gapw), 
                            (n->h = m->h - 2*n->gaph));
        DEBUG("pushtotiling: leaving, tiled only client on desktop");
        return;
    }

    if(!c) {
        DEBUG("pushtotiling: leaving, error, !c");
        return;
    }

    tiledirection[d->direction](n, c); 
        
    d->count += 1;

    adjustclientgaps(gap, c);
    adjustclientgaps(gap, n);
    
    if (d->mode == TILE) {
        xcb_move_resize(dis, c->win,
                        (c->x = m->x + (m->w * c->xp) + c->gapx), 
                        (c->y = m->y + (m->h * c->yp) + c->gapy), 
                        (c->w = (m->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw),
                        (c->h = (m->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph));
        DEBUGP("pushtotiling: tiling current x:%f y:%f w:%f h:%f\n", (m->w * c->xp), (m->h * c->yp), (m->w * c->wp) , (m->h * c->hp));

        xcb_move_resize(dis, n->win, 
                        (n->x = m->x + (m->w * n->xp) + n->gapx), 
                        (n->y = m->y + (m->h * n->yp) + n->gapy), 
                        (n->w = (m->w * n->wp) - 2*BORDER_WIDTH - n->gapx - n->gapw), 
                        (n->h = (m->h * n->hp) - 2*BORDER_WIDTH - n->gapy - n->gaph));
        DEBUGP("pushtotiling: tiling new x:%f y:%f w:%f h:%f\n", (m->w * n->xp), (m->h * n->yp), (m->w * n->wp), (m->h * n->hp));
    }
    else
            monocle(m->x, m->y, m->w, m->h, d, m);

    DEBUG("pushtotiling: leaving");
}



/* remove the specified client
 *
 * note, the removing client can be on any desktop,
 * we must return back to the current focused desktop.
 * if c was the previously focused, prevfocus must be updated
 * else if c was the current one, current must be updated. */
void removeclient(client *c, desktop *d, const monitor *m) {
    DEBUG("removeclient: entering"); 
    client **p = NULL, *dead, *n;
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

    if (!ISFFT(c)) {
        d->count -= 1;
        dead = (client *)malloc_safe(sizeof(client));
        *dead = *c;
        dead->next = NULL;
        if (!d->dead) {
            DEBUG("removeclient: d->dead == NULL");
            d->dead = dead;
        }
        else {
            for (n = d->dead; n; n = n->next);
            n = dead;
        }
        tileremove(d, m);
    } 
    free(c); c = NULL; 
    setborders(d);
    desktopinfo();
    DEBUG("removeclient: leaving");
}

 

void resizeclientbottom(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list) {
    DEBUG("resizeclientbottom: entering"); 
    if (findtouchingclients[TBOTTOM](d, c, list, n)) {
        if (grow) {
            //client in list y increases and height decreases
            for (int i = 0; i < (*n); i++)
                shrinkbyy(NULL, size, list[i], m);
            //current windows height increases
            growbyh(list[0], size, c, m);
        } else {
            shrinkbyh(NULL, size, c, m);
            for (int i = 0; i < (*n); i++)
                growbyy(c, size, list[i], m);
        }
    } else if (findtouchingclients[TTOP](d, c, list, n)) {
        if (grow) {
            //current windows y increases and height decreases
            shrinkbyy(NULL, size, c, m);
            //client in list height increases
            for (int i = 0; i < (*n); i++)
                growbyh(c, size, list[i], m);
        } else {
            for (int i = 0; i < (*n); i++)
                shrinkbyh(NULL, size, list[i], m);
            growbyy(list[0], size, c, m);
        }
    }
    DEBUG("resizeclientbottom: leaving");
}

void resizeclientleft(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list) {
    DEBUG("resizeclientleft: entering"); 
    if (findtouchingclients[TLEFT](d, c, list, n)) {
        if (grow) {
            //client in list width decreases
            for (int i = 0; i < (*n); i++)
                shrinkbyw(NULL, size, list[i], m);
            //the current windows x decreases and width increases
            growbyx(list[0], size, c, m);
        } else {
            shrinkbyx(NULL, size, c, m);
            for (int i = 0; i < (*n); i++)
                growbyw(c, size, list[i], m);
        }
    } else if (findtouchingclients[TRIGHT](d, c, list, n)) {
        if (grow) {
            //current windows width decreases
            shrinkbyw(NULL, size, c, m);
            //clients in list x decreases width increases
            for (int i = 0; i < (*n); i++)
                growbyx(c, size, list[i], m);
        } else { 
            for (int i = 0; i < (*n); i++)
                shrinkbyx(NULL, size, list[i], m);
            growbyw(list[0], size, c, m);
        }
    }
    DEBUG("resizeclientleft: leaving");
}

void resizeclientright(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list) {
    DEBUG("resizeclientright: entering");
    if (findtouchingclients[TRIGHT](d, c, list, n)) { 
        if (grow) {
            //clients in list x increases and width decrease
            for (int i = 0; i < (*n); i++)
                shrinkbyx(NULL, size, list[i], m);
            //the current windows width increases
            growbyw(list[0], size, c, m);
        } else {
            shrinkbyw(NULL, size, c, m);
            for (int i = 0; i < (*n); i++)
                growbyx(c, size, list[i], m);
        }
    } else if (findtouchingclients[TLEFT](d, c, list, n)) {
        if (grow) {
            //current windows x increases and width decreases
            shrinkbyx(NULL, size, c, m);
            //other windows width increases
            for (int i = 0; i < (*n); i++)
                growbyw(c, size, list[i], m);
        } else {
            for (int i = 0; i < (*n); i++)
                shrinkbyw(NULL, size, list[i], m);
            growbyx(list[0], size, c, m);
        }
    }
    DEBUG("resizeclientright: leaving");
}

void resizeclienttop(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list) {
    DEBUG("resizeclienttop: entering"); 
    if (findtouchingclients[TTOP](d, c, list, n)) {
        if (grow) {
            //client in list height decreases
            for (int i = 0; i < (*n); i++)
                shrinkbyh(NULL, size, list[i], m);
            //current windows y decreases and height increases
            growbyy(list[0], size, c, m);
        } else {
            shrinkbyy(NULL, size, c, m);
            for (int i = 0; i < (*n); i++)
                growbyh(c, size, list[i], m);
        }
    } else if (findtouchingclients[TBOTTOM](d, c, list, n)) {
        if (grow) {
            //current windows height decreases
            shrinkbyh(NULL, size, c, m);
            //client in list y decreases and height increases
            for (int i = 0; i < (*n); i++)
               growbyy(c, size, list[i], m);
        }else { 
            for (int i = 0; i < (*n); i++)
                shrinkbyy(NULL, size, list[i], m);
            growbyh(list[0], size, c, m);
        }
    } 
    DEBUG("resizeclienttop: leaving");
}

void retile(desktop *d, const monitor *m) {
    DEBUG("retile: entering");
    int gap = d->gap;

    if (d->mode == TILE) {
        int n = d->count;
        DEBUGP("retile: d->count = %d\n", d->count);
       
        for (client *c = d->head; c; c=c->next) {
            if (!ISFFT(c)) {
                if (n == 1) {
                    c->gapx = c->gapy = c->gapw = c->gaph = gap;
                    xcb_move_resize(dis, c->win, 
                                    (c->x = m->x + (m->w * c->xp) + gap), 
                                    (c->y = m->y + (m->h * c->yp) + gap), 
                                    (c->w = (m->w * c->wp) - 2*gap), 
                                    (c->h = (m->h * c->hp) - 2*gap));
                }
                else { 
                    xcb_move_resize(dis, c->win, 
                                    (c->x = m->x + (m->w * c->xp) + c->gapx), 
                                    (c->y = m->y + (m->h * c->yp) + c->gapy), 
                                    (c->w = (m->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw), 
                                    (c->h = (m->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph));
                }
            }
        } 
    }
    else
        monocle(m->x, m->y, m->w, m->h, d, m);
    setborders(d);

    DEBUG("retile: leaving");
}

/* jump and focus the next or previous desktop */
void rotate(const Arg *arg) {
    change_desktop(&(Arg){.i = (DESKTOPS + selmon->curr_dtop + arg->i) % DESKTOPS});
}

/* jump and focus the next or previous desktop that has clients */
void rotate_filled(const Arg *arg) {
    int n = arg->i;
    while (n < DESKTOPS && !desktops[(DESKTOPS + selmon->curr_dtop + n) % DESKTOPS].head) (n += arg->i);
    change_desktop(&(Arg){.i = (DESKTOPS + selmon->curr_dtop + n) % DESKTOPS});
}

void setborders(desktop *d) {
    DEBUG("setborders: entering"); 
    unsigned int values[1];  /* this is the color maintainer */
    unsigned int zero[1];
    int half;
    client *c;
    
    zero[0] = 0;
    values[0] = BORDER_WIDTH; /* Set border width. */ 

    // find n = number of windows with set borders
    int n = d->count;
    DEBUGP("setborders: d->count = %d\n", d->count);

    for (c = d->head; c; c = c -> next) {
        // rules for no border
        if ((n == 1) || (d->mode == MONOCLE) || (d->mode == VIDEO) || c->istransient || c->isfullscrn) {
            xcb_configure_window(dis, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, zero);
        }
        else {
            xcb_configure_window(dis, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, values);
            //if (c == d->head && c->next == NULL)
                //half = -OUTER_BORDER;
                //else
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
            
            xcb_change_gc(dis, gc, XCB_GC_FOREGROUND, (c->isurgent ? &win_urgent:&win_outer));
            xcb_poly_fill_rectangle(dis, pmap, gc, 4, rect_outer);

            xcb_change_gc(dis, gc, XCB_GC_FOREGROUND, (c == d->current ? &win_focus:&win_unfocus));
            xcb_poly_fill_rectangle(dis, pmap, gc, 5, rect_inner);
            xcb_change_window_attributes(dis,c->win, XCB_CW_BORDER_PIXMAP, &pmap);
            /* free the memory we allocated for the pixmap */
            xcb_free_pixmap(dis,pmap);
            xcb_free_gc(dis,gc);
        }
    }
    xcb_flush(dis);
    DEBUG("setborders: leaving");
}


void shrinkbyh(client *match, const float size, client *c, monitor *m) {
    c->hp = match ? (match->yp - c->yp):(c->hp - size);
    xcb_move_resize(dis, c->win, c->x, c->y, c->w, (c->h = (m->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph));
}

void shrinkbyw(client *match, const float size, client *c, monitor *m) {
    c->wp = match ? (match->xp - c->xp):(c->wp - size);
    xcb_move_resize(dis, c->win, c->x, c->y, (c->w  = (m->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw), c->h);
}

void shrinkbyx(client *match, const float size, client *c, monitor *m) {
    c->wp = match ? ((c->xp + c->wp) - (match->xp + match->wp)):(c->wp - size);
    c->xp = match ? (match->xp + match->wp):(c->xp + size);
    xcb_move_resize(dis, c->win, (c->x = m->x + (m->w * c->xp) + c->gapx), c->y, 
                    (c->w  = (m->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw), c->h);
}

void shrinkbyy(client *match, const float size, client *c, monitor *m) {
    c->hp = match ? ((c->yp + c->hp) - (match->yp + match->hp)):(c->hp - size);
    c->yp = match ? (match->yp + match->hp):(c->yp + size);
    xcb_move_resize(dis, c->win, c->x, (c->y = m->y + (m->h * c->yp) + c->gapy), 
                    c->w, (c->h = (m->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph));
}


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

    // TODO: make sure all these gc's are being cleaned up
    /*
    cookie_gc = xcb_free_gc(dis, gc);
    error = xcb_request_check (dis, cookie_gc);
    if (error) {
        fprintf (stderr, "ERROR: can't free gc : %d\n", error->error_code);
        xcb_disconnect (dis);
        exit (-1);
    }
    */
}

void tilenewbottom(client *n, client *c) {
    DEBUG("tilenewbottom: entering"); 
    n->xp = c->xp;
    n->yp = c->yp + (c->hp/2);
    n->wp = c->wp;
    n->hp = (c->yp + c->hp) - n->yp;
    c->hp = n->yp - c->yp;
    DEBUG("tilenewbottom: leaving");
}

void tilenewleft(client *n, client *c) {
    DEBUG("tilenewleft: entering");
    n->xp = c->xp;
    n->yp = c->yp;
    n->wp = c->wp/2;
    n->hp = c->hp;
    c->xp = n->xp + n->wp;
    c->wp = (n->xp + c->wp) - c->xp;
    DEBUG("tilenewleft: leaving");
}

void tilenewright(client *n, client *c) {
    DEBUG("tilenewright: entering");
    n->xp = c->xp + (c->wp/2);
    n->yp = c->yp;
    n->wp = (c->xp + c->wp) - n->xp;
    n->hp = c->hp;
    c->wp = n->xp - c->xp;
    DEBUG("tilenewright: leaving");
}

void tilenewtop(client *n, client *c) {
    DEBUG("tilenewtop: entering");
    n->xp = c->xp;
    n->yp = c->yp;
    n->wp = c->wp;
    n->hp = c->hp/2;
    c->yp = n->yp + n->hp;
    c->hp = (n->yp + c->hp) - c->yp;
    DEBUG("tilenewtop: leaving");
}

/* find which client the given window belongs to */
client *wintoclient(xcb_window_t w) {
    DEBUG("wintoclient: entering");
    client *c = NULL;
    int i;
 
    for (i = 0; i < DESKTOPS; i++)
        for (c = desktops[i].head; c; c = c->next)
            if(c->win == w) {
                DEBUG("wintoclient: leaving, returning found client");
                return c;
            }
    
    DEBUG("wintoclient: leaving, returning NULL client");
    return NULL;
}

/* find which monitor the given window belongs to */
monitor *wintomon(xcb_window_t w) {
    DEBUG("wintomon: entering");
    int x, y;
    monitor *m; client *c;
    int i; 

    if(w == screen->root && getrootptr(&x, &y)) {
        DEBUG("wintomon: leaving, returning ptrtomon");
        return ptrtomon(x, y);
    }
     
    for (i = 0, m = mons; i < nmons; m = m->next, i++)
        for (c = desktops[m->curr_dtop].head; c; c = c->next)
            if(c->win == w) {
                DEBUG("wintomon: leaving, returning found monitor");
                return m;
            }
    
    DEBUG("wintomon: leaving, returning NULL monitor");
    return NULL;
}

/* vim: set ts=4 sw=4 :*/

/* see license for copyright and license */

#include "frankensteinwm.h"

// variables
static bool running = true;
static int randrbase, retval = 0, nmons = 0;
static unsigned int numlockmask = 0, win_unfocus, win_focus, win_outer, win_urgent, win_trn, win_flt;
static xcb_connection_t *dis;
static xcb_screen_t *screen;
static xcb_atom_t wmatoms[WM_COUNT], netatoms[NET_COUNT];
static desktop desktops[DESKTOPS];
static monitor *mons = NULL, *selmon = NULL;
static Menu *menus = NULL;
static Xresources xres;

// events array on receival of a new event, call the appropriate function to handle it
void (*events[XCB_NO_OPERATION])(xcb_generic_event_t *e);

/* wrapper to move and resize window */
inline void xcb_move_resize(xcb_connection_t *con, xcb_window_t win, int x, int y, int w, int h) {
    unsigned int pos[4] = { x, y, w, h };
    xcb_configure_window(con, win, XCB_MOVE_RESIZE, pos);
}

/* wrapper to raise window */
inline void xcb_raise_window(xcb_connection_t *con, xcb_window_t win) {
    unsigned int arg[1] = { XCB_STACK_MODE_ABOVE };
    xcb_configure_window(con, win, XCB_CONFIG_WINDOW_STACK_MODE, arg);
}

bool (*findtouchingclients[TDIRECS])(desktop *d, client *c, client **list, int *num) = {
    [TBOTTOM] = clientstouchingbottom, [TLEFT] = clientstouchingleft, [TRIGHT] = clientstouchingright, [TTOP] = clientstouchingtop,
};

void (*tiledirection[TDIRECS])(client *n, client *c) = {
    [TBOTTOM] = tilenewbottom, [TLEFT] = tilenewleft, [TRIGHT] = tilenewright, [TTOP] = tilenewtop,
};

/* COMMANDS */

#define XCB_MOVE        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y
#define XCB_RESIZE      XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT

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

static void growbyh(client *match, const float size, client *c, monitor *m) {
    c->hp = match ? (match->yp - c->yp):(c->hp + size);
    xcb_move_resize(dis, c->win, c->x, c->y, c->w, (c->h = (m->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph));
}

static void growbyw(client *match, const float size, client *c, monitor *m) {
    c->wp = match ? (match->xp - c->xp):(c->wp + size);
    xcb_move_resize(dis, c->win, c->x, c->y, (c->w  = (m->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw), c->h);
}

static void growbyx(client *match, const float size, client *c, monitor *m) {
    c->wp = match ? ((c->xp + c->wp) - (match->xp + match->wp)):(c->wp + size);
    c->xp = match ? (match->xp + match->wp):(c->xp - size);
    xcb_move_resize(dis, c->win, (c->x = m->x + (m->w * c->xp) + c->gapx), c->y, 
                    (c->w  = (m->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw), c->h);
}

static void growbyy(client *match, const float size, client *c, monitor *m) {
    c->hp = match ? ((c->yp + c->hp) - (match->yp + match->hp)):(c->hp + size);
    c->yp = match ? (match->yp + match->hp):(c->yp - size);
    xcb_move_resize(dis, c->win, c->x, (c->y = m->y + (m->h * c->yp) + c->gapy), 
                    c->w, (c->h = (m->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph));
}

static void shrinkbyh(client *match, const float size, client *c, monitor *m) {
    c->hp = match ? (match->yp - c->yp):(c->hp - size);
    xcb_move_resize(dis, c->win, c->x, c->y, c->w, (c->h = (m->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph));
}

static void shrinkbyw(client *match, const float size, client *c, monitor *m) {
    c->wp = match ? (match->xp - c->xp):(c->wp - size);
    xcb_move_resize(dis, c->win, c->x, c->y, (c->w  = (m->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw), c->h);
}

static void shrinkbyx(client *match, const float size, client *c, monitor *m) {
    c->wp = match ? ((c->xp + c->wp) - (match->xp + match->wp)):(c->wp - size);
    c->xp = match ? (match->xp + match->wp):(c->xp + size);
    xcb_move_resize(dis, c->win, (c->x = m->x + (m->w * c->xp) + c->gapx), c->y, 
                    (c->w  = (m->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw), c->h);
}

static void shrinkbyy(client *match, const float size, client *c, monitor *m) {
    c->hp = match ? ((c->yp + c->hp) - (match->yp + match->hp)):(c->hp - size);
    c->yp = match ? (match->yp + match->hp):(c->yp + size);
    xcb_move_resize(dis, c->win, c->x, (c->y = m->y + (m->h * c->yp) + c->gapy), 
                    c->w, (c->h = (m->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph));
}

static void text_draw (xcb_gcontext_t gc, xcb_window_t window, int16_t x1, int16_t y1, const char *label) {
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

/* focus another desktop
 *
 * to avoid flickering
 * first map the new windows
 * first the current window and then all other
 * then unmap the old windows
 * first all others then the current */
void change_desktop(const Arg *arg) {  
    DEBUG("change_desktop: Entered"); 
    int i;

    if (arg->i == selmon->curr_dtop || arg->i < 0 || arg->i >= DESKTOPS) { 
        DEBUG("change_desktop: not a valid desktop or the same desktop");
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
        DEBUG("change_desktop: tiling current monitor, new desktop");
        retile(n, selmon);
        
        DEBUG("change_desktop: tiling other monitor, old desktop");
        retile(d, m);
    }
    else { 
        DEBUG("change_desktop: retiling new windows on current monitor");
        retile(n, selmon);
        DEBUG("change_desktop: mapping new windows on current monitor"); 
        if (n->current)
            xcb_map_window(dis, n->current->win);
        for (client *c = n->head; c; c = c->next)
            xcb_map_window(dis, c->win);
 
        DEBUG("change_desktop: unmapping old windows on current monitor");
        for (client *c = d->head; c; c = c->next) 
            if (c != d->current)
                xcb_unmap_window(dis, c->win);
        if (d->current)
            xcb_unmap_window(dis, d->current->win); 
    } 
    
    desktopinfo();
    DEBUG("change_desktop: leaving");
}

/* move a client to another desktop
 *
 * remove the current client from the current desktop's client list
 * and add it as last client of the new desktop's client list */
void client_to_desktop(const Arg *arg) {
    DEBUG("client_to_desktop: entering");

    if (arg->i == selmon->curr_dtop || arg->i < 0 || arg->i >= DESKTOPS || !desktops[selmon->curr_dtop].current) 
        return;
   
    //see if window will be moving to another monitor
    bool flag = false; monitor *m = NULL; int i;
    for (i = 0, m = mons; i < nmons; m = m->next, i++)
        if ((m->curr_dtop == arg->i) && m != selmon) {
            flag = true;
            break;
        }

    desktop *d = &desktops[selmon->curr_dtop], *n = &desktops[arg->i];
    client *c = d->current, *p = prev_client(d->current, d), *l = prev_client(n->head, n), *dead, *itr;
    
    if (c == d->head || !p) 
        d->head = c->next; 
    else 
        p->next = c->next;
    c->next = NULL;
    if (!ISFT(c))
        d->count -= 1;
    DEBUGP("client_to_desktop: d->count = %d\n", d->count);

    if (!flag) { // window is not moving to another monitor 
        xcb_unmap_window(dis, c->win);
    }

    dead = (client *)malloc_safe(sizeof(client));
    *dead = *c;
    dead->next = NULL;
    if (!d->dead) {
        DEBUG("client_to_desktop: d->dead == NULL");
        d->dead = dead;
    }
    else {
        for (itr = d->dead; itr && itr->next; itr = itr->next);
        itr->next = dead;
    }
    tileremove(d, selmon);
    focus(d->prevfocus, d);
    if (l)
        l->next = c;
    else if (n->head)
        n->head->next = c;
    else
        n->head = c; 
    n->count += 1;
    DEBUGP("client_to_desktop: n->count = %d\n", n->count);

    m = wintomon(n->head->win);
    tilenew(n, m); // itll be ok if m == NULL 
    focus(c, n);

    if (FOLLOW_WINDOW) 
        change_desktop(arg); 
    
    desktopinfo();
    DEBUG("client_to_desktop: leaving");
}

// decrease gap between windows
void decreasegap(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    if(d->gap > MINGAP) {
        d->gap -= arg->i;
        for (client *c = d->head; c; c = c->next)
            adjustclientgaps(d->gap, c);
        retile(d, selmon);
    }
}

/* find and focus the client which received
 * the urgent hint in the current desktop */
void focusurgent() {
    DEBUG("focusurgent: entering");
    client *c = NULL;
    int d = -1;
    for (c=desktops[selmon->curr_dtop].head; c && !c->isurgent; c=c->next);
    while (!c && d < DESKTOPS-1) 
        for (c = desktops[++d].head; c && !c->isurgent; c = c->next);
    //current_desktop = current_desktop;
    if (c) { 
        change_desktop(&(Arg){.i = --d}); 
        focus(c, &desktops[selmon->curr_dtop]);
    }

    DEBUG("focusurgent: leaving");
}

// increase gap between windows
void increasegap(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    if(d->gap < MAXGAP) {
        d->gap += arg->i;
        for (client *c = d->head; c; c = c->next)
            adjustclientgaps(d->gap, c);
        retile(d, selmon); 
    }
}

/* explicitly kill a client - close the highlighted window
 * send a delete message and remove the client */
void killclient() {
    DEBUG("killclient: entering");
    desktop *d = &desktops[selmon->curr_dtop];
    if (!d->current) return;
    xcb_icccm_get_wm_protocols_reply_t reply; unsigned int n = 0; bool got = false;
    if (xcb_icccm_get_wm_protocols_reply(dis,
        xcb_icccm_get_wm_protocols(dis, d->current->win, wmatoms[WM_PROTOCOLS]),
        &reply, NULL)) { /* TODO: Handle error? */
        for(; n != reply.atoms_len; ++n) 
            if ((got = reply.atoms[n] == wmatoms[WM_DELETE_WINDOW])) 
                break;
        xcb_icccm_get_wm_protocols_reply_wipe(&reply);
    }
    if (got) deletewindow(d->current->win);
    else xcb_kill_client(dis, d->current->win);
    DEBUG("killclient: leaving");
}

void launchmenu(const Arg *arg) {
    DEBUG("launchmenu: entering");
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
            DEBUG("launchmenu: found menu");
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
                DEBUG("launchmenu: entering XCB_EXPOSE");
                // loop through menu_entries
                i = 0;
                for (Menu_Entry *mentry = m->head; mentry; mentry = mentry->next) {
                    DEBUG("launchmenu: drawing iteration");
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
                DEBUG("launchmenu: entering XCB_BUTTON_PRESS");
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
                    focus(desktops[selmon->curr_dtop].current, &desktops[selmon->curr_dtop]);

                break;
            }
            case XCB_KEY_RELEASE: {
                DEBUG("launchmenu: entering XCB_KEY_RELEASE");
                break; 
            }
            default: {
                DEBUG("launchmenu: unknown event");
                // Unknown event type, ignore it
                break;
            }
        }
        // Free the Generic Event
        free (e);
    }
    DEBUG("launchmenu: leaving");
}

/* grab the pointer and get it's current position
 * all pointer movement events will be reported until it's ungrabbed
 * until the mouse button has not been released,
 * grab the interesting events - button press/release and pointer motion
 * and on on pointer movement resize or move the window under the curson.
 * if the received event is a map request or a configure request call the
 * appropriate handler, and stop listening for other events.
 * Ungrab the poitner and event handling is passed back to run() function.
 * Once a window has been moved or resized, it's marked as floating. */
void mousemotion(const Arg *arg) {
    DEBUG("mousemotion: entering");
    desktop *d = &desktops[selmon->curr_dtop];

    xcb_get_geometry_reply_t  *geometry;
    xcb_query_pointer_reply_t *pointer;
    xcb_grab_pointer_reply_t  *grab_reply;
    int mx, my, winx, winy, winw, winh, xw, yh;

    if (!d->current) return;
    geometry = xcb_get_geometry_reply(dis, xcb_get_geometry(dis, d->current->win), NULL); /* TODO: error handling */
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

    if (!d->current->isfloating) d->current->isfloating = true;
    //retile(d, selmon); 
    focus(d->current, d);

    xcb_generic_event_t *e = NULL;
    xcb_motion_notify_event_t *ev = NULL;
    bool ungrab = false;
    do {
        if (e) free(e); xcb_flush(dis);
        while(!(e = xcb_wait_for_event(dis))) xcb_flush(dis);
        switch (e->response_type & ~0x80) {
            case XCB_CONFIGURE_REQUEST: case XCB_MAP_REQUEST:
                events[e->response_type & ~0x80](e);
                break;
            case XCB_MOTION_NOTIFY:
                ev = (xcb_motion_notify_event_t*)e;
                xw = (arg->i == MOVE ? winx : winw) + ev->root_x - mx;
                yh = (arg->i == MOVE ? winy : winh) + ev->root_y - my;
                if (arg->i == RESIZE) { 
                    xcb_resize(dis, d->current->win, (d->current->w = xw>MINWSZ?xw:winw), ( d->current->h = yh>MINWSZ?yh:winh));
                    setclientborders(d, d->current);
                } else if (arg->i == MOVE) 
                    xcb_move(dis, d->current->win, (d->current->x = xw), (d->current->y = yh));
                xcb_flush(dis);
                break;
            case XCB_KEY_PRESS:
            case XCB_KEY_RELEASE:
            case XCB_BUTTON_PRESS:
            case XCB_BUTTON_RELEASE:
                ungrab = true;
        }
    } while(!ungrab && d->current);
    xcb_ungrab_pointer(dis, XCB_CURRENT_TIME);
    DEBUG("mousemotion: leaving");
}

void moveclient(const Arg *arg) {
    DEBUG("moveclient: entering");
    desktop *d = &desktops[selmon->curr_dtop];
    client *c = d->current, **list; 

    if (!c) {
        DEBUG("moveclient: leaving, no d->current");
        return;
    }

    if(d->mode == TILE) { //capable of having windows below?
        int n = d->count;
        DEBUGP("moveclient: d->count = %d\n", d->count);
        c = d->current;
        list = (client**)malloc_safe(n * sizeof(client*));
        (arg->m)(&n, c, list, d);
        free(list);
    }
    DEBUG("moveclient: leaving");
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
        setclientborders(d, list[0]);
        setclientborders(d, c);
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
        setclientborders(d, list[0]);
        setclientborders(d, c);
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
        setclientborders(d, list[0]);
        setclientborders(d, c);
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
        setclientborders(d, list[0]);
        setclientborders(d, c);
    }
    DEBUG("moveclientup: leaving");
}

void movefocus(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    client *c = d->current, **list;

    if(d->mode == TILE) { //capable of having windows to the right?
        int n = d->count;
        DEBUGP("movefocusdown: d->count = %d\n", d->count);
        c = d->current;
        list = (client**)malloc_safe(n * sizeof(client*)); 
        findtouchingclients[arg->i](d, c, list, &n);
        if (list[0] != NULL) focus(list[0], d);
        free(list);
    }
}

void pushtotiling() {
    DEBUG("pushtotiling: entering");
    desktop *d = &desktops[selmon->curr_dtop];
    int gap = d->gap;
    client *c = NULL, *n = d->current; // the client to push
    monitor *m = selmon;
    
    if (!n->isfloating && !n->istransient) // then it must already be tiled
        return;

    n->isfloating = false;
    n->istransient = false;
 
    if (d->count == 0) { // no tiled clients
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
    } else if (d->prevfocus && !ISFT(d->prevfocus))
        c = d->prevfocus;
    // else find the client behind it
    // else find the client behind the pointer
    // else just find the first tiled client
    else if (d->head && (d->head != n))
        c = d->head;

    if(!c) {
        DEBUG("pushtotiling: leaving, error, !c");
        return;
    }

    tiledirection[d->direction](n, c); 
        
    d->count += 1;

    adjustclientgaps(gap, c);
    adjustclientgaps(gap, n);
    
    if (d->mode == TILE || d->mode == FLOAT) {
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
    
        setclientborders(d, c);
        setclientborders(d, n);
    } else 
        monocle(m->x, m->y, m->w, m->h, d, m);
    
    DEBUG("pushtotiling: leaving");
}

/* to quit just stop receiving events
 * run() is stopped and control is back to main()
 */
void quit(const Arg *arg) {
    retval = arg->i;
    running = false;
}

void resizeclient(const Arg *arg) {
    DEBUG("resizeclient: entering"); 
    desktop *d = &desktops[selmon->curr_dtop];
    client *c, **list;

    c = d->current;
    if (!c) {
        DEBUG("resizeclient: leaving, no d->current");
        return;
    }
    monitor *m = wintomon(c->win);

    int n = d->count;
    DEBUGP("resizeclient: d->count = %d\n", d->count);
    list = (client**)malloc_safe(n * sizeof(client*)); 

    (arg->r)(d, arg->i, &n, arg->d, c, m, list);
    free(list);
    setdesktopborders(d); // TODO: we could propably move this to the individual functions below
    DEBUG("resizeclient: leaving");
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

/* execute a command */
void spawn(const Arg *arg) {
    if (fork()) return;
    if (dis) close(screen->root);
    setsid();
    execvp((char*)arg->com[0], (char**)arg->com);
    fprintf(stderr, "error: execvp %s", (char *)arg->com[0]);
    perror(" failed"); /* also prints the err msg */
    exit(EXIT_SUCCESS);
}

//switch the tiling direction
void switch_direction(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    if (d->mode != TILE) {
        d->mode = TILE;
        retile(d, selmon);
    }
    if (d->direction != arg->i) d->direction = arg->i;
    desktopinfo();
}

// switch the tiling mode or to floating mode,
void switch_mode(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    if (d->mode != arg->i) d->mode = arg->i;
    retile(d, selmon); // we need to retile when switching from video/monocle to tile/float
    desktopinfo();
}

/* toggle visibility state of the panel */
void togglepanel() {
    desktop *d = &desktops[selmon->curr_dtop];
    d->showpanel = !d->showpanel;
    retile(d, selmon);
}

/* UTILITIES */

static void gettitle(client *c) {
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

static monitor* ptrtomon(int x, int y) {
    monitor *m;
    int i;

    for(i = 0, m = mons; i < nmons; m = m->next, i++)
        if(INRECT(x, y, m->x, m->y, m->w, m->h))
            return m;
    return selmon;
}

void adjustclientgaps(const int gap, client *c) {
        if (c->xp == 0) c->gapx = gap;
        else c->gapx = gap/2;
        if (c->yp == 0) c->gapy = gap;
        else c->gapy = gap/2;
        if ((c->xp + c->wp) > 0.99999) c->gapw = gap;
        else c->gapw = gap/2;
        if ((c->yp + c->hp) > 0.99999) c->gaph = gap;
        else c->gaph = gap/2;
}

bool clientstouchingbottom(desktop *d, client *c, client **list, int *num) {
    DEBUG("clientstouchingbottom: entering");
    if((c->yp + c->hp) < 1) { //capable of having windows below?
        float width;
        (*num) = 0;
        width = c->wp;
        for (client *n = d->head; n; n = n->next) {
            if ((c != n ) && !ISFT(c) && (n->yp == (c->yp + c->hp))) { // directly below
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
                        if (width < 0.00001) {
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
            if ((c != n ) && !ISFT(c) && (c->xp == (n->xp + n->wp))) { // directly to the left
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
                        DEBUGP("clientstouchingleft: height = %f\n", height);
                        if (height < 0.00001) {
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
            if ((c != n ) && !ISFT(c) && (n->xp == (c->xp + c->wp))) { //directly to the right
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
                        if (height < 0.00001) {
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
            if ((c != n) && !ISFT(c) && (c->yp == (n->yp + n->hp))) {// directly above
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
                        if (width < 0.00001) {
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
    setdesktopborders(d); // TODO: see if we can change this to the client one
    gettitle(c);
    if (CLICK_TO_FOCUS) 
        grabbuttons(c);
    if (ISFT(c))
        xcb_raise_window(dis, c->win);

    xcb_change_property(dis, XCB_PROP_MODE_REPLACE, screen->root, netatoms[NET_ACTIVE], XCB_ATOM_WINDOW, 32, 1, &d->current->win);
    xcb_set_input_focus(dis, XCB_INPUT_FOCUS_POINTER_ROOT, d->current->win, XCB_CURRENT_TIME); 
    xcb_flush(dis);
    
    desktopinfo();
    DEBUG("focus: leaving");
}

bool getrootptr(int *x, int *y) {
    xcb_query_pointer_reply_t *reply = xcb_query_pointer_reply(dis, xcb_query_pointer(dis, screen->root), NULL);

    *x = reply->root_x;
    *y = reply->root_y;

    free(reply);

    return true;
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

void* malloc_safe(size_t size) {
    void *ret;
    if(!(ret = malloc(size)))
        puts("malloc_safe: fatal: could not malloc()");
    memset(ret, 0, size);
    return ret;
}

/* get the previous client from the given
 *  * if no such client, return NULL */
client* prev_client(client *c, desktop *d) {
    if (!c || !d->head->next)
        return NULL;
    client *p;
    for (p = d->head; p->next && p->next != c; p = p->next);
    return p;
}

void setclientborders(desktop *d, client *c) {
    DEBUG("setclientborders: entering"); 
    unsigned int values[1];  /* this is the color maintainer */
    unsigned int zero[1];
    int half;
    
    zero[0] = 0;
    values[0] = BORDER_WIDTH; /* Set border width. */ 

    // find n = number of windows with set borders
    int n = d->count;
    DEBUGP("setclientborders: d->count = %d\n", d->count);

    // rules for no border
    if ((n == 1) || (d->mode == MONOCLE) || (d->mode == VIDEO)) {
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
        
        xcb_change_gc(dis, gc, XCB_GC_FOREGROUND, 
                        (c->isurgent ? &win_urgent:c->istransient ? &win_trn:c->isfloating ? &win_flt:&win_outer));
        xcb_poly_fill_rectangle(dis, pmap, gc, 4, rect_outer);

        xcb_change_gc(dis, gc, XCB_GC_FOREGROUND, (c == d->current ? &win_focus:&win_unfocus));
        xcb_poly_fill_rectangle(dis, pmap, gc, 5, rect_inner);
        xcb_change_window_attributes(dis,c->win, XCB_CW_BORDER_PIXMAP, &pmap);
        /* free the memory we allocated for the pixmap */
        xcb_free_pixmap(dis,pmap);
        xcb_free_gc(dis,gc);
    }
    xcb_flush(dis);
    DEBUG("setclientborders: leaving");
}

void setdesktopborders(desktop *d) {
    DEBUG("setdesktopborders: entering");  
    client *c = NULL;
    for (c = d->head; c; c = c -> next)
        setclientborders(d, c);
    DEBUG("setdesktopborders: leaving");
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

/* wrapper to get xcb keycodes from keysymbol */
xcb_keycode_t* xcb_get_keycodes(xcb_keysym_t keysym) {
    xcb_key_symbols_t *keysyms;
    xcb_keycode_t     *keycode;

    if (!(keysyms = xcb_key_symbols_alloc(dis))) return NULL;
        keycode = xcb_key_symbols_get_keycode(keysyms, keysym);
    xcb_key_symbols_free(keysyms);

    return keycode;
}

/* EVENTS */

#define CLEANMASK(mask) (mask & ~(numlockmask | XCB_MOD_MASK_LOCK))

/* wrapper to get xcb keysymbol from keycode */
static xcb_keysym_t xcb_get_keysym(xcb_keycode_t keycode) {
    xcb_key_symbols_t *keysyms;
    xcb_keysym_t       keysym;

    if (!(keysyms = xcb_key_symbols_alloc(dis))) return 0;
    keysym = xcb_key_symbols_get_keysym(keysyms, keycode, 0);
    xcb_key_symbols_free(keysyms);

    return keysym;
}

/* wrapper to window get attributes using xcb */
static void xcb_get_attributes(xcb_window_t *windows, xcb_get_window_attributes_reply_t **reply, unsigned int count) {
    xcb_get_window_attributes_cookie_t cookies[count];
    for (unsigned int i = 0; i < count; i++) cookies[i] = xcb_get_window_attributes(dis, windows[i]);
    for (unsigned int i = 0; i < count; i++) reply[i]   = xcb_get_window_attributes_reply(dis, cookies[i], NULL); /* TODO: Handle error */
}

/* create a new client and add the new window
 * window should notify of property change events
 */
static client* addwindow(xcb_window_t w, desktop *d) {
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

/* find which client the given window belongs to */
static client *wintoclient(xcb_window_t w) {
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

/* remove the specified client
 *
 * note, the removing client can be on any desktop,
 * we must return back to the current focused desktop.
 * if c was the previously focused, prevfocus must be updated
 * else if c was the current one, current must be updated. */
static void removeclient(client *c, desktop *d, const monitor *m) {
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

    if (!ISFT(c)) {
        d->count -= 1;
        dead = (client *)malloc_safe(sizeof(client));
        *dead = *c;
        dead->next = NULL;
        if (!d->dead) {
            DEBUG("removeclient: d->dead == NULL");
            d->dead = dead;
        }
        else {
            for (n = d->dead; n && n->next; n = n->next);
            n->next = dead;
        }
        tileremove(d, m);
    } 
    free(c); c = NULL; 
    setdesktopborders(d); // TODO: see if we can handle this individually in tileremove
    desktopinfo();
    DEBUG("removeclient: leaving");
}


/* on the press of a button check to see if there's a binded function to call 
 * TODO: if we make the mouse able to switch monitors we could eliminate a call
 *       to wintomon */
void buttonpress(xcb_generic_event_t *e) {
    DEBUG("buttonpress: entering");
    xcb_button_press_event_t *ev = (xcb_button_press_event_t*)e; 
    monitor *m = wintomon(ev->event);
    client *c = wintoclient(ev->event);

    if (CLICK_TO_FOCUS && ev->detail == XCB_BUTTON_INDEX_1) {
        if (m && m != selmon)
            selmon = m; 
     
        if (c && c != desktops[selmon->curr_dtop].current) 
            focus(c, &desktops[m->curr_dtop]);

        desktopinfo();
    }

    for (unsigned int i=0; i<LENGTH(buttons); i++)
        if (buttons[i].func && buttons[i].button == ev->detail &&
            CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state)) {
            if (desktops[m->curr_dtop].current != c) focus(c, &desktops[m->curr_dtop]);
            buttons[i].func(&(buttons[i].arg));
        }

    DEBUG("buttonpress: leaving");
}

/* To change the state of a mapped window, a client MUST
 * send a _NET_WM_STATE client message to the root window
 * message_type must be _NET_WM_STATE
 *   data.l[0] is the action to be taken
 *   data.l[1] is the property to alter three actions:
 *   - remove/unset _NET_WM_STATE_REMOVE=0
 *   - add/set _NET_WM_STATE_ADD=1,
 *   - toggle _NET_WM_STATE_TOGGLE=2
 *
 * check if window requested fullscreen or activation */
void clientmessage(xcb_generic_event_t *e) {
    DEBUG("clientmessage: entering");

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
        focus(c, d);

    DEBUG("clientmessage: leaving");
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

/* a configure request means that the window requested changes in its geometry
 * state. if the window doesnt have a client set the appropriate values as 
 * requested, else fake it.
 */
void configurerequest(xcb_generic_event_t *e) {
    DEBUG("configurerequest: entering");
    xcb_configure_request_event_t *ev = (xcb_configure_request_event_t*)e; 
    unsigned int v[7];
    unsigned int i = 0;
    monitor *m; client *c; 
   
    if (!(c = wintoclient(ev->window))) { // if it has no client, configure it
        if ((m = wintomon(ev->window))) {
            DEBUGP("configurerequest: x: %d y: %d w: %d h: %d\n", ev->x, ev->y, ev->width, ev->height);

            if (ev->value_mask & XCB_CONFIG_WINDOW_X) {
                DEBUGP("configurerequest: m->x: %d\n", m->x);
                if (ev->x > m->x)
                    v[i++] = ev->x;
                else
                    v[i++] = (m->x + ev->x);
            }
            if (ev->value_mask & XCB_CONFIG_WINDOW_Y) {
                DEBUGP("configurerequest: m->y: %d\n", m->y);
                if (ev->y > m->y)
                    v[i++] = (ev->y + (desktops[m->curr_dtop].showpanel && TOP_PANEL) ? PANEL_HEIGHT : 0);
                else 
                    v[i++] = ((m->y + ev->y) + (desktops[m->curr_dtop].showpanel && TOP_PANEL) ? PANEL_HEIGHT : 0);
            }
        }
        else {
            if (ev->value_mask & XCB_CONFIG_WINDOW_X) {
                DEBUGP("configurerequest: selmon->x: %d\n", selmon->x);
                v[i++] = ev->x;
            }
            if (ev->value_mask & XCB_CONFIG_WINDOW_Y) {
                DEBUGP("configurerequest: selmon->y: %d\n", selmon->y);
                v[i++] = (ev->y + (desktops[selmon->curr_dtop].showpanel && TOP_PANEL) ? PANEL_HEIGHT : 0);
            }
        }

        if (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH) {
            DEBUG("configurerequest: width");
            v[i++] = (ev->width  < selmon->w - BORDER_WIDTH) ? ev->width  : selmon->w - BORDER_WIDTH;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
            DEBUG("configurerequest: height");
            v[i++] = (ev->height < selmon->h - BORDER_WIDTH) ? ev->height : selmon->h - BORDER_WIDTH;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
            DEBUG("configurerequest: border_width");
            v[i++] = ev->border_width;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_SIBLING) {
            DEBUG("configurerequest: sibling");
            v[i++] = ev->sibling;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
            DEBUG("configurerequest: stack_mode");
            v[i++] = ev->stack_mode;
        }
        xcb_configure_window_checked(dis, ev->window, ev->value_mask, v);
    }
    else { // has a client, fake configure it
        xcb_send_event(dis, false, c->win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char*)ev);
    }
    xcb_flush(dis);
    DEBUG("configurerequest: leaving");
}

/* a destroy notification is received when a window is being closed
 * on receival, remove the appropriate client that held that window
 */
void destroynotify(xcb_generic_event_t *e) {
    DEBUG("destroynotify: entering");   
    xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t*)e;
    client *c = wintoclient(ev->window);
    desktop *d = clienttodesktop(c);
    monitor *m = wintomon(ev->window);
    if (c) 
        removeclient(c, d, m); 
    desktopinfo();
    DEBUG("destroynotify: leaving");
}

/* when the mouse enters a window's borders
 * the window, if notifying of such events (EnterWindowMask)
 * will notify the wm and will get focus */
void enternotify(xcb_generic_event_t *e) {
    DEBUG("enternotify: entering");
    xcb_enter_notify_event_t *ev = (xcb_enter_notify_event_t*)e;  

    if (!FOLLOW_MOUSE || ev->mode != XCB_NOTIFY_MODE_NORMAL || ev->detail == XCB_NOTIFY_DETAIL_INFERIOR) {
        DEBUG("enternotify: leaving under user FOLLOW_MOUSE setting or event rules to not enter");
        return;
    }

    client *c = wintoclient(ev->event);
    if (!c) {
        DEBUG("enternotify: leaving NULL client");
        return;
    }
    
    monitor *m = NULL;
    if((m = wintomon(ev->event)) && m != selmon)
        selmon = m;

    desktop *d = &desktops[m->curr_dtop]; 
    DEBUGP("enternotify: c->xp: %f c->yp: %f c->wp: %f c->hp: %f\n", (m->w * c->xp), (m->h * c->yp), (m->w * c->wp), (m->h * c->hp));
    focus(c, d);
    desktopinfo();
    DEBUG("enternotify: leaving");
}

// Expose event means we should redraw our windows
void expose(xcb_generic_event_t *e) {
    monitor *m;
    xcb_expose_event_t *ev = (xcb_expose_event_t*)e;

    if(ev->count == 0 && (m = wintomon(ev->window))){
        //redraw windoos - xcb_flush?
        desktopinfo();
    }
}    

//events which are generated by clients
void focusin(xcb_generic_event_t *e) { /* there are some broken focus acquiring clients */
    DEBUG("focusin: entering");
    xcb_focus_in_event_t *ev = (xcb_focus_in_event_t*)e;

    if (ev->mode == XCB_NOTIFY_MODE_GRAB || ev->mode == XCB_NOTIFY_MODE_UNGRAB) {
        DEBUG("focusin: event for grab/ungrab, ignoring");
        return;
    }

    if (ev->detail == XCB_NOTIFY_DETAIL_POINTER) {
        DEBUG("focusin: notify detail is pointer, ignoring this event");
        return;
    }

    if(!desktops[selmon->curr_dtop].current && ev->event != desktops[selmon->curr_dtop].current->win)
        xcb_set_input_focus(dis, XCB_INPUT_FOCUS_POINTER_ROOT, desktops[selmon->curr_dtop].current->win,
                            XCB_CURRENT_TIME);

    DEBUG("focusin: leaving");
}

/* on the press of a key check to see if there's a binded function to call */
void keypress(xcb_generic_event_t *e) {
    DEBUG("keypress: entering");
    xcb_key_press_event_t *ev       = (xcb_key_press_event_t *)e;
    xcb_keysym_t           keysym   = xcb_get_keysym(ev->detail);
    DEBUGP("xcb: keypress: code: %d mod: %d\n", ev->detail, ev->state);
    for (unsigned int i=0; i < LENGTH(keys); i++)
        if (keysym == keys[i].keysym && CLEANMASK(keys[i].mod) == CLEANMASK(ev->state) && keys[i].func)
                keys[i].func(&keys[i].arg);

    DEBUG("keypress: leaving");
}

void mappingnotify(xcb_generic_event_t *e) {
    xcb_mapping_notify_event_t *ev = (xcb_mapping_notify_event_t*)e;
    //xcb_keysym_t           keysym   = xcb_get_keysym(ev->detail);

    //xcb_refresh_keyboard_mapping(keysym, ev);
    if(ev->request == XCB_MAPPING_NOTIFY)
        grabkeys();
}

/* a map request is received when a window wants to display itself
 * if the window has override_redirect flag set then it should not be handled
 * by the wm. if the window already has a client then there is nothing to do.
 *
 * get the window class and name instance and try to match against an app rule.
 * create a client for the window, that client will always be current.
 * check for transient state, and fullscreen state and the appropriate values.
 * if the desktop in which the window was spawned is the current desktop then
 * display the window, else, if set, focus the new desktop.
 */
void maprequest(xcb_generic_event_t *e) {
    DEBUG("maprequest: entering");
    client *c = NULL; 
    xcb_map_request_event_t            *ev = (xcb_map_request_event_t*)e;
    xcb_window_t                       windows[] = { ev->window }, transient = 0;
    xcb_get_window_attributes_reply_t  *attr[1];
    xcb_icccm_get_wm_class_reply_t     ch;
    xcb_get_geometry_reply_t           *geometry;
    xcb_get_property_reply_t           *prop_reply;

    xcb_get_attributes(windows, attr, 1);
    if (!attr[0] || attr[0]->override_redirect) return;
    c = wintoclient(ev->window);
    if (c) return; 

    bool follow = false, floating = false;
    int cd = selmon->curr_dtop, newdsk = selmon->curr_dtop;
    if (xcb_icccm_get_wm_class_reply(dis, xcb_icccm_get_wm_class(dis, ev->window), &ch, NULL)) { /* TODO: error handling */
        DEBUGP("class: %s instance: %s\n", ch.class_name, ch.instance_name);
        for (unsigned int i=0; i<LENGTH(rules); i++)
            if (strstr(ch.class_name, rules[i].class) || strstr(ch.instance_name, rules[i].class)) {
                follow = rules[i].follow;
                newdsk = (rules[i].desktop < 0) ? selmon->curr_dtop:rules[i].desktop;
                floating = rules[i].floating;
                break;
            }
        xcb_icccm_get_wm_class_reply_wipe(&ch);
    } 
     
    if (cd != newdsk) selmon->curr_dtop = newdsk;
    c = addwindow(ev->window, &desktops[newdsk]);

    xcb_icccm_get_wm_transient_for_reply(dis, xcb_icccm_get_wm_transient_for_unchecked(dis, ev->window), &transient, NULL); /* TODO: error handling */
    c->istransient = transient?true:false;
    c->isfloating  = floating || desktops[newdsk].mode == FLOAT || c->istransient;

    if (c->istransient || c->isfloating) {
        if ((geometry = xcb_get_geometry_reply(dis, xcb_get_geometry(dis, ev->window), NULL))) { /* TODO: error handling */
            DEBUGP("geom: %ux%u+%d+%d\n", geometry->width, geometry->height, geometry->x, geometry->y);
            c->x = selmon->x + geometry->x;
            c->y = selmon->y + geometry->y;
            c->w = geometry->width;
            c->h = geometry->height;
            free(geometry);
        }
    }

    if (!ISFT(c))
        desktops[newdsk].count += 1;
        
    prop_reply  = xcb_get_property_reply(dis, xcb_get_property_unchecked(dis, 0, ev->window, netatoms[NET_WM_STATE], XCB_ATOM_ATOM, 0, 1), NULL); /* TODO: error handling */
    if (prop_reply) { 
        free(prop_reply);
    } 

    monitor *m = wintomon(c->win);
    if (cd == newdsk) {
        tilenew(&desktops[selmon->curr_dtop], selmon); 
        xcb_map_window(dis, c->win);  
    }
    else if (follow)
        change_desktop(&(Arg){.i = newdsk}); 
    focus(c, &desktops[m->curr_dtop]);
    grabbuttons(c);
    
    if (!follow)
        desktopinfo();

    DEBUG("maprequest: leaving");
}

/* property notify is called when one of the window's properties
 * is changed, such as an urgent hint is received
 */
void propertynotify(xcb_generic_event_t *e) {
    DEBUG("propertynotify: entering");
    xcb_property_notify_event_t *ev = (xcb_property_notify_event_t*)e;
    xcb_icccm_wm_hints_t wmh;
    client *c;
 
    if (ev->atom != XCB_ICCCM_WM_ALL_HINTS) {
        DEBUG("propertynotify: leaving, ev->atom != XCB_ICCCM_WM_ALL_HINTS");
        return;
    }
    c = wintoclient(ev->window);
    if (!c) { 
        DEBUG("propertynotify: leaving, NULL client");
        return;
    } 
    if (xcb_icccm_get_wm_hints_reply(dis, xcb_icccm_get_wm_hints(dis, ev->window), &wmh, NULL)) { /* TODO: error handling */
        c->isurgent = c != desktops[selmon->curr_dtop].current && (wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY);
        DEBUG("propertynotify: got hint!");
        return;
    }
    desktopinfo();

    DEBUG("propertynotify: leaving");
}

/* windows that request to unmap should lose their
 * client, so no invisible windows exist on screen
 */
void unmapnotify(xcb_generic_event_t *e) {
    DEBUG("unmapnotify: entering");
    xcb_unmap_notify_event_t *ev = (xcb_unmap_notify_event_t *)e;
    client *c = wintoclient(ev->window);
    monitor *m = wintomon(ev->window);
    if (c && ev->event != screen->root) removeclient(c, &desktops[selmon->curr_dtop], m);
    desktopinfo();
    DEBUG("unmapnotify: leaving");
}

/* TILING */

/* each window should cover all the available screen space */
void monocle(int x, int y, int w, int h, const desktop *d, const monitor *m) {
    DEBUG("monocle: entering");
    int gap = d->gap; 
    for (client *c = d->head; c; c = c->next) 
        if (d->mode == VIDEO)
            xcb_move_resize(dis, c->win, x, (y - ((m->haspanel && TOP_PANEL) ? PANEL_HEIGHT:0)), w, (h + ((m->haspanel && !TOP_PANEL) ? PANEL_HEIGHT:0)));
        else
            xcb_move_resize(dis, c->win, (x + gap), (y + gap), (w - 2*gap), (h - 2*gap));
    DEBUG("monocle: leaving");
}

void retile(desktop *d, const monitor *m) {
    DEBUG("retile: entering");
    int gap = d->gap;

    if (d->mode == TILE || d->mode == FLOAT) {
        int n = d->count;
        DEBUGP("retile: d->count = %d\n", d->count);
       
        for (client *c = d->head; c; c=c->next) {
            if (!ISFT(c)) {
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
            } else
                xcb_move_resize(dis, c->win, c->x, c->y, c->w, c->h);
        } 
    }
    else
        monocle(m->x, m->y, m->w, m->h, d, m);
    setdesktopborders(d);

    DEBUG("retile: leaving");
}

void tilenew(desktop *d, const monitor *m) {
    DEBUG("tilenew: entering");
    client *c = d->current, *n, *dead = d->dead;
    int gap = d->gap;

    if (!d->head || d->mode == FLOAT) return; // nothing to arange 
    for (n = d->head; n && n->next; n = n->next);
    if (ISFT(n))
        xcb_move_resize(dis, n->win, n->x, n->y, n->w, n->h);    
    else if (n == d->head) {
        DEBUG("tilenew: tiling empty monitor");
        n->xp = 0; n->yp = 0; n->wp = 1; n->hp = 1;
        if (m != NULL) {
            xcb_move_resize(dis, n->win, 
                            (n->x = m->x + (m->w * n->xp) + gap), 
                            (n->y = m->y + (m->h * n->yp) + gap), 
                            (n->w = (m->w * n->wp) - 2*gap), 
                            (n->h = (m->h * n->hp) - 2*gap));
        }
        if (dead) {
            for ( ; d->dead; ) {
                d->dead = d->dead->next;
                free(dead); 
                dead = d->dead;
            }
        }
    }
    else if (dead) {
        DEBUG("tilenew: tiling empty space");
        //also need to assign n->xp etc
        n->xp = dead->xp; n->yp = dead->yp; n->wp = dead->wp; n->hp = dead->hp;
        n->gapx = dead->gapx; n->gapy = dead->gapy; n->gapw = dead->gapw; n->gaph = dead->gaph;
        if (m != NULL) {
            xcb_move_resize(dis, n->win, 
                            (n->x = m->x + (m->w * n->xp) + n->gapx), 
                            (n->y = m->y + (m->h * n->yp) + n->gapy), 
                            (n->w = (m->w * n->wp) - 2*BORDER_WIDTH - n->gapx - n->gapw), 
                            (n->h = (m->h * n->hp) - 2*BORDER_WIDTH - n->gapy - n->gaph));
        }
        d->dead = d->dead->next;
        free(dead); dead = NULL;
    }
    else {
        tiledirection[d->direction](n, c);
        adjustclientgaps(gap, c);
        adjustclientgaps(gap, n);

        if (m != NULL) { 
            if (d->mode != MONOCLE && d->mode != VIDEO) {
                xcb_move_resize(dis, c->win,
                                (c->x = m->x + (m->w * c->xp) + c->gapx), 
                                (c->y = m->y + (m->h * c->yp) + c->gapy), 
                                (c->w = (m->w * c->wp) - 2*BORDER_WIDTH - c->gapx - c->gapw),
                                (c->h = (m->h * c->hp) - 2*BORDER_WIDTH - c->gapy - c->gaph));
                DEBUGP("tilenew: tiling current x:%f y:%f w:%f h:%f\n", (m->w * c->xp), (m->h * c->yp), (m->w * c->wp) , (m->h * c->hp));

                xcb_move_resize(dis, n->win, 
                                (n->x = m->x + (m->w * n->xp) + n->gapx), 
                                (n->y = m->y + (m->h * n->yp) + n->gapy), 
                                (n->w = (m->w * n->wp) - 2*BORDER_WIDTH - n->gapx - n->gapw), 
                                (n->h = (m->h * n->hp) - 2*BORDER_WIDTH - n->gapy - n->gaph));
                DEBUGP("tilenew: tiling new x:%f y:%f w:%f h:%f\n", (m->w * n->xp), (m->h * n->yp), (m->w * n->wp), (m->h * n->hp));
            }
            else
                monocle(m->x, m->y, m->w, m->h, d, m);
        }
    }

    DEBUG("tilenew: leaving");
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

void tileremove(desktop *d, const monitor *m) {
    DEBUG("tileremove: entering");
    int gap = d->gap, n = 0;
    client *dead = d->dead, **list;

    n = d->count;
    DEBUGP("tileremove: d->count = %d\n", d->count);

    if (n == 1) {
        DEBUG("tileremove: only one client; fitting screen");
        client *c = d->head;
        c->xp = 0; c->yp = 0; c->wp = 1; c->hp = 1;

        if ((m != NULL) && (d->mode == TILE || d->mode == FLOAT)) {
            adjustclientgaps(gap, c);
            xcb_move_resize(dis, c->win, 
                            (c->x = m->x + (m->w * c->xp) + c->gapx), 
                            (c->y = m->y + (m->h * c->yp) + c->gapy), 
                            (c->w = (m->w * c->wp) - 2*c->gapw), 
                            (c->h = (m->h * c->hp) - 2*c->gaph));
        }
        for ( ; d->dead; ) {
            d->dead = d->dead->next;
            free(dead); 
            dead = d->dead;
        }
        DEBUG("tileremove: leaving");
        return;
    }

    list = (client**)malloc_safe(n * sizeof(client*));

    if (findtouchingclients[TTOP](d, dead, list, &n)) {
        // clients in list should gain the emptyspace
        for (int i = 0; i < n; i++) {
            list[i]->hp += dead->hp;
            if (m != NULL && (d->mode == TILE || d->mode == FLOAT)) {
                adjustclientgaps(gap, list[i]);
                xcb_move_resize(dis, list[i]->win, 
                                list[i]->x, 
                                list[i]->y, 
                                list[i]->w, 
                                (list[i]->h = (m->h * list[i]->hp) - 2*BORDER_WIDTH - list[i]->gapy - list[i]->gaph));
            }
        }
        d->dead = d->dead->next;
        free(dead); dead = NULL;
        free(list);
        DEBUG("tileremove: leaving");
        return;
    }

    if (findtouchingclients[TLEFT](d, dead, list, &n)) {
        // clients in list should gain the emptyspace
        for (int i = 0; i < n; i++) {
            list[i]->wp += dead->wp;
            if (m != NULL && (d->mode == TILE || d->mode == FLOAT)) {
                adjustclientgaps(gap, list[i]);
                xcb_move_resize(dis, list[i]->win, 
                                list[i]->x, 
                                list[i]->y, 
                                (list[i]->w = (m->w * list[i]->wp) - 2*BORDER_WIDTH - list[i]->gapx - list[i]->gapw), 
                                list[i]->h);
            }
        }
        d->dead = d->dead->next;
        free(dead); dead = NULL;
        free(list);
        DEBUG("tileremove: leaving");
        return;
    }

    if (findtouchingclients[TBOTTOM](d, dead, list, &n)) {
        // clients in list should gain the emptyspace
        for (int i = 0; i < n; i++) {
            list[i]->yp = dead->yp;
            list[i]->hp += dead->hp;
            if (m != NULL && (d->mode == TILE || d->mode == FLOAT)) {
                adjustclientgaps(gap, list[i]);
                xcb_move_resize(dis, list[i]->win, 
                                list[i]->x, 
                                (list[i]->y = m->y + (m->h * list[i]->yp) + list[i]->gapy), 
                                list[i]->w, 
                                (list[i]->h = (m->h * list[i]->hp) - 2*BORDER_WIDTH - list[i]->gapy - list[i]->gaph));
            }
        }
        d->dead = d->dead->next;
        free(dead); dead = NULL;
        free(list);
        DEBUG("tileremove: leaving");
        return;
    }
    
    if (findtouchingclients[TRIGHT](d, dead, list, &n)) {
        // clients in list should gain the emptyspace
        for (int i = 0; i < n; i++) {
            list[i]->xp = dead->xp;
            list[i]->wp += dead->wp;
            if (m != NULL && (d->mode == TILE || d->mode == FLOAT)) {
                adjustclientgaps(gap, list[i]);
                xcb_move_resize(dis, list[i]->win, 
                                (list[i]->x = m->x + (m->w * list[i]->xp) + list[i]->gapx), 
                                list[i]->y, 
                                (list[i]->w = (m->w * list[i]->wp) - 2*BORDER_WIDTH - list[i]->gapx - list[i]->gapw), 
                                list[i]->h);
            }
        }
        d->dead = d->dead->next;
        free(dead); dead = NULL;
        free(list);
        DEBUG("tileremove: leaving");
        return;
    }

    free(list);
    DEBUG("tileremove: leaving, nothing tiled");
}

/* FRANKENSTEINWM */

static char *WM_ATOM_NAME[]   = { "WM_PROTOCOLS", "WM_DELETE_WINDOW" };
static char *NET_ATOM_NAME[]  = { "_NET_SUPPORTED", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE", "_NET_WM_NAME", "_NET_ACTIVE_WINDOW" };

#define USAGE           "usage: frankensteinwm [-h] [-v]"

/* get screen of display */
static xcb_screen_t *xcb_screen_of_display(xcb_connection_t *con, int screen) {
    xcb_screen_iterator_t iter;
    iter = xcb_setup_roots_iterator(xcb_get_setup(con));
    for (; iter.rem; --screen, xcb_screen_next(&iter)) if (screen == 0) return iter.data;
    return NULL;
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


/* remove all windows in all desktops by sending a delete message */
static void cleanup(void) {
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

static Menu_Entry* createmenuentry(int x, int y, int w, int h, char *cmd) {
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

static Menu* createmenu(char **list) {
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

static monitor* createmon(xcb_randr_output_t id, int x, int y, int w, int h, int dtop) {
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

/* get a pixel with the requested color
 * to fill some window area - borders */
static unsigned int getcolor(char* color) {
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

static void getoutputs(xcb_randr_output_t *outputs, const int len, xcb_timestamp_t timestamp) {
    DEBUG("getoutputs: entering");
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
                        DEBUG("getoutputs: adjusting monitor");
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
                DEBUG("getoutputs: adding monitor");
                for(m = mons; m && m->next; m = m->next);
                if(m) {
                    DEBUG("getoutputs: entering m->next = createmon");
                    m->next = createmon(outputs[i], crtc->x, crtc->y, crtc->width, crtc->height, ++nmons);
                }
                else {
                    DEBUG("getoutputs: entering mon = createmon");
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
                    DEBUG("getoutputs: deleting monitor");
                    free(m);
                    nmons--;
                    break;
                } 
            }
        }
        free(output);
    }
    DEBUG("getoutputs: leaving");
}

static void getrandr(void) { // Get RANDR resources and figure out how many outputs there are.
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

static void initializexresources() {
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
    // TODO: get font, XrmGetResouce
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

/* main event loop - on receival of an event call the appropriate event handler */
static void run(void) {
    DEBUG("run: entered");
    xcb_generic_event_t *ev; 
    while(running) {
        DEBUG("run: running");
        xcb_flush(dis);
        if (xcb_connection_has_error(dis)) {
            DEBUG("run: x11 connection got interrupted");
            err(EXIT_FAILURE, "error: X11 connection got interrupted\n");
        }
        if ((ev = xcb_wait_for_event(dis))) {
            if (ev->response_type==randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
                DEBUG("run: entering getrandr()");
                getrandr();
            }
            if (events[ev->response_type & ~0x80]) {
                DEBUGP("run: entering event %d\n", ev->response_type & ~0x80);
                events[ev->response_type & ~0x80](ev);
            }
            else { DEBUGP("xcb: unimplented event: %d\n", ev->response_type & ~0x80); }
            free(ev);
        }
    }
    DEBUG("run: leaving");
}

/* get numlock modifier using xcb */
static int setup_keyboard(void) {
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

static void sigchld() {
    if (signal(SIGCHLD, sigchld) == SIG_ERR)
        err(EXIT_FAILURE, "cannot install SIGCHLD handler");
    while(0 < waitpid(-1, NULL, WNOHANG));
}

static int setuprandr(void) { // Set up RANDR extension. Get the extension base and subscribe to
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

/* set initial values
 * root window - screen height/width - atoms - xerror handler
 * set masks for reporting events handled by the wm
 * and propagate the suported net atoms
 */
static int setup(int default_screen) {
    sigchld();
    screen = xcb_screen_of_display(dis, default_screen);
    if (!screen) err(EXIT_FAILURE, "error: cannot aquire screen\n");
    
    randrbase = setuprandr();
    //DEBUG("exited setuprandr, continuing setup");

    selmon = mons; 
    for (unsigned int i=0; i<DESKTOPS; i++)
        desktops[i] = (desktop){ .mode = DEFAULT_MODE, .direction = DEFAULT_DIRECTION, .showpanel = SHOW_PANEL, .gap = GAP, .count = 0, };

    win_focus   = getcolor(FOCUS);
    win_unfocus = getcolor(UNFOCUS);
    win_outer   = getcolor(OTRBRDRCOL);
    win_urgent  = getcolor(URGNBRDRCOL);
    win_flt     = getcolor(FLTBRDCOL);
    win_trn     = getcolor(TRNBDRCOL);


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

    /* setup keyboard */
    if (setup_keyboard() == -1)
        err(EXIT_FAILURE, "error: failed to setup keyboard\n");

    /* set up atoms for dialog/notification windows */
    xcb_get_atoms(WM_ATOM_NAME, wmatoms, WM_COUNT);
    xcb_get_atoms(NET_ATOM_NAME, netatoms, NET_COUNT);

    /* check if another wm is running */
    if (xcb_checkotherwm())
        err(EXIT_FAILURE, "error: other wm is running\n");

    xcb_change_property(dis, XCB_PROP_MODE_REPLACE, screen->root, netatoms[NET_SUPPORTED], XCB_ATOM_ATOM, 32, NET_COUNT, netatoms);
    xcb_flush(dis);
    grabkeys();

    /* set events */
    for (unsigned int i=0; i<XCB_NO_OPERATION; i++) events[i] = NULL;
    events[XCB_BUTTON_PRESS]                = buttonpress;
    events[XCB_CLIENT_MESSAGE]              = clientmessage;
    events[XCB_CONFIGURE_REQUEST]           = configurerequest;
    //events[XCB_CONFIGURE_NOTIFY]            = configurenotify;
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

    //DEBUG("setup: about to switch to default desktop");
    if (DEFAULT_DESKTOP >= 0 && DEFAULT_DESKTOP < DESKTOPS)
        change_desktop(&(Arg){.i = DEFAULT_DESKTOP});
    DEBUG("leaving setup");
    return 0;
}

int main(int argc, char *argv[]) {
    int default_screen;
    if (argc == 2 && argv[1][0] == '-') switch (argv[1][1]) {
        case 'v': errx(EXIT_SUCCESS, "%s - by Derek Taaffe", VERSION);
        case 'h': errx(EXIT_SUCCESS, "%s", USAGE);
        default: errx(EXIT_FAILURE, "%s", USAGE);
    } else if (argc != 1) errx(EXIT_FAILURE, "%s", USAGE);
    if (xcb_connection_has_error((dis = xcb_connect(NULL, &default_screen))))
        errx(EXIT_FAILURE, "error: cannot open display\n");
    if (setup(default_screen) != -1) {
      desktopinfo(); /* zero out every desktop on (re)start */
      run();
    }
    cleanup();
    xcb_disconnect(dis);
    return retval;
}

/* vim: set ts=4 sw=4 :*/

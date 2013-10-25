/* see license for copyright and license */

#include "frankensteinwm.h"

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
    if (!ISFFT(c))
        d->count -= 1;
    DEBUGP("client_to_desktop: d->count = %d\n", d->count);

    if (!flag) { // window is not moving to another monitor 
        xcb_unmap_window(dis, c->win);
    }

    dead = (client *)malloc_safe(sizeof(client));
    *dead = *c;
    dead->next = NULL;
    if (!d->dead) {
        DEBUG("removeclient: d->dead == NULL");
        d->dead = dead;
    }
    else {
        for (itr = d->dead; itr; itr = itr->next);
        itr = dead;
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
    retile(d, selmon); 
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
                if (arg->i == RESIZE) xcb_resize(dis, d->current->win, xw>MINWSZ?xw:winw, yh>MINWSZ?yh:winh);
                else if (arg->i == MOVE) xcb_move(dis, d->current->win, xw, yh);
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
    setborders(d);
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

void switch_direction(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    if (d->mode != TILE) {
        d->mode = TILE;
        if (d->mode != FLOAT)
            for (client *c = d->head; c; c = c->next) 
                c->isfloating = false;
        retile(d, selmon);
    }
    if (d->direction != arg->i)
        d->direction = arg->i;

    desktopinfo();
}

/* switch the tiling mode and reset all floating windows */
void switch_mode(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    if (d->mode != arg->i) 
        d->mode = arg->i;
    if (d->mode != FLOAT)
        for (client *c = d->head; c; c = c->next) 
            c->isfloating = false;
    retile(d, selmon);
    
    desktopinfo();
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

/* toggle visibility state of the panel */
void togglepanel() {
    desktop *d = &desktops[selmon->curr_dtop];
    d->showpanel = !d->showpanel;
    retile(d, selmon);
}

/* vim: set ts=4 sw=4 :*/

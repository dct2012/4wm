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

/* vim: set ts=4 sw=4 :*/

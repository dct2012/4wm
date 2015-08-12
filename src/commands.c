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
    
    #if PRETTY_PRINT
    updatews();
    updatemode();
    updatedir();
    desktopinfo();
    #endif
}

// move a client to another desktop
//
// remove the current client from the current desktop's client list
// and add it as last client of the new desktop's client list
void client_to_desktop(const Arg *arg) {
    if (arg->i == selmon->curr_dtop || arg->i < 0 || arg->i >= DESKTOPS || !desktops[selmon->curr_dtop].current) 
        return;
   
    // see if window will be moving to another monitor
    bool flag = false; monitor *m = NULL; int i;
    for (i = 0, m = mons; i < nmons; m = m->next, i++)
        if ((m->curr_dtop == arg->i) && m != selmon) {
            flag = true;
            break;
        }

    desktop *d = &desktops[selmon->curr_dtop], *n = &desktops[arg->i];
    client *c = d->current, *p = prev_client(d->current, d), *l = prev_client(n->head, n);
    
    if (c == d->head || !p) 
        d->head = c->next; 
    else 
        p->next = c->next;
    c->next = NULL;
    if (!ISFT(c)) {
        d->count--;
        n->count++;
        tileremove(c, d, selmon);
    }
    DEBUGP("client_to_desktop: d->count = %d\nclient_to_desktop: n->count = %d\n", d->count, n->count);

    if (!flag) { // window is not moving to another monitor 
        xcb_unmap_window(dis, c->win);
    }
 
    focus(d->prevfocus, d, selmon);
    if (l)
        l->next = c;
    else if (n->head)
        n->head->next = c;
    else
        n->head = c;  

    m = wintomon(n->head->win);
    tilenew(n, m); // itll be ok if m == NULL 
    focus(c, n, m);

    #if PRETTY_PRINT
    updatews();
    desktopinfo();
    #endif
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

// find and focus the client which received
// the urgent hint in the current desktop
void focusurgent() {
    client *c = NULL;
    int d = -1;
    for (c=desktops[selmon->curr_dtop].head; c && !c->isurgent; c=c->next);
    while (!c && d < DESKTOPS-1) 
        for (c = desktops[++d].head; c && !c->isurgent; c = c->next);
    if (c) { 
        change_desktop(&(Arg){.i = --d}); 
        focus(c, &desktops[selmon->curr_dtop], selmon);
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

    // what is probably meant here, is for when FOLLOW_MOUSE is set to false, we will need to focus the client under mouse.
    //focus(c, d); 

    xcb_generic_event_t *e = NULL;
    xcb_motion_notify_event_t *ev = NULL;
    bool ungrab = c->isfloating ? false:true;
    while (!ungrab && c) {
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
                    xcb_resize(dis, c->win, (c->w = xw>MINWSZ?xw:winw), ( c->h = yh>MINWSZ?yh:winh));
                    setclientborders(d, d->current, selmon);
                } else if (arg->i == MOVE) {  
                    xcb_move(dis, c->win, (c->x = xw), (c->y = yh));
            
                    // handle floater moving monitors
                    if (!INRECT(xw, yh, selmon->x, selmon->y, selmon->w, selmon->h)) {
                        monitor *m = NULL;
                        for (m = mons; m && !INRECT(xw, yh, m->x, m->y, m->w, m->h); m = m->next);
                        if (m) { // we found a monitor
                            desktop *n = &desktops[m->curr_dtop];
                            client *p = prev_client(d->current, d), *l = prev_client(n->head, n);
                            // pull client from desktop
                            if (c == d->head || !p) 
                                d->head = c->next; 
                            else 
                                p->next = c->next;
                            c->next = NULL;   
                            // add to new desktop
                            if (l)
                                l->next = c;
                            else if (n->head)
                                n->head->next = c;
                            else
                                n->head = c;  
                            monitor *mold = selmon;
                            selmon = m;
                            focus(c, n, m); //readjust focus for new desktop
                            focus(d->prevfocus, d, mold); // readjust the focus from that desktop
                            d = &desktops[m->curr_dtop];
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
}

void moveclient(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    client *c = d->current, **list; 

    if (!c || c->isfloating) {
        DEBUG("moveclient: leaving, no d->current or c->isfloating\n");
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
}

void movefocus(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    client *c = d->current, **list;

    if((d->mode == TILE || d->mode == FLOAT) && !c->isfloating) { //capable of having windows to the right?
        int n = d->count;
        DEBUGP("movefocus: d->count = %d\n", n);
        list = (client**)malloc_safe(n * sizeof(client*)); 
        findtouchingclients[arg->i](d, c, list, &n);
        if (list[0] != NULL) focus(list[0], d, selmon);
        free(list);
    }
    else if (d->mode == MONOCLE || d->mode == VIDEO) {
        DEBUG("movefocus: monocle or video\n"); 
        
        if(arg->i == TTOP || arg->i == TRIGHT){
            if(c->next)
                focus(c->next, d, selmon);
            else {
                focus(d->head, d, selmon);
            }
        }
        else {
            client *n;
            for(n = d->head; n->next && n->next != c; n = n->next);
            focus(n, d, selmon);
        }

        retile(d, selmon);
    }
}

// cyclic focus the next window
// if the window is the last on stack, focus head
void next_win() {
    desktop *d = &desktops[selmon->curr_dtop];
    
    if (!d->current || !d->head->next) 
        return;
    
    focus(d->current->next ? d->current->next:d->head, d, selmon);
    
    if(d->mode == VIDEO || d->mode == MONOCLE)
        retile(d, selmon);
}

// cyclic focus the previous window
// if the window is the head, focus the last stack window
void prev_win() {
    desktop *d = &desktops[selmon->curr_dtop];
    
    if (!d->current || !d->head->next) 
        return;
    
    focus(prev_client(d->prevfocus = d->current, d), d, selmon);

    if(d->mode == VIDEO || d->mode == MONOCLE)
        retile(d, selmon);
}

void pulltofloat() {
    desktop *d = &desktops[selmon->curr_dtop];
    client *c = d->current;

    if (!c->isfloating) {
        c->isfloating = true;
        d->count--;
        tileremove(c, d, selmon);
    
        // move it to the center of the screen
        xcb_move_resize(dis, c->win, (c->x = selmon->w/2 - c->w/2), (c->y = selmon->h/2 - c->h/2), c->w, c->h);
        xcb_raise_window(dis, c->win);
    }
}

void pushtotiling() {
    desktop *d = &desktops[selmon->curr_dtop];
    int gap = d->gap;
    client *c = NULL, *n = d->current; // the client to push
    monitor *m = selmon;
    
    if (!n->isfloating && !n->istransient) // then it must already be tiled
        return;

    n->isfloating = false;
    n->istransient = false;
 
    if (d->count == 0) { // no tiled clients
        n->xp = 0; n->yp = 0; n->wp = 100; n->hp = 100;
        adjustclientgaps(gap, n);
        d->count++;
        xcb_move_resize(dis, n->win, 
                            (n->x = m->x + n->gapx), 
                            (n->y = m->y + n->gapy), 
                            (n->w = m->w - 2*n->gapw), 
                            (n->h = m->h - 2*n->gaph));
        setclientborders(d, n, selmon);
        DEBUG("pushtotiling: leaving, tiled only client on desktop\n");
        return;
    } else if (d->prevfocus)
        c = d->prevfocus;
    if (c && c->isfloating) {
        // try to find the first one behind the pointer
        xcb_query_pointer_reply_t *pointer = xcb_query_pointer_reply(dis, xcb_query_pointer(dis, screen->root), 0);
        if (!pointer) return;
        int mx = pointer->root_x; int my = pointer->root_y;
        for (c = d->head; c; c = c->next)
            if(!ISFT(c) && INRECT(mx, my, c->x, c->y, c->w, c->h))
                break;
        // just find the first tiled client.
        if (!c)
            for (c = d->head; c; c = c->next)
                if(!ISFT(c))
                    break;
    } 

    if(!c) {
        // TODO: get rid of this check
        DEBUG("pushtotiling: leaving, error, !c\n");
        return;
    }

    tiledirection[d->direction](n, c); 
        
    d->count++;

    adjustclientgaps(gap, c);
    adjustclientgaps(gap, n);
    
    if (d->mode == TILE || d->mode == FLOAT) {
        xcb_move_resize(dis, c->win,
                        (c->x = m->x + (m->w * (double)c->xp / 100) + c->gapx), 
                        (c->y = m->y + (m->h * (double)c->yp / 100) + c->gapy), 
                        (c->w = (m->w * (double)c->wp / 100) - 2*BORDER_WIDTH - c->gapx - c->gapw),
                        (c->h = (m->h * (double)c->hp / 100) - 2*BORDER_WIDTH - c->gapy - c->gaph));
        DEBUGP("pushtotiling: tiling current x:%f y:%f w:%f h:%f\n", 
                (m->w * (double)c->xp / 100), (m->h * (double)c->yp / 100), (m->w * (double)c->wp / 100) , (m->h * (double)c->hp / 100));

        xcb_move_resize(dis, n->win, 
                        (n->x = m->x + (m->w * (double)n->xp / 100) + n->gapx), 
                        (n->y = m->y + (m->h * (double)n->yp / 100) + n->gapy), 
                        (n->w = (m->w * (double)n->wp / 100) - 2*BORDER_WIDTH - n->gapx - n->gapw), 
                        (n->h = (m->h * (double)n->hp / 100) - 2*BORDER_WIDTH - n->gapy - n->gaph));
        DEBUGP("pushtotiling: tiling new x:%f y:%f w:%f h:%f\n", 
                (m->w * (double)n->xp / 100), (m->h * (double)n->yp / 100), (m->w * (double)n->wp / 100), (m->h * (double)n->hp / 100));
    
        setclientborders(d, c, selmon);
        setclientborders(d, n, selmon);
    } else 
        monocle(m->x, m->y, m->w, m->h, d, m);
}

// to quit just stop receiving events
// run() is stopped and control is back to main()
event_return_val quit(const Arg *arg) {
    event_return_val r;
    r.running = false;
    
    if(arg->i)
        r.status = EXIT_FAILURE;
    else
        r.status = EXIT_SUCCESS;
    
    running = false;
}

void resizeclient(const Arg *arg) {
    desktop *d = &desktops[selmon->curr_dtop];
    client *c, **list;
    
    if(d->mode != VIDEO || d->mode != MONOCLE) {
        c = d->current;
        if (!c) {
            DEBUG("resizeclient: leaving, no d->current\n");
            return;
        }
        monitor *m = wintomon(c->win);

        int n = d->count;
        DEBUGP("resizeclient: d->count = %d\n", d->count);
        list = (client**)malloc_safe(n * sizeof(client*)); 

        (arg->r)(d, arg->i, &n, arg->p, c, m, list);
        free(list);
    }
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

// toggle visibility state of the panel
void togglepanel() {
    desktop *d = &desktops[selmon->curr_dtop];
    d->showpanel = !d->showpanel;
    retile(d, selmon);
}

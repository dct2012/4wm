// remove the specified client
//
// note, the removing client can be on any desktop,
// we must return back to the current focused desktop.
// if c was the previously focused, prevfocus must be updated
// else if c was the current one, current must be updated.
static void removeclient(client *c, desktop *d, const monitor *m) {
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

    if (!c->isfloating) {
        d->count--;
        tileremove(c, d, m);
    } else if (d->current)
        setclientborders(d, d->current, m);
    free(c->title);
    free(c); c = NULL; 
    #if PRETTY_PRINT
    updatews();
    desktopinfo();
    #endif
}

// find which client the given window belongs to
static client *wintoclient(xcb_window_t w) {
    client *c = NULL;
    int i;
 
    for (i = 0; i < DESKTOPS; i++)
        for (c = desktops[i].head; c; c = c->next)
            if(c->win == w) {
                DEBUG("wintoclient: leaving, returning found client\n");
                return c;
            }
    
    return NULL;
}

// create a new client and add the new window
// window should notify of property change events
static client* addwindow(xcb_window_t w, desktop *d) {
    client *c, *t = prev_client(d->head, d);
 
    if (!(c = (client *)malloc_safe(sizeof(client)))) err(EXIT_FAILURE, "cannot allocate client");

    if (!d->head) d->head = c;
    else if (t) t->next = c; 
    else d->head->next = c;

    DEBUGP("addwindow: d->count = %d\n", d->count);

    unsigned int values[1] = { XCB_EVENT_MASK_PROPERTY_CHANGE|(FOLLOW_MOUSE?XCB_EVENT_MASK_ENTER_WINDOW:0) };
    xcb_change_window_attributes_checked(dis, (c->win = w), XCB_CW_EVENT_MASK, values);
    return c;
}

void setclientborders(desktop *d, client *c, const monitor *m) {
    unsigned int values[1];  /* this is the color maintainer */
    unsigned int zero[1];
    int half;
    
    zero[0] = 0;
    values[0] = BORDER_WIDTH; // Set border width.

    // find n = number of windows with set borders
    int n = d->count;
    DEBUGP("setclientborders: d->count = %d\n", d->count);

    // rules for no border
    if ((!c->isfloating && n == 1) || (d->mode == MONOCLE) || (d->mode == VIDEO) || c->istransient) {
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
        
        xcb_change_gc(dis, gc, XCB_GC_FOREGROUND, 
                        (c->isurgent ? &win_urgent:c->isfloating ? &win_flt:&win_outer));
        xcb_poly_fill_rectangle(dis, pmap, gc, 4, rect_outer);

        xcb_change_gc(dis, gc, XCB_GC_FOREGROUND, (c == d->current && m == selmon ? &win_focus:&win_unfocus));
        xcb_poly_fill_rectangle(dis, pmap, gc, 5, rect_inner);
        xcb_change_window_attributes(dis,c->win, XCB_CW_BORDER_PIXMAP, &pmap);
        // free the memory we allocated for the pixmap
        xcb_free_pixmap(dis,pmap);
        xcb_free_gc(dis,gc);
    }
    xcb_flush(dis);
}

// get the previous client from the given
// if no such client, return NULL
client* prev_client(client *c, desktop *d) {
    if (!c || !d->head->next)
        return NULL;
    client *p;
    for (p = d->head; p->next && p->next != c; p = p->next);
    return p;
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
    setdesktopborders(d, m);
        
    xcb_change_property(dis, XCB_PROP_MODE_REPLACE, screen->root, netatoms[NET_ACTIVE], XCB_ATOM_WINDOW, 32, 1, &c->win);
    xcb_set_input_focus(dis, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
    xcb_flush(dis);
     
    #if PRETTY_PRINT
    desktopinfo();
    #endif
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

void adjustclientgaps(const int gap, client *c) {
        if (c->xp == 0) c->gapx = gap;
        else c->gapx = gap/2;
        if (c->yp == 0) c->gapy = gap;
        else c->gapy = gap/2;
        if ((c->xp + c->wp) == 100) c->gapw = gap;
        else c->gapw = gap/2;
        if ((c->yp + c->hp) == 100) c->gaph = gap;
        else c->gaph = gap/2;
}

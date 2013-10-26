/* see license for copyright and license */

#include "frankensteinwm.h"

/* variables */
bool running = true;
int randrbase, retval = 0, nmons = 0;
unsigned int numlockmask = 0, win_unfocus, win_focus, win_outer, win_urgent;
xcb_connection_t *dis;
xcb_screen_t *screen;
xcb_atom_t wmatoms[WM_COUNT], netatoms[NET_COUNT];
desktop desktops[DESKTOPS];
monitor *mons = NULL, *selmon = NULL;
Menu *menus = NULL;
Xresources xres;

static bool clientstouchingbottom(desktop *d, client *c, client **list, int *num) {
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

static bool clientstouchingleft(desktop *d, client *c, client **list, int *num) {
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

static bool clientstouchingright(desktop *d, client *c, client **list, int *num) {
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

static bool clientstouchingtop(desktop *d, client *c, client **list, int *num) {
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

bool (*findtouchingclients[TDIRECS])(desktop *d, client *c, client **list, int *num) = {
    [TBOTTOM] = clientstouchingbottom, [TLEFT] = clientstouchingleft, [TRIGHT] = clientstouchingright, [TTOP] = clientstouchingtop,
};

void (*tiledirection[TDIRECS])(client *n, client *c) = {
    [TBOTTOM] = tilenewbottom, [TLEFT] = tilenewleft, [TRIGHT] = tilenewright, [TTOP] = tilenewtop,
};

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

/* vim: set ts=4 sw=4 :*/

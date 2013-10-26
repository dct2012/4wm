/* see license for copyright and license */

#include "frankensteinwm.h"

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

    if (c->istransient) {
        if ((geometry = xcb_get_geometry_reply(dis, xcb_get_geometry(dis, ev->window), NULL))) { /* TODO: error handling */
            DEBUGP("geom: %ux%u+%d+%d\n", geometry->width, geometry->height, geometry->x, geometry->y);
            c->x = selmon->x + geometry->x;
            c->y = selmon->y + geometry->y;
            c->w = geometry->width;
            c->h = geometry->height;
            free(geometry);
        }
    }

    if (!ISFFT(c))
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

/* vim: set ts=4 sw=4 :*/

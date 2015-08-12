#if MENU
static Menu_Entry* createmenuentry(int x, int y, int w, int h, char *cmd) {
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
    return m;
}

static Menu* createmenu(char **list) {
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
    return m;
}
#endif

#if MENU
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
}
#endif

#if MENU
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
}
#endif

#define INRECT(X,Y,RX,RY,RW,RH) ((X) >= (RX) && (X) < (RX) + (RW) && (Y) >= (RY) && (Y) < (RY) + (RH))


static monitor* ptrtomon(int x, int y) {
    monitor *m;
    int i;

    for(i = 0, m = mons; i < nmons; m = m->next, i++)
        if(INRECT(x, y, m->x, m->y, m->w, m->h))
            return m;
    return selmon;
}

// find which monitor the given window belongs to
monitor *wintomon(xcb_window_t w) {
    int x, y;
    monitor *m; client *c;
    int i; 

    if(w == screen->root && getrootptr(&x, &y)) {
        DEBUG("wintomon: leaving, returning ptrtomon\n");
        return ptrtomon(x, y);
    }
     
    for (i = 0, m = mons; i < nmons; m = m->next, i++)
        for (c = desktops[m->curr_dtop].head; c; c = c->next)
            if(c->win == w) {
                DEBUG("wintomon: leaving, returning found monitor\n");
                return m;
            }
    
    DEBUG("wintomon: leaving, returning NULL monitor\n");
    return NULL;
}

static monitor* createmon(xcb_randr_output_t id, int x, int y, int w, int h, int dtop) {
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
    return m;
}

static monitor* ptrtomon(int x, int y) {
    monitor *m;
    int i;

    for(i = 0, m = mons; i < nmons; m = m->next, i++)
        if(INRECT(x, y, m->x, m->y, m->w, m->h))
            return m;
    return selmon;
}


void fullscreen(client *c, int x, int y, int w, int h, const desktop *d, const monitor *m) {
    xcb_raise_window(dis, c->win);
    if (d->mode == VIDEO)
        xcb_move_resize(dis, c->win, x, (y - ((m->haspanel && TOP_PANEL) ? PANEL_HEIGHT:0)), w, (h + ((m->haspanel && !TOP_PANEL) ? PANEL_HEIGHT:0)));
    else
        xcb_move_resize(dis, c->win, (x + d->gap), (y + d->gap), (w - 2*d->gap), (h - 2*d->gap));
}

// each window should cover all the available screen space
void monocle(int x, int y, int w, int h, const desktop *d, const monitor *m) {
    if(d->head){
        client *c;
        for (c = d->head; c; c = c->next) {
            if(c != d->current){
                fullscreen(c, x, y, w, h, d, m);
            }
        }
        
        c = d->current; 
        fullscreen(c, x, y, w, h, d, m);
    }
}

void retile(desktop *d, const monitor *m) {
    int gap = d->gap;

    if (d->mode == TILE || d->mode == FLOAT) {
        DEBUGP("retile: d->count = %d\n", d->count);
       
        for (client *c = d->head; c; c=c->next) {
            if (!c->isfloating) {
                xcb_lower_window(dis, c->win);
                if (d->count == 1) {
                    c->gapx = c->gapy = c->gapw = c->gaph = gap;
                    xcb_move_resize(dis, c->win, 
                                    (c->x = m->x + (m->w * (double)c->xp / 100) + gap), 
                                    (c->y = m->y + (m->h * (double)c->yp / 100) + gap), 
                                    (c->w = (m->w * (double)c->wp / 100) - 2*gap), 
                                    (c->h = (m->h * (double)c->hp / 100) - 2*gap));
                }
                else { 
                    xcb_move_resize(dis, c->win, 
                                    (c->x = m->x + (m->w * (double)c->xp / 100) + c->gapx), 
                                    (c->y = m->y + (m->h * (double)c->yp / 100) + c->gapy), 
                                    (c->w = (m->w * (double)c->wp / 100) - 2*BORDER_WIDTH - c->gapx - c->gapw), 
                                    (c->h = (m->h * (double)c->hp / 100) - 2*BORDER_WIDTH - c->gapy - c->gaph));
                }
            } else {
                for ( ; c->x > m->x + m->w; c->x -= m->w);
                for ( ; c->y > m->y + m->h; c->y -= m->h);

                for ( ; c->x < m->x; c->x += m->w);
                for ( ; c->y < m->y; c->y += m->h);
                
                xcb_raise_window(dis, c->win);
                xcb_move_resize(dis, c->win, c->x, c->y, c->w, c->h);
            }
        } 
    }
    else
        monocle(m->x, m->y, m->w, m->h, d, m);
    setdesktopborders(d, m);
}

void tilenew(desktop *d, const monitor *m) {
    client *c = d->current, *n;
    int gap = d->gap; 

    if (!d->head) {
        DEBUG("tilenew: leaving, nothing to arrange\n");
        return; // nothing to arange
    } 

    for (n = d->head; n && n->next; n = n->next);
    if (ISFT(n)) {
        xcb_move_resize(dis, n->win, n->x, n->y, n->w, n->h);
        xcb_raise_window(dis, n->win);
    } else if (d->count == 1) {
        DEBUG("tilenew: tiling empty monitor\n");
        n->xp = 0; n->yp = 0; n->wp = 100; n->hp = 100;
        if (m != NULL) {
            if (d->mode == VIDEO) {
                xcb_move_resize(dis, n->win, m->x, (m->y - ((m->haspanel && TOP_PANEL) ? PANEL_HEIGHT:0)), m->w, (m->h + ((m->haspanel && !TOP_PANEL) ? PANEL_HEIGHT:0)));
                xcb_raise_window(dis, n->win);
            } else {
                xcb_move_resize(dis, n->win, (m->x + gap), (m->y + gap), (m->w - 2*gap), (m->h - 2*gap)); 
                xcb_lower_window(dis, n->win);
            }
        } 
    } else {
        tiledirection[d->direction](n, c); 
        adjustclientgaps(gap, c);
        adjustclientgaps(gap, n);

        if (m != NULL) { 
            if (d->mode != MONOCLE && d->mode != VIDEO) {
                xcb_move_resize(dis, c->win,
                                (c->x = m->x + (m->w * (double)c->xp / 100) + c->gapx), 
                                (c->y = m->y + (m->h * (double)c->yp / 100) + c->gapy), 
                                (c->w = (m->w * (double)c->wp / 100) - 2*BORDER_WIDTH - c->gapx - c->gapw),
                                (c->h = (m->h * (double)c->hp / 100) - 2*BORDER_WIDTH - c->gapy - c->gaph));
                DEBUGP("tilenew: tiling current x:%f y:%f w:%f h:%f\n", 
                        (m->w * (double)c->xp / 100), (m->h * (double)c->yp / 100), (m->w * (double)c->wp / 100) , (m->h * (double)c->hp / 100));
                xcb_lower_window(dis, c->win);
                xcb_move_resize(dis, n->win, 
                                (n->x = m->x + (m->w * (double)n->xp / 100) + n->gapx), 
                                (n->y = m->y + (m->h * (double)n->yp / 100) + n->gapy), 
                                (n->w = (m->w * (double)n->wp / 100) - 2*BORDER_WIDTH - n->gapx - n->gapw), 
                                (n->h = (m->h * (double)n->hp / 100) - 2*BORDER_WIDTH - n->gapy - n->gaph));
                DEBUGP("tilenew: tiling new x:%f y:%f w:%f h:%f\n", 
                        (m->w * (double)n->xp / 100), (m->h * (double)n->yp / 100), (m->w * (double)n->wp / 100), (m->h * (double)n->hp / 100));
                xcb_lower_window(dis, n->win);
            }
            else {
                focus(n, d, m);
                monocle(m->x, m->y, m->w, m->h, d, m);
            }
        }
    }
}

void tilenewbottom(client *n, client *c) {
    n->xp = c->xp;
    n->yp = c->yp + (c->hp / 2);
    n->wp = c->wp;
    n->hp = (c->yp + c->hp) - n->yp;
    c->hp = n->yp - c->yp;
}

void tilenewleft(client *n, client *c) {
    n->xp = c->xp;
    n->yp = c->yp;
    n->wp = c->wp / 2;
    n->hp = c->hp;
    c->xp = n->xp + n->wp;
    c->wp = (n->xp + c->wp) - c->xp;
}

void tilenewright(client *n, client *c) {
    n->xp = c->xp + (c->wp / 2);
    n->yp = c->yp;
    n->wp = (c->xp + c->wp) - n->xp;
    n->hp = c->hp;
    c->wp = n->xp - c->xp;
}

void tilenewtop(client *n, client *c) {
    n->xp = c->xp;
    n->yp = c->yp;
    n->wp = c->wp;
    n->hp = c->hp / 2;
    c->yp = n->yp + n->hp;
    c->hp = (n->yp + c->hp) - c->yp;
}

void tileremove(client *dead, desktop *d, const monitor *m) {
    int gap = d->gap, n = 0;
    client **list;

    n = d->count;
    DEBUGP("tileremove: d->count = %d\n", d->count);

    if (n == 1) {
        DEBUG("tileremove: only one client; fitting screen\n");
        client *c = NULL;
        for (c = d->head; c && c->isfloating; c = c->next); //find the first non-floating
        c->xp = 0; c->yp = 0; c->wp = 100; c->hp = 100;

        if ((m != NULL) && (d->mode == TILE || d->mode == FLOAT)) {
            adjustclientgaps(gap, c);
            xcb_move_resize(dis, c->win, 
                            (c->x = m->x + (m->w * (double)c->xp / 100) + c->gapx), 
                            (c->y = m->y + (m->h * (double)c->yp / 100) + c->gapy), 
                            (c->w = (m->w * (double)c->wp / 100) - 2*c->gapw), 
                            (c->h = (m->h * (double)c->hp / 100) - 2*c->gaph));
            setclientborders(d, c, m);
        }
        DEBUG("tileremove: leaving\n");
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
                                (list[i]->h = (m->h * (double)list[i]->hp / 100) - 2*BORDER_WIDTH - list[i]->gapy - list[i]->gaph));
                setclientborders(d, list[i], m);
            }
        }
        free(list);
        DEBUG("tileremove: leaving\n");
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
                                (list[i]->w = (m->w * (double)list[i]->wp / 100) - 2*BORDER_WIDTH - list[i]->gapx - list[i]->gapw), 
                                list[i]->h);
                setclientborders(d, list[i], m);
            }
        }
        free(list);
        DEBUG("tileremove: leaving\n");
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
                                (list[i]->y = m->y + (m->h * (double)list[i]->yp / 100) + list[i]->gapy), 
                                list[i]->w, 
                                (list[i]->h = (m->h * (double)list[i]->hp / 100) - 2*BORDER_WIDTH - list[i]->gapy - list[i]->gaph));
                setclientborders(d, list[i], m);
            }
        }
        free(list);
        DEBUG("tileremove: leaving\n");
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
                                (list[i]->x = m->x + (m->w * (double)list[i]->xp / 100) + list[i]->gapx), 
                                list[i]->y, 
                                (list[i]->w = (m->w * (double)list[i]->wp / 100) - 2*BORDER_WIDTH - list[i]->gapx - list[i]->gapw), 
                                list[i]->h);
                setclientborders(d, list[i], m);
            }
        }
        free(list);
        DEBUG("tileremove: leaving\n");
        return;
    }

    free(list);
}

// switch the current client with the first client we find below it
void moveclientdown(int *num, client *c, client **list, desktop *d) { 
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
                        (list[0]->x = selmon->x + (selmon->w * (double)list[0]->xp / 100) + list[0]->gapx), 
                        (list[0]->y = selmon->y + (selmon->h * (double)list[0]->yp / 100) + list[0]->gapy), 
                        (list[0]->w = (selmon->w * (double)list[0]->wp / 100) - 2*BORDER_WIDTH - list[0]->gapx - list[0]->gapw), 
                        (list[0]->h = (selmon->h * (double)list[0]->hp / 100) - 2*BORDER_WIDTH - list[0]->gapy - list[0]->gaph));
        xcb_move_resize(dis, c->win, 
                        (c->x = selmon->x + (selmon->w * (double)c->xp / 100) + c->gapx), 
                        (c->y = selmon->y + (selmon->h * (double)c->yp / 100) + c->gapy), 
                        (c->w = (selmon->w * (double)c->wp / 100) - 2*BORDER_WIDTH - c->gapx - c->gapw), 
                        (c->h = (selmon->h * (double)c->hp / 100) - 2*BORDER_WIDTH - c->gapy - c->gaph)); 
        setclientborders(d, list[0], selmon);
        setclientborders(d, c, selmon);
    }
}

// switch the current client with the first client we find to the left of it
void moveclientleft(int *num, client *c, client **list, desktop *d) { 
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
                        (list[0]->x = selmon->x + (selmon->w * (double)list[0]->xp / 100) + list[0]->gapx), 
                        (list[0]->y = selmon->y + (selmon->h * (double)list[0]->yp / 100) + list[0]->gapy), 
                        (list[0]->w = (selmon->w * (double)list[0]->wp / 100) - 2*BORDER_WIDTH - list[0]->gapx - list[0]->gapw), 
                        (list[0]->h = (selmon->h * (double)list[0]->hp / 100) - 2*BORDER_WIDTH - list[0]->gapy - list[0]->gaph));
        xcb_move_resize(dis, c->win, 
                        (c->x = selmon->x + (selmon->w * (double)c->xp / 100) + c->gapx), 
                        (c->y = selmon->y + (selmon->h * (double)c->yp / 100) + c->gapy), 
                        (c->w = (selmon->w * (double)c->wp / 100) - 2*BORDER_WIDTH - c->gapx - c->gapw), 
                        (c->h = (selmon->h * (double)c->hp / 100) - 2*BORDER_WIDTH - c->gapy - c->gaph));
        setclientborders(d, list[0], selmon);
        setclientborders(d, c, selmon);
    }
}

// switch the current client with the first client we find to the right of it
void moveclientright(int *num, client *c, client **list, desktop *d) { 
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
                        (list[0]->x = selmon->x + (selmon->w * (double)list[0]->xp / 100) + list[0]->gapx), 
                        (list[0]->y = selmon->y + (selmon->h * (double)list[0]->yp / 100) + list[0]->gapy), 
                        (list[0]->w = (selmon->w * (double)list[0]->wp / 100) - 2*BORDER_WIDTH - list[0]->gapx - list[0]->gapw), 
                        (list[0]->h = (selmon->h * (double)list[0]->hp / 100) - 2*BORDER_WIDTH - list[0]->gapy - list[0]->gaph));
        xcb_move_resize(dis, c->win, 
                        (c->x = selmon->x + (selmon->w * (double)c->xp / 100) + c->gapx), 
                        (c->y = selmon->y + (selmon->h * (double)c->yp / 100) + c->gapy), 
                        (c->w = (selmon->w * (double)c->wp / 100) - 2*BORDER_WIDTH - c->gapx - c->gapw), 
                        (c->h = (selmon->h * (double)c->hp / 100) - 2*BORDER_WIDTH - c->gapy - c->gaph));
        setclientborders(d, list[0], selmon);
        setclientborders(d, c, selmon);
    }
}

// switch the current client with the first client we find above it
void moveclientup(int *num, client *c, client **list, desktop *d) { 
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
                        (list[0]->x = selmon->x + (selmon->w * (double)list[0]->xp / 100) + list[0]->gapx), 
                        (list[0]->y = selmon->y + (selmon->h * (double)list[0]->yp / 100) + list[0]->gapy), 
                        (list[0]->w = (selmon->w * (double)list[0]->wp / 100) - 2*BORDER_WIDTH - list[0]->gapx - list[0]->gapw), 
                        (list[0]->h = (selmon->h * (double)list[0]->hp / 100) - 2*BORDER_WIDTH - list[0]->gapy - list[0]->gaph));
        xcb_move_resize(dis, c->win, 
                        (c->x = selmon->x + (selmon->w * (double)c->xp / 100) + c->gapx), 
                        (c->y = selmon->y + (selmon->h * (double)c->yp / 100) + c->gapy), 
                        (c->w = (selmon->w * (double)c->wp / 100) - 2*BORDER_WIDTH - c->gapx - c->gapw), 
                        (c->h = (selmon->h * (double)c->hp / 100) - 2*BORDER_WIDTH - c->gapy - c->gaph)); 
        setclientborders(d, list[0], selmon);
        setclientborders(d, c, selmon);
    }
} 

void resizeclientbottom(desktop *d, const int grow, int *n, const int size, client *c, monitor *m, client **list) {
    if (findtouchingclients[TBOTTOM](d, c, list, n)) {
        if (grow) {
            //client in list y increases and height decreases
            for (int i = 0; i < (*n); i++)
                shrinkbyy(NULL, size, list[i], m, d);
            //current windows height increases
            growbyh(list[0], size, c, m, d);
        } else {
            shrinkbyh(NULL, size, c, m, d);
            for (int i = 0; i < (*n); i++)
                growbyy(c, size, list[i], m, d);
        }
    } else if (findtouchingclients[TTOP](d, c, list, n)) {
        if (grow) {
            //current windows y increases and height decreases
            shrinkbyy(NULL, size, c, m, d);
            //client in list height increases
            for (int i = 0; i < (*n); i++)
                growbyh(c, size, list[i], m, d);
        } else {
            for (int i = 0; i < (*n); i++)
                shrinkbyh(NULL, size, list[i], m, d);
            growbyy(list[0], size, c, m, d);
        }
    }
}

void resizeclientleft(desktop *d, const int grow, int *n, const int size, client *c, monitor *m, client **list) {
    if (findtouchingclients[TLEFT](d, c, list, n)) {
        if (grow) {
            //client in list width decreases
            for (int i = 0; i < (*n); i++)
                shrinkbyw(NULL, size, list[i], m, d);
            //the current windows x decreases and width increases
            growbyx(list[0], size, c, m, d);
        } else {
            shrinkbyx(NULL, size, c, m, d);
            for (int i = 0; i < (*n); i++)
                growbyw(c, size, list[i], m, d);
        }
    } else if (findtouchingclients[TRIGHT](d, c, list, n)) {
        if (grow) {
            //current windows width decreases
            shrinkbyw(NULL, size, c, m, d);
            //clients in list x decreases width increases
            for (int i = 0; i < (*n); i++)
                growbyx(c, size, list[i], m, d);
        } else { 
            for (int i = 0; i < (*n); i++)
                shrinkbyx(NULL, size, list[i], m, d);
            growbyw(list[0], size, c, m, d);
        }
    }
}

void resizeclientright(desktop *d, const int grow, int *n, const int size, client *c, monitor *m, client **list) {
    if (findtouchingclients[TRIGHT](d, c, list, n)) { 
        if (grow) {
            //clients in list x increases and width decrease
            for (int i = 0; i < (*n); i++)
                shrinkbyx(NULL, size, list[i], m, d);
            //the current windows width increases
            growbyw(list[0], size, c, m, d);
        } else {
            shrinkbyw(NULL, size, c, m, d);
            for (int i = 0; i < (*n); i++)
                growbyx(c, size, list[i], m, d);
        }
    } else if (findtouchingclients[TLEFT](d, c, list, n)) {
        if (grow) {
            //current windows x increases and width decreases
            shrinkbyx(NULL, size, c, m, d);
            //other windows width increases
            for (int i = 0; i < (*n); i++)
                growbyw(c, size, list[i], m, d);
        } else {
            for (int i = 0; i < (*n); i++)
                shrinkbyw(NULL, size, list[i], m, d);
            growbyx(list[0], size, c, m, d);
        }
    }
}

void resizeclienttop(desktop *d, const int grow, int *n, const int size, client *c, monitor *m, client **list) {
    if (findtouchingclients[TTOP](d, c, list, n)) {
        if (grow) {
            //client in list height decreases
            for (int i = 0; i < (*n); i++)
                shrinkbyh(NULL, size, list[i], m, d);
            //current windows y decreases and height increases
            growbyy(list[0], size, c, m, d);
        } else {
            shrinkbyy(NULL, size, c, m, d);
            for (int i = 0; i < (*n); i++)
                growbyh(c, size, list[i], m, d);
        }
    } else if (findtouchingclients[TBOTTOM](d, c, list, n)) {
        if (grow) {
            //current windows height decreases
            shrinkbyh(NULL, size, c, m, d);
            //client in list y decreases and height increases
            for (int i = 0; i < (*n); i++)
               growbyy(c, size, list[i], m, d);
        }else { 
            for (int i = 0; i < (*n); i++)
                shrinkbyy(NULL, size, list[i], m, d);
            growbyh(list[0], size, c, m, d);
        }
    } 
}

bool clientstouchingbottom(desktop *d, client *c, client **list, int *num) {
    if((c->yp + c->hp) < 100) { //capable of having windows below?
        int width;
        (*num) = 0;
        width = c->wp;
        for (client *n = d->head; n; n = n->next) {
            if ((c != n ) && !ISFT(n) && (n->yp == (c->yp + c->hp))) { // directly below
                if ((n->xp + n->wp) <= (c->xp + c->wp)) { // width equivalent or less than
                    if ((n->xp == c->xp) && (n->wp == c->wp)) { //direct match?
                        DEBUG("clientstouchingbottom: found direct match\n");
                        list[(*num)] = n;
                        (*num)++;
                        DEBUG("clientstouchingbottom: leaving, found direct match\n");
                        return true;
                    }
                    else if (n->xp >= c->xp) { // part
                        width -= n->wp;
                        list[(*num)] = n;
                        (*num)++;
                        if (width == 0) {
                            DEBUG("clientstouchingbottom: leaving true\n");
                            return true;
                        }
                        if (width < 0) {
                            DEBUG("clientstouchingbottom: leaving false\n");
                            return false;
                        }
                    }
                }
                
                if ((n->xp <= c->xp) && ((n->xp + n->wp) >= (c->xp + c->wp))) { 
                    // width exceeds, but we should go ahead and make sure list isnt NULL
                    list[(*num)] = n;
                    DEBUG("clientstouchingbottom: leaving false\n");
                    return false;
                }
            }
        }
    }
    return false;
}

bool clientstouchingleft(desktop *d, client *c, client **list, int *num) {
    if(c->xp > 0) { // capable of having windows to the left?
        int height;
        (*num) = 0;
        height = c->hp;
        for (client *n = d->head; n; n = n->next) {
            DEBUGP("clientstouchingleft: %d == %d\n", c->xp, n->xp + n->wp);
            if ((c != n ) && !ISFT(n) && (c->xp == (n->xp + n->wp))) { // directly to the left
                DEBUGP("clientstouchingleft: %d <= %d\n",n->yp + n->hp, c->yp + c->hp);
                if ((n->yp + n->hp) <= (c->yp + c->hp)) { // height equivalent or less than
                    if ((n->yp == c->yp) && (n->hp == c->hp)) { // direct match?
                        list[(*num)] = n;
                        (*num)++;
                        DEBUG("clientstouchingleft: leaving found direct match\n");
                        return true;
                    }
                    else if (n->yp >= c->yp) { // part
                        height -= n->hp;
                        list[(*num)] = n;
                        (*num)++;
                        DEBUGP("clientstouchingleft: height = %d\n", height);
                        if (height == 0) {
                            DEBUG("clientstouchingleft: leaving true\n");
                            return true;
                        }
                        if (height < 0) {
                            DEBUG("clientstouchingleft: leaving false\n");
                            return false;
                        }
                    }
                }
                
                if ((n->yp <= c->yp) && ((n->yp + n->hp) >= (c->yp + c->hp))) { 
                    // height exceeds, but we should go ahead and make sure list isnt NULL
                    list[(*num)] = n;
                    DEBUG("clientstouchingleft: leaving false\n");
                    return false;
                }
            }
        }
    }
    return false;
}

bool clientstouchingright(desktop *d, client *c, client **list, int *num) {
    if((c->xp + c->wp) < 100) { // capable of having windows to the right?
        int height;
        (*num) = 0;
        height = c->hp;
        for (client *n = d->head; n; n = n->next) {
            if ((c != n ) && !ISFT(n) && (n->xp == (c->xp + c->wp))) { // directly to the right
                if ((n->yp + n->hp) <= (c->yp + c->hp)) { // height equivalent or less than
                    if ((n->yp == c->yp) && (n->hp == c->hp)) { // direct match?
                        list[(*num)] = n;
                        (*num)++;
                        DEBUG("clientstouchingright: leaving, found direct match\n");
                        return true;
                    }
                    else if (n->yp >= c->yp) { // part
                        height -= n->hp;
                        list[(*num)] = n;
                        (*num)++;
                        if (height == 0) {
                            DEBUG("clientstouchingright: leaving true\n");
                            return true;
                        }
                        if (height < 0) {
                            DEBUG("clientstouchingright: leaving false\n");
                            return false;
                        }
                    }
                }
                // y is less than or equal, overall height 
                if ((n->yp <= c->yp) && ((n->yp + n->hp) >= (c->yp + c->hp))) { 
                    // height exceeds, but we should go ahead and make sure list isnt NULL
                    list[(*num)] = n;
                    DEBUG("clientstouchingright: leaving false\n");
                    return false;
                }
            }
        }
    }
    return false;
}

bool clientstouchingtop(desktop *d, client *c, client **list, int *num) {
    if(c->yp > 0) { //capable of having windows above?
        int width;
        (*num) = 0;
        width = c->wp;
        for (client *n = d->head; n; n = n->next) {
            if ((c != n) && !ISFT(n) && (c->yp == (n->yp + n->hp))) {// directly above
                if ((n->xp + n->wp) <= (c->xp + c->wp)) { //width equivalent or less than
                    if ((n->xp == c->xp) && (n->wp == c->wp)) { //direct match?
                        list[(*num)] = n;
                        (*num)++;
                        DEBUG("clientstouchingtop: leaving found direct match\n");
                        return true;
                    }
                    else if (n->xp >= c->xp) { //part
                        width -= n->wp;
                        list[(*num)] = n;
                        (*num)++;
                        if (width == 0) {
                            DEBUG("clientstouchingtop: leaving true\n");
                            return true;
                        }
                        if (width < 0) {
                            DEBUG("clientstouchingtop: leaving false\n");
                            return false;
                        }
                    }
                }
                
                if ((n->xp <= c->xp) && ((n->xp + n->wp) >= (c->xp + c->wp))) { 
                    // width exceeds, but we should go ahead and make sure list isnt NULL
                    list[(*num)] = n;
                    DEBUG("clientstouchingtop: leaving false\n");
                    return false;
                }
            }
        }
    }
    return false;
}

/* see license for copyright and license */

#include "frankensteinwm.h"

/* each window should cover all the available screen space */
void monocle(int x, int y, int w, int h, const desktop *d, const monitor *m) {
    DEBUG("monocle: entering");
    int gap = d->gap; 
    for (client *c = d->head; c; c = c->next) 
        if (!ISFFT(c)){
            if (d->mode == VIDEO)
                xcb_move_resize(dis, c->win, x, (y - ((m->haspanel && TOP_PANEL) ? PANEL_HEIGHT:0)), w, (h + ((m->haspanel && !TOP_PANEL) ? PANEL_HEIGHT:0)));
            else
                xcb_move_resize(dis, c->win, (x + gap), (y + gap), (w - 2*gap), (h - 2*gap));
        }
    DEBUG("monocle: leaving");
}

void retile(desktop *d, const monitor *m) {
    DEBUG("retile: entering");
    int gap = d->gap;

    if (d->mode == TILE) {
        int n = d->count;
        DEBUGP("retile: d->count = %d\n", d->count);
       
        for (client *c = d->head; c; c=c->next) {
            if (!ISFFT(c)) {
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
            }
        } 
    }
    else
        monocle(m->x, m->y, m->w, m->h, d, m);
    setborders(d);

    DEBUG("retile: leaving");
}

void tilenew(desktop *d, const monitor *m) {
    DEBUG("tilenew: entering");
    client *c = d->current, *n, *dead = d->dead;
    int gap = d->gap;

    if (!d->head || d->mode == FLOAT) return; // nothing to arange 
    for (n = d->head; n && n->next; n = n->next);
    if (ISFFT(n))
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
            d->dead = d->dead->next;
            free(dead); dead = NULL;
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
        //TODO: we should go ahead and try to fill other dead clients
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

        if ((m != NULL) && (d->mode == TILE)) {
            adjustclientgaps(gap, c);
            xcb_move_resize(dis, c->win, 
                            (c->x = m->x + (m->w * c->xp) + c->gapx), 
                            (c->y = m->y + (m->h * c->yp) + c->gapy), 
                            (c->w = (m->w * c->wp) - 2*c->gapw), 
                            (c->h = (m->h * c->hp) - 2*c->gaph));
        }
        d->dead = d->dead->next;
        free(dead); dead = NULL;
        DEBUG("tileremove: leaving");
        return;
    }

    list = (client**)malloc_safe(n * sizeof(client*));

    if (findtouchingclients[TTOP](d, dead, list, &n)) {
        // clients in list should gain the emptyspace
        for (int i = 0; i < n; i++) {
            list[i]->hp += dead->hp;
            if (m != NULL && (d->mode == TILE)) {
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
            if (m != NULL && (d->mode == TILE)) {
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
            if (m != NULL && (d->mode == TILE)) {
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
            if (m != NULL && (d->mode == TILE)) {
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

/* vim: set ts=4 sw=4 :*/

#if PRETTY_PRINT
void updatedir() {
    desktop *d = &desktops[selmon->curr_dtop];
    char *tags_dir[] = PP_TAGS_DIR;
    char temp[512];
     
    if (tags_dir[d->direction]) {
        snprintf(temp, 512, "^fg(%s)%s ", PP_COL_DIR, tags_dir[d->direction]);
        pp.dir = realloc(pp.dir, strlen(temp));
        sprintf(pp.dir, "^fg(%s)%s ", PP_COL_DIR, tags_dir[d->direction]);
    } else {
        snprintf(temp, 512, "^fg(%s)%d ", PP_COL_DIR, d->direction);
        pp.dir = realloc(pp.dir, strlen(temp));
        sprintf(pp.dir, "^fg(%s)%d ", PP_COL_DIR, d->direction);
    }
}

void updatemode() {
    desktop *d = &desktops[selmon->curr_dtop];
    char *tags_mode[] = PP_TAGS_MODE;
    char temp[512];
    
    if (tags_mode[d->mode]) {
        snprintf(temp, 512, "^fg(%s)%s ", PP_COL_MODE, tags_mode[d->mode]);
        pp.mode = realloc(pp.mode, strlen(temp));
        sprintf(pp.mode, "^fg(%s)%s ", PP_COL_MODE, tags_mode[d->mode]);
    } else {
        snprintf(temp, 512, "^fg(%s)%d ", PP_COL_MODE, d->mode);
        pp.mode = realloc(pp.mode, strlen(temp));
        sprintf(pp.mode, "^fg(%s)%d ", PP_COL_MODE, d->mode);
    }
}

void updatetitle(client *c) {
    xcb_icccm_get_text_property_reply_t reply;
    xcb_generic_error_t *err = NULL;

    if(!xcb_icccm_get_text_property_reply(dis, xcb_icccm_get_text_property(dis, c->win, netatoms[NET_WM_NAME]), &reply, &err))
        if(!xcb_icccm_get_text_property_reply(dis, xcb_icccm_get_text_property(dis, c->win, XCB_ATOM_WM_NAME), &reply, &err))
            return;

    if(err) {
        DEBUG("updatetitle: leaving, error\n");
        free(err);
        return;
    }

    // TODO: encoding
    if(!reply.name || !reply.name_len)
        return;
     
    free(c->title);
    c->title = malloc_safe(reply.name_len+1);  
    strncpy(c->title, reply.name, reply.name_len); 
    xcb_icccm_get_text_property_reply_wipe(&reply);
}

void updatews() {
    desktop *d = NULL; client *c = NULL; monitor *m = NULL;
    bool urgent = false; 
    char *tags_ws[] = PP_TAGS_WS;
    char t1[512] = { "" };
    char t2[512] = { "" };
    #if DEBUG
    int count = 0;
    #endif

    for (int w = 0, i = 0; i < DESKTOPS; i++, w = 0, urgent = false) {
        for (d = &desktops[i], c = d->head; c; urgent |= c->isurgent, ++w, c = c->next); 
        for (m = mons; m; m = m->next)
            if (i == m->curr_dtop && w == 0)
                w++;
        
        if (tags_ws[i])
            snprintf(t2, 512, "^fg(%s)%s ", 
                    d == &desktops[selmon->curr_dtop] ? PP_COL_CURRENT:urgent ? PP_COL_URGENT:w ? PP_COL_VISIBLE:PP_COL_HIDDEN, 
                    tags_ws[i]);
        else 
            snprintf(t2, 512, "^fg(%s)%d ", 
                    d == &desktops[selmon->curr_dtop] ? PP_COL_CURRENT:urgent ? PP_COL_URGENT:w ? PP_COL_VISIBLE:PP_COL_HIDDEN, 
                    i + 1);
        strncat(t1, t2, strlen(t2));
        #if DEBUG
        count += strlen(t2);
        DEBUGP("updatews: count = %d\n", count);
        #endif
    }
    pp.ws = (char *)realloc(pp.ws, strlen(t1) + 1);
    strncpy(pp.ws, t1, strlen(t1));
}
#endif

#if PRETTY_PRINT
// output info about the desktops on standard output stream
// once the info is printed, immediately flush the stream
void desktopinfo(void) {
    desktop *d = &desktops[selmon->curr_dtop];
    PP_PRINTF;
    fflush(stdout);
}
#endif

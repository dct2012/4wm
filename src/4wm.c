// see license for copyright and license 

#include "4wm.h"

bool getrootptr(int *x, int *y) {
    xcb_query_pointer_reply_t *reply = xcb_query_pointer_reply(dis, xcb_query_pointer(dis, screen->root), NULL);

    *x = reply->root_x;
    *y = reply->root_y;

    free(reply);

    return true;
}

// the wm should listen to key presses
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

// wrapper to get xcb keycodes from keysymbol
xcb_keycode_t* xcb_get_keycodes(xcb_keysym_t keysym) {
    xcb_key_symbols_t *keysyms;
    xcb_keycode_t     *keycode;

    if (!(keysyms = xcb_key_symbols_alloc(dis))) return NULL;
        keycode = xcb_key_symbols_get_keycode(keysyms, keysym);
    xcb_key_symbols_free(keysyms);

    return keycode;
}

#define CLEANMASK(mask) (mask & ~(numlockmask | XCB_MOD_MASK_LOCK))

// wrapper to get xcb keysymbol from keycode
static xcb_keysym_t xcb_get_keysym(xcb_keycode_t keycode) {
    xcb_key_symbols_t *keysyms;
    xcb_keysym_t       keysym;

    if (!(keysyms = xcb_key_symbols_alloc(dis))) return 0;
    keysym = xcb_key_symbols_get_keysym(keysyms, keycode, 0);
    xcb_key_symbols_free(keysyms);

    return keysym;
}

// wrapper to window get attributes using xcb */
static void xcb_get_attributes(xcb_window_t *windows, xcb_get_window_attributes_reply_t **reply, unsigned int count) {
    xcb_get_window_attributes_cookie_t cookies[count];
    for (unsigned int i = 0; i < count; i++) cookies[i] = xcb_get_window_attributes(dis, windows[i]);
    for (unsigned int i = 0; i < count; i++) reply[i]   = xcb_get_window_attributes_reply(dis, cookies[i], NULL); // TODO: Handle error

static char *WM_ATOM_NAME[]   = { "WM_PROTOCOLS", "WM_DELETE_WINDOW" };
static char *NET_ATOM_NAME[]  = { "_NET_SUPPORTED", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE", "_NET_WM_NAME", "_NET_ACTIVE_WINDOW" };

#define USAGE           "usage: 4wm [-h] [-v]"

// get screen of display
static xcb_screen_t *xcb_screen_of_display(xcb_connection_t *con, int screen) {
    xcb_screen_iterator_t iter;
    iter = xcb_setup_roots_iterator(xcb_get_setup(con));
    for (; iter.rem; --screen, xcb_screen_next(&iter)) if (screen == 0) return iter.data;
    return NULL;
}

// retieve RGB color from hex (think of html)
static unsigned int xcb_get_colorpixel(char *hex) {
    char strgroups[3][3]  = {{hex[1], hex[2], '\0'}, {hex[3], hex[4], '\0'}, {hex[5], hex[6], '\0'}};
    unsigned int rgb16[3] = {(strtol(strgroups[0], NULL, 16)), (strtol(strgroups[1], NULL, 16)), (strtol(strgroups[2], NULL, 16))};
    return (rgb16[0] << 16) + (rgb16[1] << 8) + rgb16[2];
}

// wrapper to get atoms using xcb
static void xcb_get_atoms(char **names, xcb_atom_t *atoms, unsigned int count) {
    xcb_intern_atom_cookie_t cookies[count];
    xcb_intern_atom_reply_t  *reply;

    for (unsigned int i = 0; i < count; i++) cookies[i] = xcb_intern_atom(dis, 0, strlen(names[i]), names[i]);
    for (unsigned int i = 0; i < count; i++) {
        reply = xcb_intern_atom_reply(dis, cookies[i], NULL); // TODO: Handle error
        if (reply) {
            DEBUGP("%s : %d\n", names[i], reply->atom);
            atoms[i] = reply->atom; free(reply);
        } else puts("WARN: 4wm failed to register %s atom.\nThings might not work right.");
    }
}

// check if other wm exists
static int xcb_checkotherwm(void) {
    xcb_generic_error_t *error;
    unsigned int values[1] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT|XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY|
                              XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_BUTTON_PRESS};
    error = xcb_request_check(dis, xcb_change_window_attributes_checked(dis, screen->root, XCB_CW_EVENT_MASK, values));
    xcb_flush(dis);
    if (error) return 1;
    return 0;
}


// remove all windows in all desktops by sending a delete message
static void cleanup(void) {
    xcb_query_tree_reply_t  *query;
    xcb_window_t *c;

    xcb_ungrab_key(dis, XCB_GRAB_ANY, screen->root, XCB_MOD_MASK_ANY);
    if ((query = xcb_query_tree_reply(dis,xcb_query_tree(dis,screen->root),0))) {
        c = xcb_query_tree_children(query);
        for (unsigned int i = 0; i != query->children_len; ++i) deletewindow(c[i]);
        free(query);
    }
    xcb_set_input_focus(dis, XCB_INPUT_FOCUS_POINTER_ROOT, screen->root, XCB_CURRENT_TIME);
    
    xcb_ewmh_connection_wipe(ewmh);
    if(ewmh)
        free(ewmh);

    // free each monitor
    monitor *m, *t;
    for (m = mons; m; m = t){
        t = m->next;
        free(m);
    }
    #if MENU
    // free each menu and each menuentry
    Menu *men, *tmen;
    Menu_Entry *ent, *tent;
    for (men = menus; men; men = tmen) {
        tmen = men->next;
        for (ent = men->head; ent; ent = tent) {
            tent = ent->next;
            free(ent);
        }
        free(men);
    }
    #endif
    #if PRETTY_PRINT
    free(pp.ws);
    free(pp.mode);
    free(pp.dir);
    //fclose(pp_in);
    //fclose(pp_out);
    #endif 
}

// get a pixel with the requested color
// to fill some window area - borders
static unsigned int getcolor(char* color) {
    xcb_colormap_t map = screen->default_colormap;
    xcb_alloc_color_reply_t *c;
    unsigned int r, g, b, rgb, pixel;

    rgb = xcb_get_colorpixel(color);
    r = rgb >> 16; g = rgb >> 8 & 0xFF; b = rgb & 0xFF;
    c = xcb_alloc_color_reply(dis, xcb_alloc_color(dis, map, r * 257, g * 257, b * 257), NULL);
    if (!c)
        errx(EXIT_FAILURE, "error: cannot allocate color '%s'\n", color);

    pixel = c->pixel; free(c);
    return pixel;
}

static void getoutputs(xcb_randr_output_t *outputs, const int len, xcb_timestamp_t timestamp) {
    // Walk through all the RANDR outputs (number of outputs == len) there
    // was at time timestamp.
    xcb_randr_get_crtc_info_cookie_t icookie;
    xcb_randr_get_crtc_info_reply_t *crtc = NULL;
    xcb_randr_get_output_info_reply_t *output;
    xcb_randr_get_output_info_cookie_t ocookie[len];
    monitor *m;
    int i, n;
    bool flag;
 
    // get output cookies
    for (i = 0; i < len; i++) 
        ocookie[i] = xcb_randr_get_output_info(dis, outputs[i], timestamp);

    for (i = 0; i < len; i ++) { /* Loop through all outputs. */
        output = xcb_randr_get_output_info_reply(dis, ocookie[i], NULL);

        if (output == NULL) 
            continue;
        //asprintf(&name, "%.*s",xcb_randr_get_output_info_name_length(output),xcb_randr_get_output_info_name(output));

        if (XCB_NONE != output->crtc) {
            icookie = xcb_randr_get_crtc_info(dis, output->crtc, timestamp);
            crtc    = xcb_randr_get_crtc_info_reply(dis, icookie, NULL);

            if (NULL == crtc) 
                return; 

            flag = true;

            //check for uniqeness or update old
            for (n = 0, m = mons; m; m = m->next, n++) {
                if (outputs[i] == m->id) {
                    flag = false;
                    //if they are the same check to see if the dimensions have
                    //changed. and retile
                    DEBUGP("%d %d %d %d %d %d %d %d\n", crtc->x, m->x,(crtc->y + ((SHOW_PANEL && TOP_PANEL) ? PANEL_HEIGHT:0)), 
                            m->y, crtc->width, m->w, (crtc->height - (SHOW_PANEL ? PANEL_HEIGHT:0)), m->h);
                    if (crtc->x != m->x||(crtc->y + ((SHOW_PANEL && TOP_PANEL) ? PANEL_HEIGHT:0)) != m->y||
                        crtc->width != m->w|| (crtc->height - (SHOW_PANEL ? PANEL_HEIGHT:0)) != m->h) {
                        DEBUG("getoutputs: adjusting monitor\n");
                        m->x = crtc->x;
                        m->y = crtc->y;
                        m->w = crtc->width;
                        m->h = crtc->height;
                        retile(&desktops[m->curr_dtop], m);
                    }
                    break;
                }
            }
            // if unique, add it to the list, give it a desktop/workspace
            if (flag){
                DEBUG("getoutputs: adding monitor\n");
                for(m = mons; m && m->next; m = m->next);
                if(m) {
                    DEBUG("getoutputs: entering m->next = createmon\n");
                    m->next = createmon(outputs[i], crtc->x, crtc->y, crtc->width, crtc->height, ++nmons);
                }
                else {
                    DEBUG("getoutputs: entering mon = createmon\n");
                    mons = createmon(outputs[i], crtc->x, crtc->y, crtc->width, crtc->height, ++nmons);
                }
            }
        }
        else {
            //find monitor and delete
            for (m = mons; m; m = m->next) {
                if (m->id == outputs[i]) { //monitor found
                    if (m == mons)
                        mons = mons->next;
                    if (m == selmon)
                        selmon = mons;
                    DEBUG("getoutputs: deleting monitor\n");
                    free(m);
                    nmons--;
                    break;
                } 
            }
        }
        free(output);
    }
}

static void getrandr(void) { // Get RANDR resources and figure out how many outputs there are.
    xcb_randr_get_screen_resources_current_cookie_t rcookie = xcb_randr_get_screen_resources_current(dis, screen->root);
    xcb_randr_get_screen_resources_current_reply_t *res = xcb_randr_get_screen_resources_current_reply(dis, rcookie, NULL);
    if (NULL == res) return;
    xcb_timestamp_t timestamp = res->config_timestamp;
    int len     = xcb_randr_get_screen_resources_current_outputs_length(res);
    xcb_randr_output_t *outputs = xcb_randr_get_screen_resources_current_outputs(res);
    /* Request information for all outputs. */
    getoutputs(outputs, len, timestamp);
    free(res);
}

#if MENU
static void initializexresources() {
    //we should also go ahead and intitialize all the font gc's
    uint32_t            value_list[3];
    uint32_t            gcvalues[2];
    xcb_void_cookie_t   cookie_font;
    xcb_void_cookie_t   cookie_gc;
    xcb_generic_error_t *error;
    xcb_font_t          font;
    uint32_t            mask, font_mask;
    xcb_drawable_t      win = screen->root;
    XrmValue value;
    char *str_type[20];
    char buffer[20];
    char *names[] = { "*color1", "*color2",  "*color3", "*color4", "*color5", "*color6", 
                    "*color9", "*color10", "*color11", "*color12", "*color13", "*color14", NULL };
    char *class[] = { "*Color1", "*Color2",  "*Color3", "*Color4", "*Color5", "*Color6", 
                    "*Color9", "*Color10", "*Color11", "*Color12", "*Color13", "*Color14", NULL };
    struct passwd *pw = getpwuid(getuid());
    char *xdefaults = pw->pw_dir;
    strcat(xdefaults, "/.Xdefaults");

    // initialize font
    // TODO: user font
    font = xcb_generate_id (dis);
    cookie_font = xcb_open_font_checked (dis, font, strlen ("7x13"), "7x13");
    error = xcb_request_check (dis, cookie_font);
    if (error) {
        fprintf (stderr, "ERROR: can't open font : %d\n", error->error_code);
        xcb_disconnect (dis);
    }

    // initialize some values used to get the font gc's
    font_mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT;
    value_list[0] = screen->black_pixel;
    value_list[2] = font;
    // initialize some values for foreground gc's
    mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
    gcvalues[1] = 0;

    // initialize Xresources
    XrmInitialize();
    XrmDatabase dbase = XrmGetFileDatabase(xdefaults);

    for(int i = 0; i < 12; i++) {
        // now get all the colors from xresources and make the gc's
        if (XrmGetResource(dbase, names[i], class[i], str_type, &value)) {
            // getting color
            strncpy(buffer, value.addr, (int) value.size);
            xres.color[i] = getcolor(buffer);
            
            // getting rectangle foreground colors
            xres.gc_color[i] = xcb_generate_id(dis);
            gcvalues[0] = xres.color[i];
            xcb_create_gc (dis, xres.gc_color[i], win, mask, gcvalues);
            
            // getting font gc
            xres.font_gc[i] = xcb_generate_id(dis);
            value_list[1] = xres.color[i];
            cookie_gc = xcb_create_gc_checked (dis, xres.font_gc[i], win, font_mask, value_list);
            error = xcb_request_check (dis, cookie_gc);
            if (error) {
                fprintf (stderr, "ERROR: can't create gc : %d\n", error->error_code);
                xcb_disconnect (dis);
                exit (-1);
            } 
        }
    } 

    cookie_font = xcb_close_font_checked (dis, font);
    error = xcb_request_check (dis, cookie_font);
    if (error) {
        fprintf (stderr, "ERROR: can't close font : %d\n", error->error_code);
        xcb_disconnect (dis);
        exit (-1);
    }
}
#endif

// main event loop - on receival of an event call the appropriate event handler
static void run(void) {
    xcb_generic_event_t *ev; 
    while(running) {
        DEBUG("run: running\n");
        xcb_flush(dis);
        if (xcb_connection_has_error(dis)) {
            DEBUG("run: x11 connection got interrupted\n");
            err(EXIT_FAILURE, "error: X11 connection got interrupted\n");
        }
        if ((ev = xcb_wait_for_event(dis))) {
            if (ev->response_type==randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
                DEBUG("run: entering getrandr()\n");
                getrandr();
            }
            if (events[ev->response_type & ~0x80]) {
                DEBUGP("run: entering event %d\n", ev->response_type & ~0x80);
                events[ev->response_type & ~0x80](ev);
            }
            else {DEBUGP("xcb: unimplented event: %d\n", ev->response_type & ~0x80);}
            free(ev);
        }
    }
}

// get numlock modifier using xcb
static int setup_keyboard(void) {
    xcb_get_modifier_mapping_reply_t *reply;
    xcb_keycode_t                    *modmap;
    xcb_keycode_t                    *numlock;

    reply   = xcb_get_modifier_mapping_reply(dis, xcb_get_modifier_mapping_unchecked(dis), NULL); /* TODO: error checking */
    if (!reply) return -1;

    modmap = xcb_get_modifier_mapping_keycodes(reply);
    if (!modmap) return -1;

    numlock = xcb_get_keycodes(XK_Num_Lock);
    for (unsigned int i=0; i<8; i++)
       for (unsigned int j=0; j<reply->keycodes_per_modifier; j++) {
           xcb_keycode_t keycode = modmap[i * reply->keycodes_per_modifier + j];
           if (keycode == XCB_NO_SYMBOL) continue;
           for (unsigned int n=0; numlock[n] != XCB_NO_SYMBOL; n++)
               if (numlock[n] == keycode) {
                   DEBUGP("xcb: found num-lock %d\n", 1 << i);
                   numlockmask = 1 << i;
                   break;
               }
       }

    return 0;
}

static void sigchld() {
    if (signal(SIGCHLD, sigchld) == SIG_ERR)
        err(EXIT_FAILURE, "cannot install SIGCHLD handler");
    while(0 < waitpid(-1, NULL, WNOHANG));
}

static int setuprandr(void) { // Set up RANDR extension. Get the extension base and subscribe to
    // events.
    const xcb_query_extension_reply_t *extension = xcb_get_extension_data(dis, &xcb_randr_id);
    if (!extension->present) return -1;
    else getrandr();
    int base = extension->first_event;
    xcb_randr_select_input(dis, screen->root,XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
            XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE |XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
            XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);
    return base;
}

// set initial values
// root window - screen height/width - atoms - xerror handler
// set masks for reporting events handled by the wm
// and propagate the suported net atoms
static int setup(int default_screen) {
    sigchld();
    screen = xcb_screen_of_display(dis, default_screen);
    if (!screen) err(EXIT_FAILURE, "error: cannot aquire screen\n");
    
    randrbase = setuprandr();
    //DEBUG("exited setuprandr, continuing setup\n");

    selmon = mons; 
    for (unsigned int i=0; i<DESKTOPS; i++)
        desktops[i] = (desktop){ .mode = DEFAULT_MODE, .direction = DEFAULT_DIRECTION, .showpanel = SHOW_PANEL, .gap = GAP, .count = 0, };

    win_focus   = getcolor(FOCUS);
    win_unfocus = getcolor(UNFOCUS);
    win_outer   = getcolor(OTRBRDRCOL);
    win_urgent  = getcolor(URGNBRDRCOL);
    win_flt     = getcolor(FLTBRDCOL);

    #if MENU
    // initialize the menu 
    Menu *m = NULL;
    Menu *itr = NULL;
    char **menulist[] = MENUS;
    for (int i = 0; menulist[i]; i++) {
        m = createmenu(menulist[i]);
        if (!menus)
            menus = m;
        else {
            for (itr = menus; itr->next; itr = itr->next); 
            itr->next = m;
        }
    }

    initializexresources();
    #endif

    /* setup keyboard */
    if (setup_keyboard() == -1)
        err(EXIT_FAILURE, "error: failed to setup keyboard\n");

    /* set up atoms for dialog/notification windows */
    xcb_get_atoms(WM_ATOM_NAME, wmatoms, WM_COUNT);
    xcb_get_atoms(NET_ATOM_NAME, netatoms, NET_COUNT);

    /* check if another wm is running */
    if (xcb_checkotherwm())
        err(EXIT_FAILURE, "error: other wm is running\n");

    /* initialize EWMH */
    ewmh = malloc_safe(sizeof(xcb_ewmh_connection_t));
    if (!ewmh)
        err(EXIT_FAILURE, "error: failed to set ewmh atoms\n");
    xcb_ewmh_init_atoms_replies(ewmh, xcb_ewmh_init_atoms(dis, ewmh), (void *)0);

    xcb_change_property(dis, XCB_PROP_MODE_REPLACE, screen->root, netatoms[NET_SUPPORTED], XCB_ATOM_ATOM, 32, NET_COUNT, netatoms);
    grabkeys();    

    //DEBUG("setup: about to switch to default desktop\n");
    if (DEFAULT_DESKTOP >= 0 && DEFAULT_DESKTOP < DESKTOPS)
        change_desktop(&(Arg){.i = DEFAULT_DESKTOP});
    
    // new pipe to messenger, panel, dzen
    #if PRETTY_PRINT
    updatews();
    updatemode();
    updatedir();
    
    int pfds[2];
    pid_t pid;

    if (pipe(pfds) < 0) {
        perror("pipe failed");
        return EXIT_FAILURE;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return EXIT_FAILURE;
    } else if (pid == 0) { // child
        close(pfds[0]); // close unused read end
        // set write end of pipe as stdout for this child process
        dup2(pfds[1], STDOUT_FILENO);
        //pp_out = fdopen(pfds[1], "w");
        close(pfds[1]);

        desktopinfo();
    } else /* if (pid > 0) */ { // parent
        char *args[] = PP_CMD;
        close(pfds[1]); // close unused write end
        // set read end of pipe as stdin for this process
        dup2(pfds[0], STDIN_FILENO);
        //pp_in = fdopen(pfds[0], "r");
        close(pfds[0]); // already redirected to stdin

        execvp(args[0], args);
        perror("exec failed");
        exit(EXIT_FAILURE);
    }
    #endif

    return 0;
}

int main(int argc, char *argv[]) {
    int default_screen;

    // variables
    static bool running = true;
    static int randrbase, retval = 0, nmons = 0;
    static unsigned int numlockmask = 0, win_unfocus, win_focus, win_outer, win_urgent, win_flt;
    static xcb_connection_t *dis;
    static xcb_screen_t *screen;
    static xcb_atom_t wmatoms[WM_COUNT], netatoms[NET_COUNT];
    static desktop desktops[DESKTOPS];
    static monitor *mons = NULL, *selmon = NULL;
    static xcb_ewmh_connection_t *ewmh;
    #if MENU
    static Menu *menus = NULL;
    static Xresources xres;
    #endif
    #if PRETTY_PRINT
    pp_data pp;
    #endif

    if (argc == 2 && argv[1][0] == '-') {
        switch (argv[1][1]) {
            case 'v': 
                errx(EXIT_SUCCESS, "%s - by Derek Taaffe", VERSION);
            case 'h': 
                errx(EXIT_SUCCESS, "%s", USAGE);
            default: 
                errx(EXIT_FAILURE, "%s", USAGE);
        }
    } else if (argc != 1) 
        errx(EXIT_FAILURE, "%s", USAGE);
    
    if (xcb_connection_has_error((dis = xcb_connect(NULL, &default_screen))))
        errx(EXIT_FAILURE, "error: cannot open display\n");
    
    if (setup(default_screen) != -1) {
      #if PRETTY_PRINT
      desktopinfo(); // zero out every desktop on (re)start
      #endif
      run();
    }
    
    cleanup();
    xcb_disconnect(dis);
    
    return retval;
}

/* vim: set ts=4 sw=4 :*/

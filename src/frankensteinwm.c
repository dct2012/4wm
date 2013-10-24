/* see license for copyright and license */

#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <stdarg.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <pwd.h>
#include <X11/keysym.h>
#include <X11/Xresource.h>
#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>

static char *WM_ATOM_NAME[]   = { "WM_PROTOCOLS", "WM_DELETE_WINDOW" };
static char *NET_ATOM_NAME[]  = { "_NET_SUPPORTED", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE", "_NET_WM_NAME", "_NET_ACTIVE_WINDOW" };

#define USAGE           "usage: frankensteinwm [-h] [-v]"

/* get screen of display */
static xcb_screen_t *xcb_screen_of_display(xcb_connection_t *con, int screen) {
    xcb_screen_iterator_t iter;
    iter = xcb_setup_roots_iterator(xcb_get_setup(con));
    for (; iter.rem; --screen, xcb_screen_next(&iter)) if (screen == 0) return iter.data;
    return NULL;
}

/* retieve RGB color from hex (think of html) */
static unsigned int xcb_get_colorpixel(char *hex) {
    char strgroups[3][3]  = {{hex[1], hex[2], '\0'}, {hex[3], hex[4], '\0'}, {hex[5], hex[6], '\0'}};
    unsigned int rgb16[3] = {(strtol(strgroups[0], NULL, 16)), (strtol(strgroups[1], NULL, 16)), (strtol(strgroups[2], NULL, 16))};
    return (rgb16[0] << 16) + (rgb16[1] << 8) + rgb16[2];
}

/* wrapper to get atoms using xcb */
static void xcb_get_atoms(char **names, xcb_atom_t *atoms, unsigned int count) {
    xcb_intern_atom_cookie_t cookies[count];
    xcb_intern_atom_reply_t  *reply;

    for (unsigned int i = 0; i < count; i++) cookies[i] = xcb_intern_atom(dis, 0, strlen(names[i]), names[i]);
    for (unsigned int i = 0; i < count; i++) {
        reply = xcb_intern_atom_reply(dis, cookies[i], NULL); /* TODO: Handle error */
        if (reply) {
            DEBUGP("%s : %d\n", names[i], reply->atom);
            atoms[i] = reply->atom; free(reply);
        } else puts("WARN: frankensteinwm failed to register %s atom.\nThings might not work right.");
    }
}

/* check if other wm exists */
static int xcb_checkotherwm(void) {
    xcb_generic_error_t *error;
    unsigned int values[1] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT|XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY|
                              XCB_EVENT_MASK_PROPERTY_CHANGE|XCB_EVENT_MASK_BUTTON_PRESS};
    error = xcb_request_check(dis, xcb_change_window_attributes_checked(dis, screen->root, XCB_CW_EVENT_MASK, values));
    xcb_flush(dis);
    if (error) return 1;
    return 0;
}

/* remove all windows in all desktops by sending a delete message */
void cleanup(void) {
    DEBUG("cleanup: entering");
    xcb_query_tree_reply_t  *query;
    xcb_window_t *c;

    xcb_ungrab_key(dis, XCB_GRAB_ANY, screen->root, XCB_MOD_MASK_ANY);
    if ((query = xcb_query_tree_reply(dis,xcb_query_tree(dis,screen->root),0))) {
        c = xcb_query_tree_children(query);
        for (unsigned int i = 0; i != query->children_len; ++i) deletewindow(c[i]);
        free(query);
    }
    
    free(mons);
    free(menus);
    xcb_set_input_focus(dis, XCB_INPUT_FOCUS_POINTER_ROOT, screen->root, XCB_CURRENT_TIME);
    xcb_flush(dis);
    DEBUG("cleanup: leaving");
}

Menu_Entry* createmenuentry(int x, int y, int w, int h, char *cmd) {
    DEBUG("createmenuentry: entering");
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
    DEBUG("createmenuentry: leaving");
    return m;
}

Menu* createmenu(char **list) {
    DEBUG("createmenu: entering");
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
    DEBUG("createmenu: leaving");
    return m;
}

monitor* createmon(xcb_randr_output_t id, int x, int y, int w, int h, int dtop) {
    DEBUG("createmon: entering");
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
    DEBUG("createmon: leaving");
    return m;
}

/* get a pixel with the requested color
 * to fill some window area - borders */
unsigned int getcolor(char* color) {
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

void getoutputs(xcb_randr_output_t *outputs, const int len, xcb_timestamp_t timestamp)
{
    DEBUG("getoutputs: entering");
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
                        DEBUG("getoutputs: adjusting monitor");
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
                DEBUG("getoutputs: adding monitor");
                for(m = mons; m && m->next; m = m->next);
                if(m) {
                    DEBUG("getoutputs: entering m->next = createmon");
                    m->next = createmon(outputs[i], crtc->x, crtc->y, crtc->width, crtc->height, ++nmons);
                }
                else {
                    DEBUG("getoutputs: entering mon = createmon");
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
                    DEBUG("getoutputs: deleting monitor");
                    free(m);
                    nmons--;
                    break;
                } 
            }
        }
        free(output);
    }
    DEBUG("getoutputs: leaving");
}

void getrandr(void)                 // Get RANDR resources and figure out how many outputs there are.
{
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

void initializexresources() {
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
    // TODO: get font, XrmGetResouce
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

/* main event loop - on receival of an event call the appropriate event handler */
void run(void) {
    DEBUG("run: entered");
    xcb_generic_event_t *ev; 
    while(running) {
        DEBUG("run: running");
        xcb_flush(dis);
        if (xcb_connection_has_error(dis)) {
            DEBUG("run: x11 connection got interrupted");
            err(EXIT_FAILURE, "error: X11 connection got interrupted\n");
        }
        if ((ev = xcb_wait_for_event(dis))) {
            if (ev->response_type==randrbase + XCB_RANDR_SCREEN_CHANGE_NOTIFY) {
                DEBUG("run: entering getrandr()");
                getrandr();
            }
            if (events[ev->response_type & ~0x80]) {
                DEBUGP("run: entering event %d\n", ev->response_type & ~0x80);
                events[ev->response_type & ~0x80](ev);
            }
            else { DEBUGP("xcb: unimplented event: %d\n", ev->response_type & ~0x80); }
            free(ev);
        }
    }
    DEBUG("run: leaving");
}

/* get numlock modifier using xcb */
int setup_keyboard(void)
{
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

/* set initial values
 * root window - screen height/width - atoms - xerror handler
 * set masks for reporting events handled by the wm
 * and propagate the suported net atoms
 */
int setup(int default_screen) {
    sigchld();
    screen = xcb_screen_of_display(dis, default_screen);
    if (!screen) err(EXIT_FAILURE, "error: cannot aquire screen\n");
    
    randrbase = setuprandr();
    //DEBUG("exited setuprandr, continuing setup");

    selmon = mons; 
    for (unsigned int i=0; i<DESKTOPS; i++)
        desktops[i] = (desktop){ .mode = DEFAULT_MODE, .direction = DEFAULT_DIRECTION, .showpanel = SHOW_PANEL, .gap = GAP, .count = 0, };

    win_focus   = getcolor(FOCUS);
    win_unfocus = getcolor(UNFOCUS);
    win_outer = getcolor(OTRBRDRCOL);
    win_urgent = getcolor(URGNBRDRCOL);

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

    /* setup keyboard */
    if (setup_keyboard() == -1)
        err(EXIT_FAILURE, "error: failed to setup keyboard\n");

    /* set up atoms for dialog/notification windows */
    xcb_get_atoms(WM_ATOM_NAME, wmatoms, WM_COUNT);
    xcb_get_atoms(NET_ATOM_NAME, netatoms, NET_COUNT);

    /* check if another wm is running */
    if (xcb_checkotherwm())
        err(EXIT_FAILURE, "error: other wm is running\n");

    xcb_change_property(dis, XCB_PROP_MODE_REPLACE, screen->root, netatoms[NET_SUPPORTED], XCB_ATOM_ATOM, 32, NET_COUNT, netatoms);
    xcb_flush(dis);
    grabkeys();

    /* set events */
    for (unsigned int i=0; i<XCB_NO_OPERATION; i++) events[i] = NULL;
    events[XCB_BUTTON_PRESS]                = buttonpress;
    events[XCB_CLIENT_MESSAGE]              = clientmessage;
    events[XCB_CONFIGURE_REQUEST]           = configurerequest;
    //events[XCB_CONFIGURE_NOTIFY]            = configurenotify;
    events[XCB_DESTROY_NOTIFY]              = destroynotify;
    events[XCB_ENTER_NOTIFY]                = enternotify;
    events[XCB_EXPOSE]                      = expose;
    events[XCB_FOCUS_IN]                    = focusin;
    events[XCB_KEY_PRESS|XCB_KEY_RELEASE]   = keypress;
    events[XCB_MAPPING_NOTIFY]              = mappingnotify;
    events[XCB_MAP_REQUEST]                 = maprequest;
    events[XCB_PROPERTY_NOTIFY]             = propertynotify;
    events[XCB_UNMAP_NOTIFY]                = unmapnotify;
    events[XCB_NONE]                        = NULL;

    //DEBUG("setup: about to switch to default desktop");
    if (DEFAULT_DESKTOP >= 0 && DEFAULT_DESKTOP < DESKTOPS)
        change_desktop(&(Arg){.i = DEFAULT_DESKTOP});
    DEBUG("leaving setup");
    return 0;
}

int setuprandr(void)                // Set up RANDR extension. Get the extension base and subscribe to
{                                   // events.
    const xcb_query_extension_reply_t *extension = xcb_get_extension_data(dis, &xcb_randr_id);
    if (!extension->present) return -1;
    else getrandr();
    int base = extension->first_event;
    xcb_randr_select_input(dis, screen->root,XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
            XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE |XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
            XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);
    return base;
}

void sigchld() {
    if (signal(SIGCHLD, sigchld) == SIG_ERR)
        err(EXIT_FAILURE, "cannot install SIGCHLD handler");
    while(0 < waitpid(-1, NULL, WNOHANG));
}

int main(int argc, char *argv[]) {
    int default_screen;
    if (argc == 2 && argv[1][0] == '-') switch (argv[1][1]) {
        case 'v': errx(EXIT_SUCCESS, "%s - by Derek Taaffe", VERSION);
        case 'h': errx(EXIT_SUCCESS, "%s", USAGE);
        default: errx(EXIT_FAILURE, "%s", USAGE);
    } else if (argc != 1) errx(EXIT_FAILURE, "%s", USAGE);
    if (xcb_connection_has_error((dis = xcb_connect(NULL, &default_screen))))
        errx(EXIT_FAILURE, "error: cannot open display\n");
    if (setup(default_screen) != -1) {
      desktopinfo(); /* zero out every desktop on (re)start */
      run();
    }
    cleanup();
    xcb_disconnect(dis);
    return retval;
}

/* vim: set ts=4 sw=4 :*/

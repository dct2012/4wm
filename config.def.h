/* see LICENSE for copyright and license */

#ifndef CONFIG_H
#define CONFIG_H

/* -----------
 * modifiers 
 * ---------*/

#define MOD1            Mod1Mask    // ALT key
#define MOD4            Mod4Mask    // Super/Windows key
#define CONTROL         ControlMask // Control key
#define SHIFT           ShiftMask   // Shift key

/* -----------------
 * generic settings 
 * ----------------*/

// show panel by default on exec
#define SHOW_PANEL              true
// false means panel is on bottom
#define TOP_PANEL               true
// 0 for no space for panel, thus no panel
#define PANEL_HEIGHT            16
// Panel monitor number, 1 = first, 2 = second, etc
#define PANEL_MON               1
// initial layout/mode: TILE MONOCLE VIDEO
#define DEFAULT_MODE            TILE
// initial tiling direction: TBOTTOM, TLEFT, TRIGHT, TTOP
#define DEFAULT_DIRECTION       TRIGHT
// focus an unfocused window when clicked
#define CLICK_TO_FOCUS          true
// mouse button to be used along with CLICK_TO_FOCUS
#define FOCUS_BUTTON            Button1
// outer window border width
#define OUTER_BORDER            4
// inner window border width
#define INNER_BORDER            2
// window border width
#define BORDER_WIDTH            (OUTER_BORDER + INNER_BORDER)
// focused window border color
#define FOCUS           "#005FFF"
// unfocused window border color
#define UNFOCUS         "#262626"
// outer border color
#define OTRBRDRCOL      "#626262"
// floating outer border color
#define FLTBRDCOL       "#00FF5F"
// transeint outer border color
#define TRNBDRCOL       "#7F5AA1"
// minimum window size in pixels
#define MINWSZ          50          
// the desktop to focus initially
#define DEFAULT_DESKTOP 0
// number of desktops - edit DESKTOPCHANGE keys to suit
#define DESKTOPS        9
// the default size of the gap between windows in pixels
#define GAP             8

// pretty print, 1 = on, 0 = off
#define PRETTY_PRINT 0
#if PRETTY_PRINT
#define PP_CMD { "dzen2", "-x", "0", "-y", "784", "-h", "16", "-w", "640", NULL }
#define PP_COL_CURRENT  "#005FFF"
#define PP_COL_VISIBLE  "#FFFFFF"
#define PP_COL_HIDDEN   "#262626"
#define PP_COL_DIR      "#00FF5F"
#define PP_COL_MODE     "#AF00FF"
#define PP_COL_TITLE    "#FFFFFF"
#define PP_TAGS_WS { "one", "two", "three", "four", "five", "six", "seven", "eight", "nine" }
#define PP_TAGS_MODE { "^i(/path/to/bitmap)", "^i(/path/to/bitmap)", "^i(/path/to/bitmap)", "^i(/path/to/bitmap)" }
#define PP_TAGS_DIR { "^i(/path/to/bitmap)", "^i(/path/to/bitmap)", "^i(/path/to/bitmap)", "^i(/path/to/bitmap)" } 
#define PP_PRINTF printf("%s %s %s ^fg(%s)%s\n", pp.ws, pp.mode, pp.dir, PP_COL_TITLE, d->current ? d->current->title :"");
#endif

// helper for spawning shell commands
#define SHCMD(cmd) {.com = (const char*[]){"/bin/sh", "-c", cmd, NULL}}

// custom commands, must always end with ', NULL };'
static const char *termcmd[] = { "xterm",     NULL };
static const char *webbrowsercmd[] = { "chromium", NULL };

// menu, 1 = on, (0 = off and make sure you dont have a launchmenu keybind)
#define MENU 1
#if MENU
static char *menu1[] = { "xterm", "chromium", "firefox", "libreoffice", "mupen64plus", NULL };
#define MENUS { menu1, NULL }
#endif

#define DESKTOPCHANGE(K,N) \
    {  MOD1,             K,              change_desktop, {.i = N}}, \
    {  MOD1|ShiftMask,   K,              client_to_desktop, {.i = N}},

// keyboard shortcuts
static Key keys[] = {
    // modifier         key             function            argument
    // close focused window 
    {  MOD1|SHIFT,      XK_c,           killclient,         {NULL}}, 
    // decrease gap between windows
    {  MOD1,            XK_minus,       changegap,          {.i = -2}},
    // increase gap between windows
    {  MOD1,            XK_equal,       changegap,          {.i = 2}}, 
    // rotate backwards through desktops
    {  MOD1|CONTROL,    XK_minus,       rotate,             {.i = -1}},
    // rotate fowards through desktops
    {  MOD1|CONTROL,    XK_equal,       rotate,             {.i = +1}},
    // rotate backwards through desktops with windows
    {  MOD1|SHIFT,      XK_minus,       rotate_filled,      {.i = -1}},
    // rotate fowards through desktops with windows
    {  MOD1|SHIFT,      XK_equal,       rotate_filled,      {.i = +1}},
    // move focus only for tiled windows
    {  MOD1|CONTROL,    XK_k,           movefocus,          {.i = TTOP}},
    {  MOD1|CONTROL,    XK_h,           movefocus,          {.i = TLEFT}},
    {  MOD1|CONTROL,    XK_j,           movefocus,          {.i = TBOTTOM}},
    {  MOD1|CONTROL,    XK_l,           movefocus,          {.i = TRIGHT}},
    // grow clients
    {  MOD1,            XK_h,           resizeclient,       {.p = 1, .i = 1, .r = &resizeclientleft}},
    {  MOD1,            XK_l,           resizeclient,       {.p = 1, .i = 1, .r = &resizeclientright}},
    {  MOD1,            XK_j,           resizeclient,       {.p = 1, .i = 1, .r = &resizeclientbottom}},
    {  MOD1,            XK_k,           resizeclient,       {.p = 1, .i = 1, .r = &resizeclienttop}},
    // shrink clients
    {  MOD1|CONTROL,    XK_d,           resizeclient,       {.p = 1, .i = 0, .r = &resizeclientleft}},
    {  MOD1|CONTROL,    XK_a,           resizeclient,       {.p = 1, .i = 0, .r = &resizeclientright}},
    {  MOD1|CONTROL,    XK_w,           resizeclient,       {.p = 1, .i = 0, .r = &resizeclientbottom}},
    {  MOD1|CONTROL,    XK_s,           resizeclient,       {.p = 1, .i = 0, .r = &resizeclienttop}},
    // switch clients, with the first it find up/left/down/right
    {  MOD1|SHIFT,      XK_k,           moveclient,         {.i = TTOP}},
    {  MOD1|SHIFT,      XK_h,           moveclient,         {.i = TLEFT}},
    {  MOD1|SHIFT,      XK_j,           moveclient,         {.i = TBOTTOM}},
    {  MOD1|SHIFT,      XK_l,           moveclient,         {.i = TRIGHT}},
    // push client into tiling
    {  MOD1,            XK_t,           pushtotiling,       {NULL}},
    // pull client to float
    {  MOD1,            XK_f,           pulltofloat,        {NULL}},
    // Switch layouts/directions 
    {  MOD1|SHIFT,      XK_s,           switch_direction,   {.i = TBOTTOM}},
    {  MOD1|SHIFT,      XK_a,           switch_direction,   {.i = TLEFT}},
    {  MOD1|SHIFT,      XK_d,           switch_direction,   {.i = TRIGHT}},
    {  MOD1|SHIFT,      XK_w,           switch_direction,   {.i = TTOP}},
    {  MOD1|SHIFT,      XK_m,           switch_mode,        {.i = MONOCLE}},
    {  MOD1|SHIFT,      XK_v,           switch_mode,        {.i = VIDEO}},
    {  MOD1|SHIFT,      XK_z,           switch_mode,        {.i = FLOAT}},
    // cycle focus through all window
    {  MOD1,            XK_Right,       next_win,           {NULL}},
    {  MOD1,            XK_Left,        prev_win,           {NULL}},
    // quit with exit value 0
    {  MOD1|SHIFT,      XK_q,           quit,               {.i = 0}},
    // quit with exit value 1
    {  MOD1|CONTROL,    XK_q,           quit,               {.i = 1}},
    // launch menu
    {  MOD1|CONTROL,    XK_m,           launchmenu,         {.list = menu1}},
    // launch xterm
    {  MOD1|SHIFT,      XK_Return,      spawn,              {.com = termcmd}},
    // launch web browser
    {  MOD1|SHIFT,      XK_f,           spawn,              {.com = webbrowsercmd}},


    /*------------------------------------
     *  MOVE and RESIZE floating windows  
     *----------------------------------*/
    
    // move down
    //{  MOD4,            XK_j,           moveresize,         {.v = (int []){   0,  25,   0,   0 }}},
    // move up
    //{  MOD4,            XK_k,           moveresize,         {.v = (int []){   0, -25,   0,   0 }}},
    // move right
    //{  MOD4,            XK_l,           moveresize,         {.v = (int []){  25,   0,   0,   0 }}},
    // move left
    //{  MOD4,            XK_h,           moveresize,         {.v = (int []){ -25,   0,   0,   0 }}},
    // height grow
    //{  MOD4|SHIFT,      XK_j,           moveresize,         {.v = (int []){   0,   0,   0,  25 }}},
    // height shrink
    //{  MOD4|SHIFT,      XK_k,           moveresize,         {.v = (int []){   0,   0,   0, -25 }}},
    // width grow
    //{  MOD4|SHIFT,      XK_l,           moveresize,         {.v = (int []){   0,   0,  25,   0 }}},
    // width shrink 
    //{  MOD4|SHIFT,      XK_h,           moveresize,         {.v = (int []){   0,   0, -25,   0 }}}, 
        DESKTOPCHANGE(  XK_1,   0)
        DESKTOPCHANGE(  XK_2,   1)
        DESKTOPCHANGE(  XK_3,   2)
        DESKTOPCHANGE(  XK_4,   3)
        DESKTOPCHANGE(  XK_5,   4)
        DESKTOPCHANGE(  XK_6,   5)
        DESKTOPCHANGE(  XK_7,   6)
        DESKTOPCHANGE(  XK_8,   7)
        DESKTOPCHANGE(  XK_9,   8)
};

// mouse shortcuts
static Button buttons[] = {
    {  MOD1,    Button1,     mousemotion,   {.i = MOVE}},
    {  MOD1,    Button3,     mousemotion,   {.i = RESIZE}}, 
};
#endif

/* vim: set expandtab ts=4 sts=4 sw=4 : */

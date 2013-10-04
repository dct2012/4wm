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
#define SHOW_PANEL              True
// False means panel is on bottom
#define TOP_PANEL               True
// 0 for no space for panel, thus no panel
#define PANEL_HEIGHT            16
// Panel monitor number, 1 = first, 2 = second, etc
#define PANEL_MON               1
// initial layout/mode: TRIGHT TLEFT TTOP TBOTTOM MONOCLE VIDEO
#define DEFAULT_MODE            TRIGHT
// follow the window when moved to a different desktop
#define FOLLOW_WINDOW           False
// focus the window the mouse just entered
#define FOLLOW_MOUSE            True
// focus an unfocused window when clicked
#define CLICK_TO_FOCUS          True
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
// urgent color for border
#define URGNBRDRCOL     "#FFFF5F"
// minimum window size in pixels
#define MINWSZ          50          
// the desktop to focus initially
#define DEFAULT_DESKTOP 0
// number of desktops - edit DESKTOPCHANGE keys to suit
#define DESKTOPS        9
// the default size of the gap between windows in pixels
#define GAP             8
// the maximum gap size
#define MAXGAP          30
// the minimum gap size
#define MINGAP          0


/**
 * open applications to specified desktop with specified mode.
 * if desktop is negative, then current is assumed
 */
static const AppRule rules[] = { \
    /*  class     desktop  follow  float */
    { "MPlayer",     3,    False,   False },
    //{ "Gimp",        0,    False,  True  },
};

// helper for spawning shell commands
#define SHCMD(cmd) {.com = (const char*[]){"/bin/sh", "-c", cmd, NULL}}

// custom commands, must always end with ', NULL };'
static const char *termcmd[] = { "xterm",     NULL };
static const char *webbrowsercmd[] = { "chromium", NULL };
static const char *filemanagercmd[] = { "dolphin", NULL };


#define DESKTOPCHANGE(K,N) \
    {  MOD1,             K,              change_desktop, {.i = N}}, \
    {  MOD1|ShiftMask,   K,              client_to_desktop, {.i = N}},

// keyboard shortcuts
static Key keys[] = {
    // modifier         key             function            argument
    {  MOD1,            XK_b,           togglepanel,        {NULL}},
    {  MOD1,            XK_BackSpace,   focusurgent,        {NULL}},
    // close focused window 
    {  MOD1|SHIFT,      XK_c,           killclient,         {NULL}}, 
    // decrease gap between windows
    {  MOD1,            XK_minus,       decreasegap,        {.i = 2}},
    // increase gap between windows
    {  MOD1,            XK_equal,       increasegap,        {.i = 2}}, 
    // rotate backwards through desktops
    {  MOD1|CONTROL,    XK_minus,       rotate,             {.i = -1}},
    // rotate fowards through desktops
    {  MOD1|CONTROL,    XK_equal,       rotate,             {.i = +1}},
    // rotate backwards through desktops with windows
    {  MOD1|SHIFT,      XK_minus,       rotate_filled,      {.i = -1}},
    // rotate fowards through desktops with windows
    {  MOD1|SHIFT,      XK_equal,       rotate_filled,      {.i = +1}},
    // move focus
    {  MOD1|CONTROL,    XK_k,           movefocusup,        {NULL}},
    {  MOD1|CONTROL,    XK_h,           movefocusleft,      {NULL}},
    {  MOD1|CONTROL,    XK_j,           movefocusdown,      {NULL}},
    {  MOD1|CONTROL,    XK_l,           movefocusright,     {NULL}},
    // resize clients
    {  MOD1,            XK_h,           resizeclientleft,   {.d = .01}},
    {  MOD1,            XK_l,           resizeclientright,  {.d = .01}},
    {  MOD1,            XK_j,           resizeclientbottom, {.d = .01}}, 
    {  MOD1,            XK_k,           resizeclienttop,    {.d = .01}}, 
    // switch clients, with the first it find up/left/down/right
    {  MOD1|SHIFT,      XK_k,           moveclientup,       {NULL}},
    {  MOD1|SHIFT,      XK_h,           moveclientleft,     {NULL}},
    {  MOD1|SHIFT,      XK_j,           moveclientdown,     {NULL}},
    {  MOD1|SHIFT,      XK_l,           moveclientright,    {NULL}},
    // push client into tiling
    {  MOD1,            XK_t,           pushtotiling,       {NULL}},
    // Switch layouts
    {  MOD1|SHIFT,      XK_w,           switch_mode,        {.i = TTOP}},
    {  MOD1|SHIFT,      XK_m,           switch_mode,        {.i = MONOCLE}},
    {  MOD1|SHIFT,      XK_s,           switch_mode,        {.i = TBOTTOM}},
    {  MOD1|SHIFT,      XK_d,           switch_mode,        {.i = TRIGHT}},
    {  MOD1|SHIFT,      XK_a,           switch_mode,        {.i = TLEFT}},
    {  MOD1|SHIFT,      XK_v,           switch_mode,        {.i = VIDEO}},
    //{  MOD1|SHIFT,      XK_z,           switch_mode,        {.i = FLOAT}},
    // quit with exit value 0
    {  MOD1|SHIFT,      XK_q,           quit,               {.i = 0}},
    // quit with exit value 1
    {  MOD1|CONTROL,    XK_q,           quit,               {.i = 1}},
    // launch xterm
    {  MOD1|SHIFT,      XK_Return,      spawn,              {.com = termcmd}},
    // launch web browser
    {  MOD1|SHIFT,      XK_f,           spawn,              {.com = webbrowsercmd}}, 
    // launch file manager
    {  MOD1|SHIFT,      XK_p,           spawn,              {.com = filemanagercmd}},


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

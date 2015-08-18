#define MOD1            Mod1Mask    // ALT key
#define MOD4            Mod4Mask    // Super/Windows key
#define CONTROL         ControlMask // Control key
#define SHIFT           ShiftMask   // Shift key

// show panel by default on exec
#define SHOW_PANEL              true
// false means panel is on bottom
#define TOP_PANEL               false
// 0 for no space for panel, thus no panel
#define PANEL_HEIGHT            32
// Panel monitor number, 1 = first, 2 = second, etc
#define PANEL_MON               1
// number of desktops - edit DESKTOPCHANGE keys to suit
#define DESKTOPS        9
// initial layout/mode: TILE MONOCLE VIDEO
#define DEFAULT_MODE            TILE
// initial tiling direction: TBOTTOM, TLEFT, TRIGHT, TTOP
#define DEFAULT_DIRECTION       TRIGHT
// the default size of the gap between windows in pixels
#define GAP             8
// focus the window the mouse just entered
#define FOLLOW_MOUSE            true
// focus an unfocused window when clicked
#define CLICK_TO_FOCUS          true
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
// floating outer border color
#define FLTBRDCOL       "#00FF5F"

// desktop info, 1 = on, 0 = off
#define DESKTOP_INFO 0
#if DESKTOP_INFO
#define DI_CMD { "dzen2", "-x", "0", "-y", "1668", "-h", "32", "-w", "1280", NULL }
#define DI_COL_CURRENT  "#005FFF"
#define DI_COL_URGENT   "#FFFF5F"
#define DI_COL_VISIBLE  "#FFFFFF"
#define DI_COL_HIDDEN   "#262626"
#define DI_COL_DIR      "#00FF5F"
#define DI_COL_MODE     "#AF00FF"
#define DI_COL_TITLE    "#FFFFFF"
#define DI_TAGS_WS { "1", "2", "3", "4", "5", "6", "7", "8", "9" }
#define DI_TAGS_MODE { "T", "M", "V", "F" }
#define DI_TAGS_DIR { "L", "R", "D", "U" } 
#define DI_PRINTF printf("%s %s %s ^fg(%s)%s\n", di.ws, di.mode, di.dir, DI_COL_TITLE, d->current ? d->current->title :"");
#endif

static const char *termcmd[] = { "xterm", NULL };
static const char *webcmd[] = { "google-chrome-beta", NULL };

#define DESKTOPCHANGE(K,N) \
    {  MOD1,             K,              change_desktop, {.i = N}},
    //{  MOD1|ShiftMask,   K,              client_to_desktop, {.i = N}},

static Key keys[] = {
    // close focused window 
    {  MOD1|SHIFT,      XK_c,           killwindow,         {NULL}},
    // launch xterm
    {  MOD1|SHIFT,      XK_Return,      spawn,              {.com = termcmd}},
    // launch webbrowser
    {  MOD1|SHIFT,      XK_f,      spawn,              {.com = webcmd}},
    //quit
    { MOD1|SHIFT,       XK_q,           quit,               {NULL}},
    // Switch layouts/directions 
    {  MOD1|SHIFT,      XK_s,           switch_direction,   {.i = TBOTTOM}},
    {  MOD1|SHIFT,      XK_a,           switch_direction,   {.i = TLEFT}},
    {  MOD1|SHIFT,      XK_d,           switch_direction,   {.i = TRIGHT}},
    {  MOD1|SHIFT,      XK_w,           switch_direction,   {.i = TTOP}},

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

static Button buttons[] = {
    { MOD1, Button1, mousemotion, {.i = MOVE}},
    { MOD1, Button3, mousemotion, {.i = RESIZE}},
};

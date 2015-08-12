typedef struct Menu {
    char **list;                // list to hold the original list of commands
    struct Menu *next;          // next menu incase of multiple
    struct Menu_Entry *head;
} Menu;

typedef struct Menu_Entry {
    char *cmd[2];                               // cmd to be executed
    int x, y;                                   // w and h will be default or defined
    struct Menu_Entry *next, *b, *l, *r, *t;    // next and neighboring entries
    xcb_rectangle_t *rectangles;                // tiles to draw
} Menu_Entry;

typedef struct Xresources {
    unsigned int color[12];
    xcb_gcontext_t gc_color[12];
    xcb_gcontext_t font_gc[12];
} Xresources;

typedef struct monitor {
    xcb_randr_output_t id;  // id
    int x, y, w, h;         // size in pixels
    struct monitor *next;   // the next monitor after this one
    int curr_dtop;          // which desktop the monitor is displaying
    bool haspanel;          // does this monitor display a panel
} monitor;

extern bool getrootptr(int *x, int *y);
extern monitor *wintomon(xcb_window_t w);

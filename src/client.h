#define BUTTONMASK      XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE

/* a client is a wrapper to a window that additionally
 * holds some properties for that window
 *
 * isurgent    - set when the window received an urgent hint
 * istransient - set when the window is transient
 * isfloating  - set when the window is floating
 *
 * istransient is separate from isfloating as floating window can be reset
 * to their tiling positions, while the transients will always be floating
 */
typedef struct client {
    struct client *next;                                // the client after this one, or NULL if the current is the last client
    int x, y, w, h;                                     // actual window size
    int xp, yp, wp, hp;                                 // percent of monitor, before adjustment (percent is a int from 0-100)
    int gapx, gapy, gapw, gaph;                         // gap sizes
    bool isurgent, istransient, isfloating;             // property flags
    xcb_window_t win;                                   // the window this client is representing
    char *title;
} client;


extern void adjustclientgaps(const int gap, client *c);
extern bool clientstouchingbottom(desktop *d, client *c, client **list, int *num);
extern bool clientstouchingleft(desktop *d, client *c, client **list, int *num);
extern bool clientstouchingright(desktop *d, client *c, client **list, int *num);
extern bool clientstouchingtop(desktop *d, client *c, client **list, int *num);
extern void deletewindow(xcb_window_t w);
extern void focus(client *c, desktop *d, const monitor *m);
extern void grabbuttons(client *c);
extern client* prev_client(client *c, desktop *d);
extern void setclientborders(desktop *d, client *c, const monitor *m);

/* properties of each desktop
 * mode         - the desktop's tiling layout mode
 * gap          - the desktops gap size
 * direction    - the direction to tile
 * count        - the number of clients on that desktop
 * head         - the start of the client list
 * current      - the currently highlighted window
 * prevfocus    - the client that previously had focus
 * dead         - the start of the dead client list
 * showpanel    - the visibility status of the panel
 */
typedef struct {
    int mode, gap, direction, count;
    client *head, *current, *prevfocus;
    bool showpanel;
} desktop;

extern void setdesktopborders(desktop *d, const monitor *m);

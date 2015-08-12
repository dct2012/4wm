//argument structure to be passed to function by config.h 
typedef struct {
    const char** com;                                                               // a command to run
    const int i;                                                                    // an integer to indicate different states
    const int p;                                                                    // represents a percentage for resizing
    void (*m)(int*, client*, client**, desktop*);                                   // for the move client command
    void (*r)(desktop*, const int, int*, const int, client*, monitor*, client**);   // for the resize client command
    char **list;                                                                    // list for menus
} Arg;

// a key struct represents a combination of
typedef struct {
    unsigned int mod;           // a modifier mask
    xcb_keysym_t keysym;        // and the key pressed
    void (*func)(const Arg *);  // the function to be triggered because of the above combo
    const Arg arg;              // the argument to the function
} Key;

// a button struct represents a combination of
typedef struct {
    unsigned int mask, button;  // a modifier mask and the mouse button pressed
    void (*func)(const Arg *);  // the function to be triggered because of the above combo
    const Arg arg;              // the argument to the function
} Button;



extern void change_desktop(const Arg *arg);
extern void client_to_desktop(const Arg *arg);
extern void decreasegap(const Arg *arg);
extern void focusurgent();
extern void increasegap(const Arg *arg);
extern void killclient();
extern void launchmenu(const Arg *arg);
extern void moveclient(const Arg *arg);
extern void moveclientup(int *num, client *c, client **list, desktop *d);
extern void moveclientleft(int *num, client *c, client **list, desktop *d);
extern void moveclientdown(int *num, client *c, client **list, desktop *d);
extern void moveclientright(int *num, client *c, client **list, desktop *d);
extern void movefocus(const Arg *arg);
extern void mousemotion(const Arg *arg);
extern void next_win();
extern void prev_win();
extern void pulltofloat();
extern void pushtotiling();
extern void quit(const Arg *arg);
extern void resizeclient(const Arg *arg);
extern void resizeclientbottom(desktop *d, const int grow, int *n, const int size, client *c, monitor *m, client **list);
extern void resizeclientleft(desktop *d, const int grow, int *n, const int size, client *c, monitor *m, client **list);
extern void resizeclientright(desktop *d, const int grow, int *n, const int size, client *c, monitor *m, client **list);
extern void resizeclienttop(desktop *d, const int grow, int *n, const int size, client *c, monitor *m, client **list);
extern void rotate(const Arg *arg);
extern void rotate_filled(const Arg *arg);
extern void spawn(const Arg *arg);
extern void switch_mode(const Arg *arg);
extern void switch_direction(const Arg *arg);
extern void togglepanel();

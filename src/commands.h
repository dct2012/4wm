#ifndef FRANKENSTEINWM_COMMANDS_H
#define FRANKENSTEINWM_COMMANDS_H

void change_desktop(const Arg *arg);
void client_to_desktop(const Arg *arg);
void decreasegap(const Arg *arg);
void focusurgent();
void increasegap(const Arg *arg);
void killclient();
void launchmenu(const Arg *arg);
void moveclient(const Arg *arg);
void movefocus(const Arg *arg);
void mousemotion(const Arg *arg);
void pushtotiling();
void quit(const Arg *arg);
void resizeclient(const Arg *arg);
void rotate(const Arg *arg);
void rotate_filled(const Arg *arg);
void spawn(const Arg *arg);
void switch_mode(const Arg *arg);
void switch_direction(const Arg *arg);
void togglepanel();

void growbyh(client *match, const float size, client *c, monitor *m);
void growbyw(client *match, const float size, client *c, monitor *m);
void growbyx(client *match, const float size, client *c, monitor *m);
void growbyy(client *match, const float size, client *c, monitor *m);

void moveclientup(int *num, client *c, client **list, desktop *d);
void moveclientleft(int *num, client *c, client **list, desktop *d);
void moveclientdown(int *num, client *c, client **list, desktop *d);
void moveclientright(int *num, client *c, client **list, desktop *d);
void resizeclientbottom(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list);
void resizeclientleft(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list);
void resizeclientright(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list);
void resizeclienttop(desktop *d, const int grow, int *n, const float size, client *c, monitor *m, client **list);

#endif

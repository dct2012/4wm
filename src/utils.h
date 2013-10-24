#ifndef FRANKENSTEINWM_UTILS_H
#define FRANKENSTEINWM_UTILS_H

void adjustclientgaps(const int gap, client *c);
bool clientstouchingbottom(desktop *d, client *c, client **list, int *num);
bool clientstouchingleft(desktop *d, client *c, client **list, int *num);
bool clientstouchingright(desktop *d, client *c, client **list, int *num);
bool clientstouchingtop(desktop *d, client *c, client **list, int *num);
void deletewindow(xcb_window_t w);
void desktopinfo(void);
void focus(client *c, desktop *d);
bool getrootptr(int *x, int *y);
void gettitle(client *c);
void grabbuttons(client *c);
void grabkeys(void);
client* prev_client(client *c, desktop *d);
monitor* ptrtomon(int x, int y);
void setborders(desktop *d);
monitor *wintomon(xcb_window_t w);

bool (*findtouchingclients[TDIRECS])(desktop *d, client *c, client **list, int *num) = {
    [TBOTTOM] = clientstouchingbottom, [TLEFT] = clientstouchingleft, [TRIGHT] = clientstouchingright, [TTOP] = clientstouchingtop,
};

#endif

/* vim: set ts=4 sw=4 :*/

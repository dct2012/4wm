#ifndef FRANKENSTEINWM_TILING_H
#define FRANKENSTEINWM_TILING_H

void monocle(int x, int y, int w, int h, const desktop *d, const monitor *m);
void retile(desktop *d, const monitor *m);
void tilenew(desktop *d, const monitor *m);
void tilenewbottom(client *n, client *c);
void tilenewleft(client *n, client *c);
void tilenewright(client *n, client *c);
void tilenewtop(client *n, client *c);
void tileremove(desktop *d, const monitor *m);

#endif

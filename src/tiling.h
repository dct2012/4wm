#define ISFT(c)        (c->isfloating || c->istransient)

// TILING
extern void monocle(int x, int y, int w, int h, const desktop *d, const monitor *m);
extern void retile(desktop *d, const monitor *m);
extern void tilenew(desktop *d, const monitor *m);
extern void tilenewbottom(client *n, client *c);
extern void tilenewleft(client *n, client *c);
extern void tilenewright(client *n, client *c);
extern void tilenewtop(client *n, client *c);
extern void tileremove(client *dead, desktop *d, const monitor *m);

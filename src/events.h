#ifndef FRANKENSTEINWM_EVENTS_H
#define FRANKENSTEINWM_EVENTS_H

client* addwindow(xcb_window_t w, desktop *d);
void buttonpress(xcb_generic_event_t *e);
void clientmessage(xcb_generic_event_t *e);
desktop *clienttodesktop(client *c);
void configurerequest(xcb_generic_event_t *e);
void destroynotify(xcb_generic_event_t *e);
void enternotify(xcb_generic_event_t *e);
void expose(xcb_generic_event_t *e);
void focusin(xcb_generic_event_t *e);
void keypress(xcb_generic_event_t *e);
void mappingnotify(xcb_generic_event_t *e);
void maprequest(xcb_generic_event_t *e);
void propertynotify(xcb_generic_event_t *e);
void removeclient(client *c, desktop *d, const monitor *m);
void unmapnotify(xcb_generic_event_t *e);
client *wintoclient(xcb_window_t w);

#endif

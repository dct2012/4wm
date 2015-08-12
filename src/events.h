// EVENTS
extern void buttonpress(xcb_generic_event_t *e);
extern void clientmessage(xcb_generic_event_t *e);
extern void configurerequest(xcb_generic_event_t *e);
extern void destroynotify(xcb_generic_event_t *e);
extern void enternotify(xcb_generic_event_t *e);
extern void expose(xcb_generic_event_t *e);
extern void focusin(xcb_generic_event_t *e);
extern void keypress(xcb_generic_event_t *e);
extern void mappingnotify(xcb_generic_event_t *e);
extern void maprequest(xcb_generic_event_t *e);
extern void propertynotify(xcb_generic_event_t *e);
extern void unmapnotify(xcb_generic_event_t *e);

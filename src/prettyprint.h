#if PRETTY_PRINT


typedef struct pp_data {
    char *ws;
    char *mode;
    char *dir;
} pp_data;



extern void desktopinfo(void);
extern void updatedir();
extern void updatemode();
extern void updatetitle(client *c);
extern void updatews();

#endif

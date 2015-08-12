void setdesktopborders(desktop *d, const monitor *m) {
    client *c = NULL;
    for (c = d->head; c; c = c -> next)
        setclientborders(d, c, m);
}

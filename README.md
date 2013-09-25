frankensteinwm
=============


----------------------
**frankensteinwm** is a minimal, lightweight, dynamic tiling window manager with 
two borders. Its aim is to be small, fast, and versatile. It has one tiling layout, where
it splits windows in four different ways, and two fullscreen layouts. Each virtual desktop
has its own properties, unaffected by other desktops' or monitors' settings.

Modes
-----

The tiling layout has four modes: left, right, top, bottom. The layout splits the current
window in half and adds the new window to the left, right, top, or bottom. All windows can
be resized in four directions, switch places in four directions, and the focus can move in
four directions. 
The two fullscreen layouts are: monocle and video. Both with no borders. Monocle is aware
of gaps and video ignores both gaps and panels.

Panel - Statusbar
-----------------

The user can define an empty space (by default 16px) on the bottom or top(default) of the
screen, to be used by a panel. The panel is toggleable, but will be visible if no windows
are on the screen.

Frankensteinwm does not provide a panel and/or statusbar itself. Instead it adheres
to the [UNIX philosophy][unix] and outputs information about the existent
desktop, the number of windows on each, the mode of each desktop, the current
desktop and urgent hints whenever needed. The user can use whatever tool or
panel suits him best (dzen2, conky, w/e), to process and display that information.

To disable the panel completely set `PANEL_HEIGHT` to zero `0`.
The `SHOW_PANELL` setting controls whether the panel is visible on startup,
it does not control whether there is a panel or not.

 [unix]: http://en.wikipedia.org/wiki/Unix_philosophy

Here is a list of minimal and lightweight panels:

 * [`bar`](https://github.com/LemonBoy/bar)
 * [`some_sorta_bar`](https://github.com/moetunes/Some_sorta_bar)
 * [`bipolarbar`](https://github.com/moetunes/bipolarbar)
 * [`mopag`](https://github.com/c00kiemon5ter/mopag) (pager)

You can find an examples configurations of panels [here](https://gist.github.com/1905427).
You can actually parse monsterwm's output with any language you want,
build anything you want, and display the information however you like.
Do not be limited by those examples.

Installation
------------

You need xcb and xcb-utils then,
copy `config.def.h` as `config.h`
and edit to suit your needs.
Build and install.

    $ cp config.def.h config.h
    $ $EDITOR config.h
    $ make
    # make clean install

TODO
----

Work on steam
  * a lot of configure requests when window sized too small
  * games sometimes work, when they dont they're black/blank windows

Gimp
  * when killing client, another window pops up, without configure request, map notify, etc

Applications that ask before leaving
  * client get killed before question is answered. so if the question is, are you sure you
  want to exit, and you say no, that client is already gone.
  * firefox, libreoffice

Bugs
----

For any bug or request [fill an issue][bug] on [GitHub][ghp].

  [bug]: https://github.com/dct2012/frankensteinwm/issues
  [ghp]: https://github.com/dct2012/frankensteinwm


License
-------

Licensed under MIT/X Consortium License, see [LICENSE][law] file for more copyright and license information.

   [law]: https://raw.github.com/dct2012/frankensteinwm/master/LICENSE

Thanks
------

[the suckless team][skls] for [dwm][], 
[moetunes][] for [dminiwm][], 
[pyknite][] for [catwm][], 
[c00kiemonster][cookiemonster] for [monsterwm][], 
[Cloudef][cloudef] for [monsterwm-xcb][], 
[venam][vnm] for [2bwm][]

  [skls]: http://suckless.org/
  [dwm]:  http://dwm.suckless.org/
  [moetunes]: https://github.com/moetunes
  [dminiwm]:  https://bbs.archlinux.org/viewtopic.php?id=126463
  [pyknite]: https://github.com/pyknite
  [catwm]:   https://github.com/pyknite/catwm
  [monsterwm]: https://github.com/c00kiemon5ter/monsterwm
  [cookiemonster]: https://github.com/c00kiemon5ter
  [monsterwm-xcb]: https://github.com/Cloudef/monsterwm-xcb
  [cloudef]: https://github.com/Cloudef
  [2bwm]: https://github.com/venam/2bwm
  [vnm]: https://github.com/venam


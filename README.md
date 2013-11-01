frankensteinwm
=============


----------------------
**frankensteinwm** is a versatile, lightweight, dynamic tiling window manager with 
two borders. Its mission is to be small, versatile, and quick. It has three layouts: a 
tiling layout and two fullscreen layouts. There are four direction in which you can tile,
four directions in which a window can swap with another, and four directions in which you
can resize a window. Each desktop can have gaps between windows and the size can change. 
Each virtual desktop has its own properties, unaffected by other desktops' or monitors' 
settings.

Layouts
-----

The three layouts are: tiling, monocle and video. Monocle is a fullscreen layout with no
borders and obeys the desktops' gap and the monitors' panel. Video is a fullscreen layout 
that has no borders and ignores both the desktops' gap and monitors' panel. The tiling 
layout, along with the other two layouts, split the current window in half and place the 
new window in the direction of it's mode. Tiling is the only layout that displays the 
windows in their correct position.

Modes
-----

The four modes are: tbottom, tleft, tright, ttop. They are simply just directions in
which to tile.

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

You can find examples of configurations for panels [here](https://gist.github.com/1905427).
You can actually parse frankensteinwm's output with any language you want,
build anything you want, and display the information however you like.
Do not be limited by those examples.

Menu - launcher
---------------

Frankensteinwm provides a menu or a launcher similar to xmonad's grid select. It will use
the colors from your Xdefaults for the tiles.

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

* Work on steam
  * games 
    * launch instances of pulseaudio which hang; leaving the window black or unresponsive.
      killing these pulseaudio processes allow the game to resume. alternatively you can
      launch steam or the game with SDL_AUDIODRIVER=alsa

* Work on menu.
  * limit string length to rectangle/tile width.
  * add user defined fonts in Xdefaults.
  * add keyboard use.

* Add a way to pipe desktopinfo to a panel like xmonad.

* overall organization.
  * look for conditional statements that can be moved to preprocessor conditionals.
  * preprocessor conditions for function/variables/etc.
  * constantly impove readability.

* Work on floating layout.
  * need to work on tilenew when current client is a floater.
    * finding the client to tile with.
  * in floating mode, clients that are tiled are not being retiled after being destroyed.

* Work on push to tiling.
  * finding the client to tile with.

Bugs
----

For any bug or request [fill an issue][bug] on [GitHub][ghp]. Also check the [TODO][tdo]
for your issue.

  [bug]: https://github.com/dct2012/frankensteinwm/issues
  [ghp]: https://github.com/dct2012/frankensteinwm
  [tdo]: https://github.com/dct2012/frankensteinwm#TODO


License
-------

Licensed under MIT/X Consortium License, see [LICENSE][law] file for more copyright and license information.

   [law]: https://raw.github.com/dct2012/frankensteinwm/master/LICENSE

Thanks
------

* [the suckless team][skls] for [dwm][] 
* [c00kiemonster][cookiemonster] for [monsterwm][]
* [Cloudef][cloudef] for [monsterwm-xcb][]
* [venam][vnm] for [2bwm][]
* Michael Stapelberg for [i3][]
* [moetunes][] for [dminiwm][]
* [pyknite][] for [catwm][] 


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
  [i3]: http://i3wm.org/

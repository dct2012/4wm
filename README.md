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

Currently, frankensteinwm only provides you with a way to clear space for a panel and
with desktop information that needs to be parsed. For further details refer to 
[monsterwm][monsterwm-panel].

  [monsterwm-panel]: https://github.com/c00kiemon5ter/monsterwm#panel---statusbar

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

* Overall organization.
  * look for conditional statements that can be moved to preprocessor conditionals.
  * preprocessor conditions for function/variables/etc.
  * constantly impove readability.

* Work on floating layout. 
  * handle movefocus and moveclient.

* Add a command to invert the current windows color.

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

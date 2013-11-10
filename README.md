4wm
=============


----------------------
**4wm** is a versatile, lightweight, dynamic tiling window manager with 
two borders. Its mission is to be small, versatile, and quick. There are four modes: 
tiling, floating, monocle and video.. There are four direction in which you can tile, four
directions in which a window can swap with another, and four directions in which you can 
resize a window. Each desktop can have gaps between windows and the size can be change. 
Each virtual desktop has its own properties, unaffected by other desktops' or monitors' 
settings.

Modes
-----

There are four modes: tiling, floating, monocle and video. Tiling has four directions it 
can tile: bottom, left, right, top. In tile mode the current window will be resized half
its size and the new window will be placed in the empty space to the direction selected.
Monocle is a fullscreen mode that respects the desktops' gaps and a screens' panel. Video
is a fullscreen mode that ignores both the desktops' gaps and a screens' panel. This is a 
tiling window manager, so anytime a window is created, we tile/handle/manage it. The 
exception to the rule is in floating mode, where the window will configure its width and 
height then we'll move it to the center, from there the user will manage it. In video and 
monocle mode each window is handled like it is in the tiling mode; it is only drawn 
fullscreen.

Panel - Statusbar
-----------------

Currently, 4wm only provides you with a way to clear space for a panel and
with desktop information that needs to be parsed. For further details refer to 
[monsterwm][monsterwm-panel].

  [monsterwm-panel]: https://github.com/c00kiemon5ter/monsterwm#panel---statusbar

Menu - launcher
---------------

4wm provides a menu or a launcher similar to xmonad's grid select. It will use
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

***BUGS*** 
  * steam
    * games 
      * launch instances of pulseaudio which hang; leaving the window black or unresponsive.
        killing these pulseaudio processes allow the game to resume. alternatively you can
        launch steam or the game with SDL_AUDIODRIVER=alsa

***New Features***
  * Menu.
    * make sure we can compile without all this stuff defined
    * limit string length to rectangle/tile width.
    * add user defined fonts in Xdefaults.
    * add keyboard use.

  * pretty print.
    * make sure we can compile without all this stuff defined
    * make sure it also works for messengers other than dzen2
    * reduce calls to printf()
    * add a way to pipe another stream (conky) to dzen
    * handle urgent
    * try to allow the user to control what is displayed and the order
    * maybe add seperators

  * Add a command to invert the current windows color.

***MISC***
  * Overall organization.
    * look for conditional statements that can be moved to preprocessor conditionals.
    * preprocessor conditions for function/variables/etc.
    * constantly impove readability.

  * Work on the man page.

Bugs
----

For any bug or request [fill an issue][bug] on [GitHub][ghp]. Also check the [TODO][tdo]
for your issue.

  [bug]: https://github.com/dct2012/4wm/issues
  [ghp]: https://github.com/dct2012/4wm
  [tdo]: https://github.com/dct2012/4wm#TODO


License
-------

Licensed under MIT/X Consortium License, see [LICENSE][law] file for more copyright and license information.

   [law]: https://raw.github.com/dct2012/4wm/master/LICENSE

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

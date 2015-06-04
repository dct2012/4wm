4wm
=============


----------------------
**4wm** is a versatile, lightweight, dynamic tiling window manager with 
two borders. Its mission is to be small, versatile, and quick. There are four modes: 
tiling, floating, monocle and video. There are four direction in which you can tile, four
directions in which a window can swap with another, and four directions in which you can 
resize a window. Each desktop can have gaps between windows and the size can be change. 
Each virtual desktop has its own properties, unaffected by other desktops' or monitors' 
settings.

Modes
-----

There are four modes: tiling, floating, monocle and video. 
* **Tiling**
  * Tiling mode has four directions it can tile: bottom, left, right, and top. In tile 
  mode, the current window will be resized half of its size and the new window will be 
  placed in the new space created in the direction selected.
* **Monocle**
  * Monocle is a fullscreen mode that respects the desktops' gaps and a screens' panel. 
* **Video**
  * Video is a fullscreen mode that ignores both the desktops' gaps and a screens' panel. 

This is a tiling window manager, so anytime a window is created, we tile/handle/manage it. 
The exception to the rule is in floating mode.
* **Floating**
  * In floating mode, the window will configure its width and height, then we'll move it
  to the center, from there the user will manage it.

In video and monocle mode, each window is handled like it is in the tiling mode; it only 
appears to be fullscreen.

Panel - Statusbar
-----------------

4wm does not provide a panel or statusbar. It provides a way to pipe the information to a
panel or statusbar. Currently, only [dzen2][dz2] is supported.

  [dz2]: https://github.com/robm/dzen

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

Donate
------

Bitcoin: 1EqURvUkiP4BtZBxY6X8uUSe6F2zPtqGEB  
Litecoin: LWeBjVZHsR5KnV4t89x7ESHVgyvwDSjZZP  
Dogecoin: DDrqVPBkC2e3c9Z56oWRnJxssdNFncUbSQ  

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

TODO
----

***BUGS***
  * killing a chrome window, causes snafu (wtf). You can exit chrome through it's menu and all will be
    fine. But we need to fix this.
  * some programs have windows that don't get unmapped, like they dont send a unmapnotify or something. 
    So the window will disapear and the client will still be there. steam and drracket are known to do
    this.
  * Add the launch option -window to steam games. (I'll probably move this outside of bugs, make a notes
    section or something.)
  * stdin issue. I think this is when other programs print to the same stream we're reading from with
    pretty print. I believe dolphin does this, not sure what the case for it is though. The fix to this
    we be somehow making out pipe more private.

***New Features***
  * add its own pager like dzen2
  * steal form kde's android notification system
  * maybe add three finger scroll through workspaces like OSX
  * Menu.
    * limit string length to rectangle/tile width.
    * add user defined fonts in Xdefaults.
    * add keyboard use.
  * pretty print.
    * make sure it also works for messengers other than dzen2.
  * make a "placeholder"
    * I sometimes make use xterm (a window) just to manipulate how windows
      will be tiled.
  * add a command to invert the current windows color.

***MISC***
  * Overall organization.
    * look for conditional statements that can be moved to preprocessor conditionals.
    * preprocessor conditions for function/variables/etc.
    * constantly improve readability.
  * Work on the man page.
  * Double check all options.

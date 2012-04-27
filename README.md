##dminiwm
### it's minimal and dynamic

I started this from catwm 31/12/10 ( https://bbs.archlinux.org/viewtopic.php?id=100215&p=1 )
    See dminiwm.c for thanks and licensing.
Screenshots and ramblings/updates at https://bbs.archlinux.org/viewtopic.php?id=126463


###Summary
-------


**dminiwm** is a very minimal and lightweight dynamic tiling window manager.

>    I will try to stay under 1000 SLOC.

>    Currently under 960 lines with the config file included.


###Modes
-----

It allows the "normal" method of tiling window managers(with the new window as the master)
    or with the new window opened at the bottom or the top of the stack.

 *There's vertical tiling mode:*

    --------------
    |        | W |
    |        |___|
    | Master |   |
    |        |___|
    |        |   |
    --------------

 *Horizontal tiling mode:*

    -------------
    |           |
    |  Master   |
    |-----------|
    | W |   |   |
    -------------
    
 *Grid tiling mode:*

    -------------
    |      | W  |
    |Master|    |
    |------|----|
    |      |    |
    -------------

 *Stacking mode:*

    -------------
    |   _______  |
    |  |  ___  | |
    |  | |___| | |
    |  |_______| |
    -------------


 *Fullscreen mode*(which you'll know when you see it)

 All accessible with keyboard shortcuts defined in the config.h file.
 
 * The window *W* at the top of the stack can be resized on a per desktop basis.
 * Changing a tiling mode or window size on one desktop doesn't affect the other desktops.


###Recent Changes
--------------

27/4/12

>	Added stacking mode

###Status
------

There are more options in the config file than the original catwm.

  * Fixed the window manager crashing on a bad window error.
  * Fixed the keyboard shortcuts not working if numlock was on.
  * Added some functions.
  * Added an option to focus the window the mouse just moved to.
  * Fixed a window being destroyed on another desktop creating ghost windows.
  * Added ability to resize the window on the top of the stack
  * Added having applications open on specified desktop
  * Added a click to focus option
  * Added ability to change back to last opened desktop.
  * Transient windows are now always floating and always on top
  * Option in the config file to not show a panel when a window is open.
  * Last focused window is refocused and the mouse moved to it on desktop change
  * Added option in the config to have new window opened top or bottom of the stack when using atach aside


###Installation
------------

Need Xlib, then:

    edit the config.h.def file to suit your needs
        and save it as config.h.

    $ make
    # make install
    $ make clean


###Bugs
----

[ * No bugs for the moment ;) (I mean, no importants bugs ;)]


###Todo
----

  * 


Clevo & System76 Fan Controller 
===============================

This was scrubbed from gtk and graphical dependencies (see https://github.com/SkyLandTW/clevo-indicator for the original) in order
to be usable as a CLI tool and/or system service. This further allows me to write a more sophisticated PID-controller thermal algo in higher language
and only use this to translate to the EmbeddedController by clevo, which AqD <iiiaqd@gmail.com> already handled.




Clevo Fan Control Indicator for Ubuntu
======================================

This program is an Ubuntu indicator to control the fan of Clevo laptops, using reversed-engineering port information from ECView.

It shows the CPU temperature on the left and the GPU temperature on the right, and a menu for manual control.

![Clevo Indicator Screen](http://i.imgur.com/ucwWxLq.png)



For command-line, use *-h* to display help, or a number representing percentage of fan duty to control the fan (from 40% to 100%).


Build and Install
-----------------

```shell
git clone https://github.com/mmt050/clevo-fancontrol.git
cd clevo-indicator
make install
```


Notes
-----

Be careful not to use any other program accessing the EC by low-level IO
syscalls (inb/outb) at the same time - I don't know what might happen, since
every EC actions require multiple commands to be issued in correct sequence and
there is no kernel-level protection to ensure each action must be completed
before other actions can be performed... The program also attempts to prevent
abortion while issuing commands by catching all termination signals except
SIGKILL - don't kill the indicator by "kill -9" unless absolutely necessary.


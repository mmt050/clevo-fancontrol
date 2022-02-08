Clevo & System76 Fan Controller 
===============================

This was forked from https://github.com/SkyLandTW/clevo-indicator and scrubbed from gtk and graphical dependencies in order
to be usable as a CLI tool and/or systemd service. This further allows me to write a more sophisticated PID-controller thermal algo in higher language (like python)
and only use this to translate to the EmbeddedController by clevo, which the original project already handled.

The built-in curve setting works great with Ryzen 5700U S76 Pangolin (pang11). My need is to have a setpoint of 60deg, and turn off the fans below 50deg. 

The 5700U is a 8-core CPU, and S76 have configured it with generous 25w (with 30w burst) TDP limit on AC power. This allows it to reach some impressive benchmark scores but it also means it needs lots of cooling. My purpose was to have a mostly silent laptop and I was willing to leave some multi-core performance on the table to achieve this. Enter https://github.com/FlyGoat/RyzenAdj/wiki/Renoir-Tuning-Guide#skin-temp-limit, which allows to set target TDP, burst-TDP, and maximum tctl-temp.  


Clevo Fan Control Indicator for Ubuntu
======================================

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

SystemD Service
---------------

You can use a service similar to the below, placed in /etc/systemd/system/

Note the targets, which make sure the 

```shell
[Unit]
Description=Clevo-Fancontrol
After=network.target
StartLimitIntervalSec=0
After=network-target.service
Wants=network-target.service

[Service]
Type=idle
User=root
Group=root
Restart=always
RestartSec=2
User=root
ExecStart=/usr/bin/clevo-fancontrol -1

StandardOutput=syslog
StandardError=syslog
SyslogIdentifier=clevo-fancontrol

[Install]
WantedBy=graphical.target

```

UDEV Rules
----------

The following two rules can be placed in two files in /etc/udev/rules.d/

```shell
SUBSYSTEM=="power_supply",ENV{POWER_SUPPLY_ONLINE}=="1",RUN+="/usr/bin/systemctl start ryzenadj"
```

```shell
SUBSYSTEM=="power_supply",ENV{POWER_SUPPLY_ONLINE}=="0",RUN+="/usr/bin/systemctl start ryzenadj"
```



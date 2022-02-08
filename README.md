Clevo & System76 Fan Controller 
===============================

This was forked from https://github.com/SkyLandTW/clevo-indicator and scrubbed from gtk and graphical dependencies in order
to be usable as a CLI tool and/or systemd service. This further allows me to write a more sophisticated PID-controller thermal algo in higher language (like python)
and only use this to translate to the EmbeddedController by clevo, which the original project already handled.

The built-in curve setting works great with Ryzen 5700U S76 Pangolin (pang11). My need is to have a setpoint of 60deg, and turn off the fans below 50deg. 

Taming the 5700U, 8-core CPU with shitty stock Clevo fan/cooler
===============================================================

The 5700U is a 8-core CPU, and S76 have configured it with generous 25w (with 30w burst) TDP limit on AC power, and 18W limit on battery.

This allows it to reach some impressive benchmark scores (1280/7800 Geekbench) but it also means it needs lots of cooling. Cooling that the stock fan+sink combo can only provide at high RPM.  My purpose was to have a mostly silent laptop and I was willing to trade some multi-core performance to achieve it. 

Enter https://github.com/FlyGoat/RyzenAdj/wiki/Renoir-Tuning-Guide#skin-temp-limit, which allows to set target TDP, burst-TDP, and maximum tctl-temp. With some tinkering I am satisfied to have applied the following:
```shell
/usr/bin/ryzenadj  --slow-limit=9000 --fast-limit=13000 --tctl-temp 80
```

Which gives me a completely silent laptop about 95% of the time, while preserving 100% of stock single-core, and 75-80% of the stock multi-core performance. If you're not willing to leave 25% multi-core performance on the table, you can up the --slow-limit (controlling the sustained performance), and also up the --fast-limit (controlling the burst), and up the tctl-temp to 90 (heat transfer is more efficient the higher the temp. difference). This will take you much closer to 100% mutli-core performance all the while your laptop will be silent when you're reading or coding.


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



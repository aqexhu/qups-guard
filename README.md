# qups-guard
AQEX qUPS uninterruptible power supply - software guard for safe shutdown of the Raspberry PI
![qups_trans_v1](https://user-images.githubusercontent.com/101105892/157266323-7faa3947-2520-4996-8900-2e5b6d1450db.png)

**1. Greeting / purpose**

First of all, we would like to express our deeply honest greeting to all of you, who lay trust in our efforts and use our products! We are working hard day-by-day to keep software and hardware up-to-date and develop the products and ourselves. We are trying to deliver devices and services, which serve your lives and purposes better, than any past ones, taking our environment also in focus. Sustainable, long lifespan, reliable, flexible and easy to use - in a green shape!

**2. Device**

The qUPS uninterruptible power supply was created, because the mission critical endpoints need protection against black- or brownouts and during longer power-cuts, syncing/last communication/safe shutdown must be initiated before everything gets dark.
Our solution is simple in design with high flexibility - voltage levels and GPIOs are configurable.

**4. Installation**

  **4.1 RaspiOS on Raspberry PI binary**

If you are using RaspiOS, you can clone the repository and start the compiled binaries or execute python3 interpreter.

  **4.2 RaspiOS on Raspberry PI compile from source**

Libraries needed:
```
lgpiod-dev
```

You can compile the source files with following options and libraries:
```
/usr/bin/gcc -Wall qups-guard.cpp -o qups-guard -lpthread -lgpiod
```
or
```
/usr/bin/gcc -Wall qups-guard_f.cpp -o qups-guard_f -lpthread -lgpiod
```


**6. Usage**

The manually or automatically started qups-guard will keep track of energy level and power supply. If power fails, syslog will be updated with the ```Power NOK!``` information. If power keeps failing and the supercapacitor voltage level drops below high voltage limit, the LED changes from green to amber. After depleting below low voltage limit, red LED also starts up and syslog is updated with ```UPS level LOW - initiating shutdown seqence.```
On power resume, the capacitor gets filled above high voltage limit and the computer gets powered up.

**7. Autostart**

You can utilize systemd service model. Therefor the service file is located in the repository "systemd" folder. Be careful, that it is parametrized for python script, user "pi" and with "100" DIP switch setting. Any other combinations are free to use.

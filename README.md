# AQEX qUPS smart supercapacitor uninterruptible power supply 
## Software guard for safe shutdown of the Raspberry PI
![qups_trans_v1](https://github.com/aqexhu/qups-guard/blob/main/images/qups_trans_smaller_v1.png)
![qups_trans_v2](https://github.com/aqexhu/qups-guard/blob/main/images/qups_trans2_smaller_v1.png)

![license](https://img.shields.io/github/license/aqexhu/qups-guard)
![last_commit](https://img.shields.io/github/last-commit/aqexhu/qups-guard)
![opsys](https://img.shields.io/github/checks/node-formidable/node-formidable/master/ubuntu?label=linux)

## 1. Greeting / purpose

First of all, we would like to express our deeply honest greeting to all of you, who lay trust in our efforts and use our products! We are working hard day-by-day to keep software and hardware up-to-date and develop the products and ourselves. We are trying to deliver devices and services, which serve your lives and purposes better, than any past ones, taking our environment also in focus. Sustainable, long lifespan, reliable, flexible and easy to use - in a green shape!

## 2. Device

The qUPS uninterruptible power supply was created, because the mission critical endpoints need protection against black- or brownouts and during longer power-cuts, syncing/last communication/safe shutdown must be initiated before everything gets dark.
Our solution is simple in design with high flexibility - voltage levels and GPIOs are configurable.

## 3. Installation

### 3.1 RaspiOS on Raspberry PI binary

If you are using RaspiOS, you can clone the repository and start the compiled binaries or execute python3 interpreter. The binaries and the source codes for the c++ version cover two different approaches: qups-guard is a 1 sec polling evaluation for the GPIO, qups-guard_f is an asynchronous event driven version.

### 3.2 RaspiOS on Raspberry PI compile from source

Libraries needed:
```
libgpiod-dev
```

You can compile the source files with following options and libraries:
```
/usr/bin/gcc -Wall qups-guard.cpp -o qups-guard -lpthread -lgpiod
```
or
```
/usr/bin/gcc -Wall qups-guard_f.cpp -o qups-guard_f -lpthread -lgpiod
```


## 4. Usage

The manually or automatically started qups-guard will keep track of energy level and power supply. 
Manual start from command line (assumed repo cloned to pi home dir and DIP is set to 100):
```
/home/pi/qups-guard/qups-guard 100
```
If power fails, syslog will be updated with the ```Power NOK!``` information. If power keeps failing and the supercapacitor voltage level drops below high voltage limit, the LED changes from **green** to **amber**. After depleting below low voltage limit, **red** LED also starts up and syslog is updated with ```UPS level LOW - initiating shutdown seqence.```
On power resume, the capacitor gets filled above high voltage limit and the computer gets powered up by the qups.

## 5. Autostart

You can utilize systemd service model. Therefor the service file is located in the repository "systemd" folder. Be careful, that it is parametrized for python script, user "pi" and with "100" DIP switch setting. Any other combinations are free to use.

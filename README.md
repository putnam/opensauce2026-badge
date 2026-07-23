# custom opensauce 2026 badge firmware

this firmware is a replacement for the firmware included on the 2026 open sauce badge.

the badge uses a custom board based on the Arduino MKR Zero.

### how this came to be

during open sauce 2026, we were lucky enough to get an "eyebrow module" for our badges on saturday, and the badge designer (caleb) mentioned he hoped someone would hack it. so, yeah, had to do that. later that night i dumped the firmware and looked around in ghidra to identify what was going on. the original firmware is effectively the "bop it" game. you react to prompts by triggering inputs and keep going until you fail.

since there are some natural features for creating a menu of 5 slots (plus secrets), we figured it would be cool to make five games/apps for the badge. i also added an "attract mode" for people to have a little light show on their badge while they walk around.

with the help of gerard hudson, we threw this together in about 3 hours after we got back late on saturday. it was a necessary evil to use some AI tools, so credit is due to GLM 5.2 for making something functional. i definitely had to go back and clean some things up but it blows my mind how fast you can work in a pinch.

### included modes

attract mode: six different light animations. tap the robot's teeth pads to choose between them. includes the LEDs on SAO modules if you have them attached (via SAO pin GPIO2 -- see "notes on SAOs" below)

slot 1 - bop it 2.0: like the original bop-it, but with advancing difficulty tiers and some music cues.

slot 2 - simon: a basic simon game that uses the middle 4 teeth on the bot's mouth. your best score is tracked and saved across reboots.

slot 3 - balloon: an advanced lung function tester. blow into the microphone to inflate the balloon until it pops.

slot 4 - robotheremin: a simple musical instrument that uses accelerometer data to make your bot sing.

slot 5 - drum machine: a (very) simple 3-channel drum machine. middle 4 pads are steps, the button switches between the 3 channels for the instruments (kick/snare/hat).

in addition to the above modes, there is a special event that occurs if you brush the bots teeth just right a few times. dare you figure it out before you read the code?

### how to flash

1. install arduino IDE if you don't already have it
2. open the .ino file in arduino IDE
3. plug your badge in via usb. if you do not see it show up, try quickly double-pressing the little button in between the eyes to go to bootloader mode.
4. open the library manager on the left and search/download `Adafruit_LIS3DH`, `Adafruit_FreeTouch`, and `FlashStorage`.
5. go to tools->boards->board manager... and search for "SAMD boards" -- install the "Arduino SAMD boards" package
6. select your board and serial/usb port in the dropdown at the top
7. sketch->upload (or the right arrow in the IDE on the top left). this will compile and upload the sketch to your badge and restart it
8. that's it, you can unplug now.

you can power the badge with the battery on the back or via USB. you can also attach a battery holder to the 3v3 override pads on the bottom rear. the possibilities are endless!

### notes on SAOs

at open sauce 2026 there were a few different SAO modules handed out (a robot arm, a cowboy hat, the robot cube head) that all are basic LED modules. on a fully-built badge, these modules will light up solid (3v3 is tied directly to the LED circuit). but on the back of the SAO is a set of three pads that say "LED select". if you carefully cut the bridge between the first two pads, then solder together the right two pads, you'll bridge GPIO2 to the LED line so it can be driven by the firmware instead. now that SAO will participate in attract modes.

third-party SAOs are untested right now. this firmware just drives the GPIO2 pin when in attract mode as if it's a light. the standard includes pins for an i2c bus which the board does implement, but i don't have any sample SAOs to integrate into the firmware at this time.

### todo / ideas

1. add quantized scale options to the robotheremin
2. maybe do an ocarina mode where you blow into the mic and use the pads for notes (also with quantized scales, and change scale with button presses)
3. a balance game?
4. persistence of vision tricks with the 5 mouth leds?
5. probably some kind of power saving strategy would be good
6. a call-and-response rhythm game?

### license

there isn't one. we don't care. do whatever!

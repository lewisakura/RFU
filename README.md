# Retirement Notice
Hi all. I think it's time for this project to finally be retired. There are a few of reasons for this:

- For the most part, the gripes I had with the original Roblox FPS Unlocker are resolved, so I myself will move over to using it.
- Due to the upstream rewrites, I can no longer keep up with manually merging in the changes without bugfree guarantees.
- I've realised I'm way out of my depth. I'm not a good reverse engineer by any means.
- Unfortunately, this code is now horribly spaghetti. This is mainly my own downfall; I didn't want to merge in certain changes but wanted to
merge in the fixes. This had led to a very terrible codebase that no longer lines up with the original, so I can't simply merge in changes
anymore. If I could, I'd rewrite this in my own way, but I don't have the motive when there's a tool that's better maintained.

RFU has overstayed its welcome, and it's time for it to go. With the advent of the rewritten Roblox FPS Unlocker 5, and the reasonings above,
it's time to say goodbye.

Thank you for using RFU. I hope I can write some more tools for you soon. And most of all, thank you to Axstin for maintaining one of the
most important tools for the Roblox platform.

And of course, in case you need it, go and grab the original here: https://github.com/axstin/rbxfpsunlocker

I've pushed one last release with this same text so that you get prompted to download it if you are still using it.

<p align="center">
  <img src="https://raw.githubusercontent.com/LewisTehMinerz/RFU/master/repository-banner.png">
</p>
<p align="center">
  A utility to remove the mandatory FPS cap put on by Roblox.
</p>

## Why RFU rather than the *actual* Roblox FPS Unlocker?
**R**oblox **F**PS **U**nlocker (RFU) is a fork of the original [Roblox FPS Unlocker](https://github.com/axstin/rbxfpsunlocker). It removes
the dependency on Blackbone (which wasn't even used in most builds of it), modernizes the code, and also removes dead code. This makes it
slightly faster, smaller, and makes it compilable on newer versions of the C++ compiler. With the removal of Blackbone, antivirus false
positives should be less common than they were before.

# FAQ
## I'm getting an error saying that "VCRUNTIME140.DLL" is missing! Why?
You need to install the Visual C++ redistributable. Here are the links to the correct ones:
* [64-bit](https://aka.ms/vs/16/release/vc_redist.x64.exe)
* [32-bit](https://aka.ms/vs/16/release/vc_redist.x86.exe)

You can install both, however you only actually need to install the one for the version of RFU you are running.

## How can I see my FPS?
Press `SHIFT + F5` in game. In Studio, go to `View -> Stats -> Summary`.

## I used this unlocker and my framerate is the same or below 60. Why?
RFU is not a *booster*, but an *unlocker*. It will not give you any more performance than your computer can give you.

With that being said, if you are **absolutely sure** that your PC is powerful enough to run Roblox at >60 FPS, try using
[this testing game](https://www.roblox.com/games/5885482504/-) which will most certainly bring you above 60 FPS. If it doesn't (so the game
reports "Not working correctly."), [file an issue](https://github.com/LewisTehMinerz/RFU/issues).

## Can I set a custom framerate cap?
Custom framerate limits can be set by changing the `FPSCap` value inside the settings file located in the same folder as the application
and reloading settings (`RFU -> Load Settings`). Changing the cap with RFU's menu will reset/overwrite this value.

## Does this work for Mac?
RFU will not work for Mac. I do not have plans to add this to RFU because, much like the original developer, I do not have access to a Mac
neither do I have the experience to do what RFU does on Mac.

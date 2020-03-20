<p align="center">
  <img src="https://raw.githubusercontent.com/LewisTehMinerz/RFU/master/repository-banner.png">
</p>
<p align="center">
  A rewrite of <a href="https://github.com/axstin/rbxfpsunlocker">Roblox FPS Unlocker</a>.
</p>

**R**oblox **F**PS **U**nlocker (RFU) is a utility to remove the mandatory FPS cap put on by Roblox.

## Why RFU rather than the *actual* Roblox FPS Unlocker?
RFU is a rewrite of the original Roblox FPS Unlocker. It removes the dependency on Blackbone (which wasn't even used in most builds of it),
modernizes the code, and also removes dead code. This makes it slightly faster, smaller, and makes it compilable on newer versions of the
C++ compiler. With the removal of Blackbone, antivirus false positives should be less common than they were before.

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

This being said, if you are **absolutely sure** that your PC is powerful enough to run Roblox at >60 FPS,
[file an issue](https://github.com/LewisTehMinerz/RFU/issues).
## Can I set a custom framerate cap?
Custom framerate limits can be set by changing the `FPSCap` value inside the settings file located in the same folder as the application
and reloading settings (`RFU -> Load Settings`). Changing the cap with RFU's menu will reset/overwrite this value.
## Does this work for Mac?
RFU will not work for Mac. I do not have plans to add this to RFU because, much like the original developer, I do not have access to a Mac
neither do I have the experience to do what RFU does on Mac.

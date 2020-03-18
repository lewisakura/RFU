<p align="center">
  <img src="https://raw.githubusercontent.com/LewisTehMinerz/RFU/master/repository-header.png">
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
# YoYo Loader Vita

<p align="center"><img src="./screenshots/game.png"></p>

YoYo Loader is a loader for libyoyo.so, the official GameMaker Studio Runner application for Android, for the *PS Vita*.

YoYo Loader works by loading such ARMv7 executable in memory, resolving its imports with native functions and patching it in order to properly run.

This enables to run potentially any game made with GameMaker Studio.

## What is supported

| Type of Game         | Compatibility                                                            |
| :------------------- |:------------------------------------------------------------------------ |
| Android Bytecode     | ![#c5f015](https://via.placeholder.com/15/c5f015/000000?text=+) `Native` |
| Android YYC          | ![#c5f015](https://via.placeholder.com/15/c5f015/000000?text=+) `Native` |
| PC/Console Bytecode  | ![#1589f0](https://via.placeholder.com/15/1589f0/000000?text=+) `Yes`    |
| PC/Console YYC       | ![#f03c15](https://via.placeholder.com/15/f03c15/000000?text=+) `No`     |

For PC/Console exported games, you will need to perform an assets swap with a blank Android exported project with a Game Maker Studio version similar or equal of the one of the game you want to attempt to run.

Note that patches to the bytecode may still be required in order to fix resolution, inputs or performances issues. Any game reported as ![#c5f015](https://via.placeholder.com/15/c5f015/000000?text=+) `Native`, instead, will work with simple drag'n'drop of the apk.

A comprehensive Compatibility List can be found here: https://yoyo.rinnegatamante.it. You can contribute to it by opening an Issue here: https://github.com/Rinnegatamante/YoYo-Loader-Vita-Compatibility/issues.

## Setup Instructions (For End Users)

In order to properly install the loader, you'll have to follow these steps precisely:

- Install [kubridge](https://github.com/TheOfficialFloW/kubridge/releases/) and [FdFix](https://github.com/TheOfficialFloW/FdFix/releases/) by copying `kubridge.skprx` and `fd_fix.skprx` to your taiHEN plugins folder (usually `ux0:tai`) and adding two entries to your `config.txt` under `*KERNEL`:
  
```
  *KERNEL
  ux0:tai/kubridge.skprx
  ux0:tai/fd_fix.skprx
```

**Note** Don't install fd_fix.skprx if you're using rePatch plugin

- Install `libshacccg.suprx`, if you don't have it already, by following [this guide](https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx).
- Games must be placed inside `ux0:data/gms/GAMENAME`, where `GAMENAME` must refer to the name of the game, in form of an apk file renamed as `game.apk`. [You can get all the required files directly from your phone](https://stackoverflow.com/questions/11012976/how-do-i-get-the-apk-of-an-installed-app-without-root-access).
- Inside the loader, you can also find a feature, by pressing Triangle in the game selector screen, to optimize the apk. Such feature will optimize compression of the files inside the apk to not cause stuttering and loading issues and will also remove any unnecessary file thus reducing the final apk size.

## Build Instructions (For Developers)

In order to build the loader, you'll need a [vitasdk](https://github.com/vitasdk) build fully compiled with softfp usage.  
You can find a precompiled version here: https://github.com/vitasdk/buildscripts/releases.
Additionally, you'll need some libraries normally included in vdpm compiled with softfp. You can get most of the required ones here: https://github.com/Rinnegatamante/vitasdk-packages-softfp/releases.
Finally, you'll need to recompile the few missing ones on your own:

- [vitaGL](https://github.com/Rinnegatamante/vitaGL)

    ````bash
    make SOFTFP_ABI=1 NO_DEBUG=1 SAMPLER_UNIFORMS=1 install
    ````

After all these requirements are met, you can compile the loader with the following commands:

```bash
mkdir build && cd build
cmake .. && make
```

## Credits

- TheFloW for the original .so loader.
- JohnnyonFlame for GMSLoader used as reference for some implementations and for generic advices.
- Once13One for the Livearea assets and the README showcase image.

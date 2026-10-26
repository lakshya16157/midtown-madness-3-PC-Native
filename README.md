# Midtown Madness 3 PC Native

Play the Xbox racing game **Midtown Madness 3** natively on your Windows PC: your screen's
resolution and widescreen, 60 fps, controller or keyboard, and optional mods. You need your
own copy of the Xbox game.

**[Download MM3-Setup.exe](https://github.com/lakshya16157/midtown-madness-3-PC-Native/releases/latest)**
(under **Assets** on the latest release).

## Install and play

1. **Run MM3-Setup.exe.** Windows may say "Windows protected your PC" because the file is
   not code-signed: choose **More info**, then **Run anyway**.
2. **Pick your game image**: a disc image of your own Midtown Madness 3 Xbox disc (`.iso`
   or `.xiso`). The Europe/Australia and USA releases both work.
3. **Choose options** (you can change them later by running Setup again):
   - **Add extra cars**: nine community cars (see [credits](#extra-car-credits)).
   - **Add game improvements** (on by default): sharper image defaults. Untick it to start
     with the plain Xbox picture settings.
   - **Official DLC** (optional): point Setup at a DLC ZIP or folder you are entitled to use.
4. Click **Install**, then **Play now**. Later, start the game with
   **Play Midtown Madness 3.cmd** in the install folder.

You need Windows 10 or 11 (64-bit), a Direct3D 11 graphics card and about 6 GB of free
disk space. The first start shows the intro movies for about 3½ minutes; the game has not
frozen.

## Original or Improved

The launcher opens before the game. Its **Mods** tab starts with a choice:

- **Original game**: Midtown Madness 3 as it was on the Xbox, running natively at your
  resolution with the graphics and audio upgrades, and nothing else.
- **Improved game** (default): the same, plus the mods below, each with its own switch.

Both include widescreen, anti-aliasing and sharper textures, the accurate Xbox sound
processor, controller vibration, keyboard controls, System Link and many fixes. These
include working post-race menus, console-accurate car damage at 60 fps, visible rain, and
Spanish voices in Spanish.

**Mods:**

| Visuals | Gameplay |
|---|---|
| Longer view distance | 20 extra circuit races (Washington DC and Paris) |
| Car headlight beams and glowing tail lights | Extra cars ([Car Workshop](DEVELOPERS.md#added-cars-and-editing-stats)) |
| Better street lamps, tunnels included | Names and pictures for the DLC races |
| Better night lighting | Trees fall over when hit |
| Lamp flares hidden behind buildings | Slipstream drafting |
| Route line on the map | Rollover assist |
| In-game Graphics Settings menu | AI opponents in DLC and extra cars |
| Triangle exhaust (off by default) | |

## Controls

- **Controller:** Xbox and PlayStation (DualSense) controllers work, with vibration. Up to
  four players.
- **Keyboard:**
  - Driving: arrows drive, **Space** is the handbrake, **C** changes the camera.
  - Menus: **Z** confirms, **X** or **Escape** goes back, **Enter** pauses.
  - Hold **Ctrl** to enlarge the map.
- **Change bindings** in the launcher's **Controls** tab.
- **Graphics:** use the launcher's Settings tab, Graphics Settings in the main menu, or
  **F10** while playing.

## Multiplayer (System Link)

Every player needs the same version and their own game files.

1. In the launcher's **Settings**, tick **Enable System Link**.
2. Enter the other PCs as **Peers** (for example `192.168.1.51:3075`).
3. In the game, choose **Multiplayer > System Link**.

Players on different networks can join through a private VPN. Allow the game's UDP port
(3075) through Windows Firewall. The full guide, for the internet and up to eight
players, is in [docs/MULTIPLAYER.md](docs/MULTIPLAYER.md).

## Problems?

- **Which version do I have?** The launcher's title shows it. Update to the latest release
  first.
- **A race doesn't finish:** in circuit races, checkpoints count only in order. If you miss
  one, the game tells you which, and you need to go back for it.
- **A save from a real Xbox says it is damaged:** run
  `python tools\saves\resign_save.py <its UDATA folder> <new folder>`, then copy the new
  `4d53002a` folder into `game_files\UDATA`.
- **Something else:** the game writes `logs\last-run.log` in the install folder.
  [Open an issue](https://github.com/lakshya16157/midtown-madness-3-PC-Native/issues) and
  attach it.

Your saves are kept when you run Setup again. To uninstall, delete the install folder.

## Support me

If you enjoy this project, you can support me on Revolut:
**[revolut.me/lakshyasingh](https://revolut.me/lakshyasingh)**

## Extra car credits

The nine extra cars were made by members of the Midtown Madness modding community and
converted for Midtown Madness 3 by this project's tools. Thank you to every author.

| Car | Converted for Midtown Madness 2 by | Original model | Download page |
|---|---|---|---|
| Panoz GTR-1 | **Aaro4130** | ISI (original rFactor model); skins by Thunder-ice and Alexi99 | [Panoz GTR-1](https://www.mm2x.com/download-1336-panoz-gtr-1#1336) |
| McLaren F1 GTR (1995) | **Tsutarja495** | Firemint (Real Racing 2 model and textures) | [1995 McLaren F1 GTR](https://www.mm2x.com/download-2214-1995-mclaren-f1-gtr#2214) |
| Ferrari F2007 (Formula 1) | **Kunal** (Kunal2045) | Image Space Incorporated (original rFactor model); tested by Franch88, Riva and Sajmon14 | [Ferrari F2007 F1](https://www.mm2x.com/download-1220-ferrari-f2007-f1#1220) |
| Ford GT (2017) | **mm2mm1fan** | Firemonkeys (Real Racing 3 model), extracted by Eclipse 72rus | [2017 Ford GT](https://www.mm2x.com/download-2049-2017-ford-gt#2049) |
| McLaren Senna (2019) | **Tsutarja495** | Firemonkeys (Real Racing 3 model and textures) | [McLaren Senna 2-Pack](https://www.mm2x.com/download-2221-mclaren-senna-2-pack#2221) |
| McLaren Senna GTR (2020) | **Tsutarja495** | Firemonkeys (Real Racing 3 model and textures) | [McLaren Senna 2-Pack](https://www.mm2x.com/download-2221-mclaren-senna-2-pack#2221) |
| Ferrari FXX K (2015) | **Matyii** (Matyasd at MMArchive) | Firemonkeys Studios (Real Racing 3 model) | [2015 Ferrari FXX K](https://www.mm2x.com/download-1806-2015-ferrari-fxx-k#1806) |
| Koenigsegg Regera (2015) | **Matyii** (Matyasd at MMArchive) | Firemonkeys Studios (Real Racing 3 model) | [2015 Koenigsegg Regera](https://www.mm2x.com/download-1715-2015-koenigsegg-regera#1715) |
| Lamborghini Aventador LP700-4 | **BUGATTIMAN253MPH** | Firemonkeys (Real Racing 3 body); wheels, brake discs and calipers by Gameloft (GT Racing 2) | [Lamborghini Aventador LP700-4](https://www.mm2x.com/download-1465-lamborghini-aventador-lp700-4#1465) |

Several authors ask that their cars are not modified or reposted without permission. Each
pack's original readme is saved next to the game in `Extra cars - credits.txt`.

## Ownership, disc images and DLC

You must own the original Xbox game. Make the disc image from your own disc where your
local law allows it, and use only DLC you are entitled to. Setup does not include or
download the game or its DLC. The full notes, with the image and DLC sources the
maintainer tested, are in [DEVELOPERS.md](DEVELOPERS.md#ownership-disc-images-and-dlc).

## Credits and developers

Built on [mm3-recomp by Matt Graham (MattSG)](https://github.com/MattSG/mm3-recomp) and the
[xboxrecomp toolkit](https://github.com/MattSG/xboxrecomp). Maintained by
[lakshya16157](https://github.com/lakshya16157).

Building from source, tools and technical notes are in [DEVELOPERS.md](DEVELOPERS.md).

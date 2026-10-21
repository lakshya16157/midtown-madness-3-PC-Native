# Midtown Madness 3 PC Native

Play the Xbox racing game **Midtown Madness 3** on your Windows PC: widescreen, sharper
graphics, keyboard controls, and optionally nine extra cars. You must already own the original
Xbox game and supply a lawfully obtained disc image. No building, no programming.

## Install and play

1. **Download [MM3-Setup.exe](https://github.com/lakshya16157/midtown-madness-3-PC-Native/releases/latest)**
   from the latest release (under **Assets**).
2. **Run it.** Windows may say "Windows protected your PC" because the file is not
   code-signed. Choose **More info**, then **Run anyway**.
3. **Pick your game image:** your lawfully obtained Midtown Madness 3 Xbox disc image
   (`.iso` or `.xiso`). The Europe/Australia (English, French, German, Spanish, Italian)
   and USA releases both work, including full redump images and bare XISOs.
4. **Choose your options:**
   - **Add extra cars** (off by default). Adds nine community cars: Panoz GTR-1,
     McLaren F1 GTR, Ferrari F2007 (Formula 1), Ford GT, McLaren Senna, McLaren Senna GTR,
     Ferrari FXX K, Koenigsegg Regera and Lamborghini Aventador.
   - **Add game improvements** (on by default). A sharper image (2x resolution, 4x
     anti-aliasing, 16x texture filtering), longer draw distance, widescreen that fits your
     window and better night lighting. Untick it for the original Xbox look.
   - **Official Xbox Live DLC** (optional): select a local DLC ZIP or extracted folder
     that you are entitled to use. Setup imports the content packs and reports missing
     packs. The disc and the **Add extra cars** option do not include the official DLC.
5. Click **Install**, then **Play now**. Later, start the game with
   **Play Midtown Madness 3.cmd** in the install folder (by default a `Midtown Madness 3`
   folder next to the installer).

You need Windows 10 or 11 (64-bit), a Direct3D 11 graphics card, about 6 GB of free disk
space, and your own game image (it is not included).

## Play online (System Link)

**Setup 1.2 and later include System Link.** Each player needs the same build and their own
game files. Use **Multiplayer > System Link** in the game.

Configure the connection before starting the game:

1. In the launcher's **Settings**, tick **Enable System Link**.
2. Set **Local UDP port** to `3075` on each PC.
3. In **Peers (host:port, comma separated)**, enter the other PC's reachable IPv4
   address and port, for example `192.168.1.51:3075`. Configure both PCs: each needs
   the other's address. Use the PC's LAN, VPN or public address as appropriate,
   rather than the emulated guest address printed in the game log.
4. Allow the game's chosen UDP port through Windows Firewall on the network you
   are using. See Microsoft's [Windows Firewall rule documentation](https://learn.microsoft.com/en-us/powershell/module/netsecurity/new-netfirewallrule).

For **two PCs on the same LAN**, find each PC's IPv4 address in Windows network
settings or with `ipconfig`. Both PCs must be able to reach each other; guest Wi-Fi
or wireless client isolation can prevent this. You can also launch from PowerShell 7
in the repository folder, replacing these example addresses:

```powershell
# PC A: its LAN address is 192.168.1.50; PC B is 192.168.1.51.
pwsh scripts\play.ps1 -Instance Online -Network -NetworkPort 3075 -NetPeers '192.168.1.51:3075'

# PC B: point back to PC A.
pwsh scripts\play.ps1 -Instance Online -Network -NetworkPort 3075 -NetPeers '192.168.1.50:3075'
```

Separate PCs can both use port `3075`. Multiple copies on **one PC** need different
ports and different `-Instance` names; see the [two-instance example](#pc-runtime-upgrades).
Named instances start with separate profiles and save preferences under
`%LOCALAPPDATA%\MM3Recomp\instances\<name>`, while sharing the installed game data.
The command-line network options override saved launcher settings.

For **internet play**, choose one of these connection methods, then follow the
same in-game steps below:

- **Private VPN:** connect all players to a private VPN that allows UDP traffic
  between their PCs. Enter each other's VPN IPv4 address and game port in **Peers**,
  and allow that traffic through the VPN's access rules and Windows Firewall.
- **Direct UDP:** on each player's router, forward a chosen external **UDP** port
  to that PC's LAN IPv4 address and game UDP port. Exchange public IPv4 addresses
  and external ports, and use those endpoints in **Peers**. A private LAN address
  such as `192.168.1.50` is not reachable across the internet. If an ISP uses CGNAT,
  or another router prevents inbound forwarding, use a private VPN with reachable
  peer addresses instead.

Every player needs mutual UDP reachability. This backend provides no public lobby
service, relay, automatic port forwarding or NAT hole punching. Its UDP tunnel
has no authentication or encryption; connect to trusted peers or use a private VPN.

For **three to eight players**, configure each PC's comma-separated **Peers** list
with **every other player**. For example, A lists B and C, B lists A and C, and C
lists A and B. The host does not relay the other players' tunnel traffic. Peer
entries accept IPv4 addresses or hostnames that resolve to IPv4, followed by a
port; up to eight peer entries can be stored. Use commas without spaces, for
example `192.168.1.51:3075,192.168.1.52:3075`.

To create and join a game:

1. Choose a profile, then **Multiplayer > System Link**.
2. **Host:** press **Y — Create New Game**, select **Done** and press **A** to accept
   the name, then choose the session settings and vehicle. Start with **Cruise**
   and a base-game car when testing the connection.
3. **Other players:** select the host's session and press **A** to join.
4. **Clients:** press **X — Ready** in the lobby.
5. **Host:** once the players are ready, press **X — Start Game**.

These are Xbox button prompts. With the default keyboard bindings, **Y = S**,
**A = Z**, and **X = A**; bindings changed in the launcher's **Controls** take
precedence. Xbox Live sign-in and Insignia service support have not been validated.

If the host does not appear, check that System Link is enabled on every PC,
the peer addresses and ports are reciprocal, and firewall/VPN rules allow the
chosen UDP port. `127.0.0.1` works only for copies on the same PC. If the port is
already in use, choose a different local port and update the other players' peer
lists. If joining fails, first retry with the same build, game content and base-game
cars on all PCs. Launch logs are under `logs\`; include them when reporting a problem.

**Tested:** two native copies on one Windows PC discovered a session, joined,
propagated Ready, started a Paris Cruise and exchanged moving vehicle state in both
directions. Separate physical PCs, VPNs and public-internet latency/loss conditions
have not yet been tested.

## Ownership, disc images and DLC

**Already owning the original Xbox disc is a requirement of this project.** The preferred
route is to make an image from your own disc using compatible Xbox dumping hardware and
tools, where local law permits, and select that image in Setup. Keep your original disc.
Supply DLC from your own backup or another source that you have permission to use.

Owning a disc does **not** automatically authorize downloading somebody else's copy,
redistributing the game or DLC, or bypassing copy protection. An old game or a file hosted
on an archive is not automatically public domain or authorized. Backup and copy-protection
rules depend on your country and licence. See the
[U.S. Copyright Office's software backup FAQ](https://www.copyright.gov/help/faq/faq-digital.html)
and, for France, [Code de la propriété intellectuelle, Article L122-6-1](https://www.legifrance.gouv.fr/codes/section_lc/LEGITEXT000006069414/LEGISCTA000006161637/).
This project cannot grant rights to Microsoft's game, DLC or community car assets.

The following third-party links are supplied at the maintainer's request for identifying
the tested image and the optional DLC package. Their availability is not proof of a licence;
use downloads only when the rights holder permits your use:

- [Europe/Australia multilingual Xbox image — Internet Archive](https://archive.org/details/midtown-madness-3-europe-australia-en-fr-de-es-it)
  (open **Download options** on the item page).
- [Offline Xbox Live DLC installers — Digiex](https://digiex.net/threads/xbox-offline-xbox-live-downloadable-content-dlc-installers.675/),
  including the [Midtown Madness 3 DLC ZIP](https://download.digiex.net/Consoles/Xbox/DLC/MidtownMadness3DLC.zip).
  Setup reads the data packs from this ZIP or an extracted copy; do not run its Xbox
  installer on Windows. Console title-update executables are excluded because this
  native build requires the supported retail code.
- [Midtown Madness 3 DLC installers — Xbox-Scene](https://www.xbox-scene.info/files/file/167-midtown-madness-3/)
  hosts the same community installer pack (four car packs with 14 cars and two track
  packs, credited to NemVitzh and InsaneNutter). Setup was tested with the Digiex ZIP.

DLC is optional and separate from the nine community cars. Setup imports local packs
only when you select a source; it does not download or bundle the game or official DLC.
It preserves existing DLC and saves. A complete official set contains six packs: four
car packs (14 cars total) and two track packs. Run Setup again with your DLC source to
add missing packs. Installed data lives under `game_files\TDATA\4d53002a\$C`.

**Good to know**

- **Launcher:** Settings, Controls and Mods open before the game. Change graphics,
  field of view, keyboard/mouse bindings and controller buttons there, or toggle
  individual enhancements. The Setting Guide explains the selected value, its
  effect and tradeoffs as you hover, focus or change a control. Controls shows
  the status of up to four XInput pads;
  select a binding and press a key or controller button to change it. Any pad
  button or trigger can stand in for any Xbox button (for example **LB** for
  **White**, the headlight switch), for all four pads. Settings
  are saved for the next launch. `scripts\play.ps1 -NoLauncher` skips the window.
- **The first start takes about 3 1/2 minutes** before the menu appears while the intro
  movies play. The game has not frozen.
- **Keyboard:** arrows drive and navigate menus, **Space** is the handbrake, **C** changes
  the camera, **Z** confirms, **X** or **Escape** goes back, **Enter** pauses, hold **Ctrl**
  to enlarge the race map, **Alt+F4** quits. Pick **Graphics Settings** in the main menu
  (between Options and Profile), or press **F10** while playing, to change resolution,
  fullscreen, FPS and quality.
- **Changing your mind:** run the installer again with different options. Your saved games
  are kept. They live in the install folder under `game_files\UDATA`. To uninstall, delete
  the folder.
- **Something wrong?** The game writes `logs\last-run.log` in the install folder. Please
  [open an issue](https://github.com/lakshya16157/midtown-madness-3-PC-Native/issues)
  and attach it.
- **Extra car credits:** see [Extra car credits](#extra-car-credits) below. They are also
  saved next to the game as `Extra cars - credits.txt`.

## Extra car credits

The nine extra cars were made by members of the Midtown Madness modding community. Thank
you to every one of them. Each car was converted for Midtown Madness 2 by the person named
in the table, from a model made by the studio named, and then converted again for Midtown
Madness 3 by this project's tools. The links go to each author's original download page on
[MM2X](https://www.mm2x.com/).

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

Please respect the authors' wishes. Several ask that their cars are not modified or
reposted without their permission, and each pack's original readme (including credits for
the sounds in the original packs) is saved unchanged inside `Extra cars - credits.txt`.
In the game these cars keep Midtown Madness 3's own engine sounds, interiors and collision.

Developers: everything below the line is about building and changing the project.

---

## For developers and contributors

The Windows installer above is built from [installer/](installer/README.md), which packs
the native build, the disc extractor and the optional car pack into one `MM3-Setup.exe`.
The rest of this document covers building from source.

**Midtown Madness 3 PC Native** brings the Xbox arcade racing game to Windows
10 and Windows 11 x64 through static recompilation. This native MM3 PC port
uses the [xboxrecomp](tools/xboxrecomp) runtime and a Direct3D 11 renderer.

An improved and modded PC version with keyboard controls, widescreen graphics,
custom-car tools, gameplay repairs and performance work. Build it from your own
supported game disc with the instructions below. Recent quality-of-life updates
add a selectable transparent road minimap in the bottom-left corner, cleaner
logs and safer crash reports.

**Choose Graphics Settings in the main menu**, between Options and Profile.
Adjust resolution, fullscreen, maximum FPS, VSync and render quality, then choose **Apply**. Your
choices are saved, and settings that need a restart are clearly marked.
F10 also opens the desktop settings panel while playing.

**Credits:** This project builds on the original
[mm3-recomp by Matt Graham (MattSG)](https://github.com/MattSG/mm3-recomp) and the
[xboxrecomp toolkit](https://github.com/MattSG/xboxrecomp). Credit for that foundation
belongs to the original author and upstream contributors.

This fork is maintained by [lakshya16157](https://github.com/lakshya16157), with
assistance from OpenAI Codex on performance and runtime improvements, gameplay
fixes, and the custom-car tools.

## Project keywords

Terms that describe this project and help players find the right repository:

- **Game and project:** Midtown Madness 3, Midtown Madness 3 PC Native,
  Midtown Madness 3 on PC, Midtown Madness 3 PC port, Midtown Madness 3 for
  Windows, MM3 PC, MM3 native, MM3 recompilation, mm3-recomp.
- **Platform and technology:** Xbox to PC, native PC gaming, Windows 10,
  Windows 11, x64, x86-64, static recompilation, Xbox recompilation,
  xboxrecomp, Direct3D 11, D3D11.
- **Mods and improvements:** Midtown Madness 3 mods, MM3 car mods, custom cars,
  car workshop, car model conversion, keyboard controls, widescreen,
  graphics fixes, gameplay bug fixes, performance improvements,
  shader caching, frame pacing, improved draw distance.
- **Gameplay:** arcade racing, driving game, Paris, Washington DC,
  free-roam driving, Blitz races, Checkpoint races.

## How this fork was developed

The maintainer built and tested this fork from a Midtown Madness 3 `.iso`/XISO,
using the [xemu Xbox emulator](https://xemu.app/) to run the original Xbox game
as a reference during development. The native build extracts the game files
from a supported disc image and statically recompiles its Xbox CPU code into
a Windows executable; it then runs on the xboxrecomp runtime with Direct3D 11.
Launching that native executable does not require xemu.

Bring your own game disc or supported image. Game data, disc images, xemu BIOS
files and emulator hard disks are not distributed in this repository. The
[disc notes below](#which-disc) explain accepted native-build images; xemu's
reference workflow uses a bare XDVDFS XISO rather than a two-part redump image.

## Improvements in this fork

- **Visuals:** fixed refreshing of streamed textures and CPU-written swizzled
  surfaces, and prevented stale vertex programs/constants after failed GPU
  uploads. Building-detail fades scale with extended draw distance, and the car
  converter builds closed body and animated-wheel shadow proxies. Widescreen
  pause menus keep button caps, labels and controller prompts joined in one layout.
  Race maps and their checkpoint icons share one placement while expanding or turning.
  Blue guidance follows the cities' road curves and junctions, and updates
  the path to the next checkpoint or selected Undercover objective as the car moves.
  Floating mission and opponent names retain their projected positions even
  when their font texture is also used by the HUD.
  The improved minimap draws gray roads on a transparent, tilted plane.
  Ctrl expands it into a flat overview, retaining checkpoints around the car
  throughout the transition. Narrow streets have a wider visibility floor
  so small Washington DC roads remain readable after perspective scaling.
  Connected street segments share rounded joins at bends, right-angle turns
  and closed-loop seams, removing sharp bevels and small gaps between roads.
  Daylight retains its authored colors. Night ambient, diffuse and backlight
  use 25% of their authored strength, with no global brightness floor or added
  ambient lift. Vehicle lamps and world light effects illuminate nearby surfaces
  with source-specific reach and strength, while unlit streets stay dark.
  Player, opponent and traffic cars have warm-white, restrained road beams
  with a soft, elongated fill ahead of the bumper. Front lamps on the same
  vehicle share their beam and fill at their center, retaining both lamps'
  placement as their rendering order changes. Fading lamps smoothly reduce
  their strength and contribution to that center, avoiding an abrupt switch
  or sideways jump. Nearby cars keep independent lights. Beams use the car's
  current-frame straight-ahead axis, so steering and reappearing cars do not
  inherit an old direction. Ordinary vehicle lamps are collected from the live
  model hierarchy before the body's visibility rejection, so an overtaken
  car's headlights can keep illuminating the road after its body leaves the
  screen. This pass respects current lamp switches, broken/hidden groups and
  owner flags without drawing hidden meshes or flares. Whole light volumes
  are tested against the camera planes; a bulb behind the camera remains
  relevant when its beam reaches the visible road. Every beam follows its
  vehicle's axis, so models whose lamp nodes are authored toed-out (Audi TT,
  Escalade, Avalanche,
  ambulances, armoured cars) no longer throw their light sideways. A night
  driving beam also covers the Formula 1 model without adding bodywork lenses.
  Tail, brake, reversing and turn-signal lights cast smaller tinted pools,
  barely any in daylight. The driver's headlights follow the game's own Light
  control (pad White, mapped to LB/L1 by default; keyboard Q): on at the
  start of evening/night races, off by day, and switchable either way at any
  time, in cruise as well as races. Switched off in the evening or at night,
  the tail lamps go off with them; brake, reverse and signal lamps keep
  working. Each change is logged as `[MM3_CARLIGHT] driver headlights on/off`.
  Other cars keep the time-of-day rule. Neutral street
  fixtures cast warm amber gradients with modest extra reach, and bulbs that
  previously supplied only a visible flare have bounded local pools. Colored
  fixtures and traffic signals retain their intended hues. Soft attenuation
  eases the edges and centers of street and vehicle light gradients without
  adding more lights to the queue. Street-lamp warmth comes from the fixture's
  authored color, keeping its fade monotonic through rounded brightness steps.
  Overlapping local lights use screen accumulation: each source adds a smaller
  share as that pixel brightens, preserving a lone beam while reducing washed-out
  intersections. Unrelated lights do not change a beam's brightness, and this
  uses the existing GPU blend unit without another pass or texture. The primary
  player's front beam and near field can reclaim dim non-player records when
  the shared queue fills, keeping traffic submission order from dropping them.
  Push-buffer kicks also synchronize when the producer is more than 256 KiB
  ahead, with host waits for the actual WBC acknowledgment rather than
  serializing every kick. After the producer wraps around the ring the
  distance is measured through the wrap ((end - GET) + (PUT - start)) instead
  of always draining the executor, which had happened about once a frame;
  `MM3_PRODUCER_WRAP_SYNC=1` restores that full drain. This targets a separate
  one-frame lighting race; driving validation remains necessary.
  Native time, breakage and authored flicker
  controls continue to govern world lights.
  Local illumination samples the native 3D attenuation textures and reconstructs
  surfaces from raw scene depth. The light shader receives the constant and
  separate high/low depth bytes its reconstruction rows require, so nearby
  roads and walls stay in the correct light volumes as the camera moves.
  Lamp glows use the scene depth buffer's encoding when testing occlusion,
  and are tested slightly nearer the camera so a lamp's own lens or housing
  cannot hide it. Every street lamp is lit at night (retail lit only one in N
  of each type); reversing lights cast a lane-wide pool behind the car.
  Imported wheel references retain their opaque donor headers, and rear lamp
  effects sit outside the imported lens surfaces.
  Road spline addressing uses an unbiased floor, keeping its control-point
  index consistent with the fractional interpolation at road seams.
  The top HUD direction arrow is hidden unless Mods > "Show the race direction
  arrow" (`MM3_HUD_RACE_ARROW=1`) brings it back. Original cars retain their retail
  speedometers by default, including the numeric readout in numeric-only HUD layouts.
  Cubic B-spline sampling smooths the distant skyline, and corrected frame/depth
  alignment removes the sampling offsets that caused bright silhouette rims
  and uncovered top/left frame strips.
  Native GPU coverage for these changes lives in
  `tools/xboxrecomp/tests/nv2a_graphics_quality`: independent cubic RGBA samples,
  panorama selection exclusions, packed depth at four scales with/without MSAA,
  and actual vertex output for display/menu/offscreen placement.
  Balanced renders
  at 1536×960 and Quality at 2304×1440, both at 16:10, with
  configurable aspect, resolution, MSAA, anisotropic filtering and draw distance.
- **Efficiency:** persistent shader caching and startup prewarming, less repeated
  texture/shader lookup work, optimized texture conversion and audio processing,
  and GPU-completion waits that let waiting threads sleep.
- **Timing and runtime:** a 60 FPS maximum, presentation pacing immediately before
  Present, command/fence ordering repairs, safer guest-handle access, and save-root
  handling. Graphics memory is reused across scene loads without treating skipped
  heap addresses as occupied. Extended-distance particle overlap is bounded to
  reduce effects load. CPU-drawn loading prompts continue updating after a race,
  so choosing Next Race displays the A/Start prompt instead of a black screen.
  The CRT remainder helper completes independently of stale floating-point
  comparison flags, fixing the captured Undercover taxi pickup hang.
  Pedestrian paths use the recovered native point-count helper, preventing
  passengers from walking past their boarding path. The native arrival query
  also retains its zero-flag tests so completed walks can release mission waits.
- **Driving and selection:** arrow-key driving, camera and manual-shift controls,
  focus-aware keyboard input, and selection of all 35 supported-disc car models.
- **Car workshop:** nine extra tuned variants, optional prepared model
  packages, editable paint and driving stats, sourced specification references
  and a **Restore default driving** action. The presets retain the stock donor
  engine, gearbox and handling, adjusting only weight and power. Each extra
  car can select a native HUD family and custom dial maxima; those maxima also
  calibrate its interior speedometer needle.

The detailed settings and runtime notes below describe these changes and their
limits. See the [car-pack guide](tools/car-mods/README.md) for further tuning examples.

## Upstream comparison (2026-10-12)

Compared the actual local sources and generated Xbox C with
[MattSG/mm3-recomp at `a24b737`](https://github.com/MattSG/mm3-recomp/commit/a24b737)
and its runtime, [MattSG/xboxrecomp at `b666568`](https://github.com/MattSG/xboxrecomp/commit/b666568).
Upstream had 33 game-repository commits outside this fork's ancestry at audit time.
That count does not mean 33 missing fixes: some commits update documentation or
the runtime pointer, and some repairs already have independent implementations here.

**The latest upstream improvements have not been merged wholesale into this
fork.** Its toolkit remains pinned to `2a521ff`, with this fork's reproducible
runtime patch on top. Fetching upstream updates the comparison refs; it does
not change the translated game, the pinned toolkit or the working executable.
This publication preserves the current tested fork and records the remaining
integration work below.

The status notes include later integration work. The 18 October PC upgrades are
described under [PC runtime upgrades](#pc-runtime-upgrades).

The detailed future implementation checklist is saved in
[Next fixes to do.txt](Next%20fixes%20to%20do.txt), including root causes,
source locations, dependencies, integration steps and acceptance tests.

### Upstream comparison and later integration

| Upstream fix | Status in this fork | Code comparison and remaining work |
|---|---|---|
| [Collision-cell bounds](https://github.com/MattSG/mm3-recomp/commit/9ad1762): freezes, crashes and falling through roads | Implemented after this audit | `sub_0015A929` validates decoded grids/lists once with bounded linear work, repairs invalid indexes and retains valid cells/cache hits. Native contracts and a Paris driving smoke pass. |
| [Two unresolved functions](https://github.com/MattSG/mm3-recomp/commit/c91d8f1): corrupted guest registers | Partial | The graph epilogue at `0x144ED9` is repaired. The camera-related setter at `0x2534FF` is still an unresolved stub referenced by six generated thunk-tail sites. Direct unresolved-stub reporting is also absent. |
| [Bounded push-buffer run-ahead](https://github.com/MattSG/mm3-recomp/commit/fdc33ea): one-frame dark/tunnel-lighting flicker | Implemented after this audit | Kicks retain the store fence and guest register effects, synchronizing above 256 KiB or across a wrap. The host waiter observes the real WBC clear with shutdown/self-wait guards; threshold, ABI and acknowledgment regressions cover it. Cruise/mission visual acceptance remains open. |
| [White rims on silhouettes](https://github.com/MattSG/xboxrecomp/commit/d62ae29) | Implemented after this audit | Display passes use aligned translation; Y16 depth taps compensate their quarter-pixel offset. |
| [Bright top/left frame strip](https://github.com/MattSG/xboxrecomp/commit/4d9d178) | Implemented after this audit | Display-sized passes remove the Xbox screen offset while offscreen targets retain pixel-zero snapping. |
| [Extended-distance haze](https://github.com/MattSG/xboxrecomp/commit/d448358) and [distant-tree fade](https://github.com/MattSG/xboxrecomp/commit/060122c) | Implemented after this audit | Depth-as-Y16 uses the exponential haze tail. Eligible depth-writing foliage drawn after haze uses matching coverage fading with the final ordered 2×2 distribution. Gameplay motion and different scaling modes still need visual review. |
| [Smooth horizon panorama](https://github.com/MattSG/xboxrecomp/commit/0e63cf0) | Implemented after this audit | Four bilinear taps reconstruct the skyline with cubic B-spline weights; MM3 opts in its panorama blend and texture signature. |
| [Reset fade keeps the damage bar on the gauge](https://github.com/MattSG/xboxrecomp/commit/76192ee) | Implemented after this audit | HUD-atlas fades preserve edge anchors; fullscreen fade pieces stay separate and cover wide and tall displays. |
| [Pause/post-race button caps remain joined](https://github.com/MattSG/xboxrecomp/commit/7367dca) | Partial | Our pause-state callback preserves the pause layout. Upstream also detects missing minimap/start rectangles across current and previous frames; equivalent post-race coverage is not established here. |
| [Audio queue cushion](https://github.com/MattSG/xboxrecomp/commit/9b9a37c) and [audio producer priority](https://github.com/MattSG/xboxrecomp/commit/a4c2800) | Different policy | The upstream silent cushion and time-critical producer priority are not adopted. This fork now compiles known DSP programs and uses a bounded, sleeping DSP worker at normal Windows priority; existing PCM buffering remains in use. |
| [Device interrupts preempt DISPATCH](https://github.com/MattSG/xboxrecomp/commit/d359ee0): half-second freezes | Implemented after this audit | Known level-16 host ISRs can acknowledge completion while a guest holds DISPATCH. A separate device lock retains ISR/synchronization exclusion; nested DPCs still acquire DISPATCH before claiming work. Five new regressions cover completion, nesting, cancellation, restoration and device exclusion. |
| [Facade slabs and capacity](https://github.com/MattSG/mm3-recomp/commit/31451f6) | Partial | Facade zones and their fade widths now use `min(view_distance, 2)`. Actual buffer storage and limits grow by `ceil(facade_scale^2)`, capped at 4×, with failure rollback and custom capacities preserved. Upstream enlarges four fields by 5×; this fork retains its tested three-pool strategy. |
| [Quiet hot-path logging](https://github.com/MattSG/xboxrecomp/commit/4f4dbc6) | Partial | Summary, path, successful-read and push-buffer logs are reduced or opt-in here; empty or `0` wrap-trace settings now stay quiet. Loose asset search misses use bounded samples; actual file failures include host and guest context. Upstream's spike-stage split is absent. Focused building traces add work; disable them for performance comparisons. |

The main implementation evidence is in `src/recomp_manual.c`,
`src/mm3_widescreen.c`, `src/mm3_facade_capacity.c`,
`src/mm3_render_distance.c`, `mm3_runtime_entry_repairs.json` and the patched
runtime's `src/video/nv2a_d3d11.c`, `src/apu/apu_core.c`,
`src/apu/apu_xaudio2.c` and `src/kernel/kernel_hal.c`. Generated Xbox C is
rebuilt locally from the supported disc rather than bundled in this repository.

### Upstream fixes already covered independently

- **[80-bit `fld` throttle/gearing repair](https://github.com/MattSG/mm3-recomp/commit/fd112c6):**
  the affected math helper is recovered with `MEMFP80`, and same-chunk calls
  route to it. The functional fix is present, although upstream's generic
  lifter patch is absent and the bypassed original generated function still
  contains the old load. See `scripts/Generate-MM3RuntimeEntries.py`, the repair JSON
  and CMake's recovered-helper routing.
- **[DSP inlining](https://github.com/MattSG/xboxrecomp/commit/7d47dc8) and
  [idle interrupt-check/fetch optimization](https://github.com/MattSG/xboxrecomp/commit/9d8716e):**
  our optimized Windows build already applies `/Ob2` to the hot interpreter,
  forces instruction-fetch inlining and skips idle interrupt work. Upstream's
  explicit GNU `always_inline` path is not present.
- **Developer tools off by default:** local developer features already require
  opt-in flags. The public play script also leaves focused building traces off
  by default; use `-TraceBuildings` only for a diagnostic capture.
- **Separate object/detail and building/view distance:** `RECOMP_DRAW_DISTANCE`
  controls object LODs, pedestrians and blob shadows; `RECOMP_VIEW_DISTANCE`
  controls city visibility, camera far plane and haze. Existing preferences
  inherit the old combined setting when no separate view value is present.
  Balanced/Quality retain 2×/2× defaults, rather than adopting upstream's
  increased 4× object-detail default.

### Other upstream improvements still absent

- Hover/click navigation inside the retail game menus. Settings, saved key/mouse
  and controller bindings, and individually selectable enhancements are now
  available through the native Windows launcher.
- Spike screenshots, game-thread sampling, guest call chains and the new timing
  stage reports. Local hitch tracing and frame captures do not supply those features.
- Heading display/input in the teleport GUI. The pipe already accepts an
  optional heading in radians; the GUI still only edits position.
- The 240 FPS launch option and achieved-FPS race-test reporting. Upstream's
  `8cff2f2`/`965f6d0` report successful higher-rate tests and expand the launch
  range; they do not add a new AI algorithm. This fork retains its 60 FPS cap
  and has not reproduced those higher-rate tests.

Earlier publication validation: the `RelWithDebInfo` game build passes, along with 126
Python tests (two optional cases skipped), 25 standalone native regressions,
four additional factory-physics/gearing variants and 195 CTest cases across 19
suites. The runtime patch applies to a clean export of the pinned toolkit and
reproduces all 96 modified or added runtime files after Git newline normalization.
These checks verify the published changes; they do not establish that the
missing upstream fixes above are unnecessary or that every gameplay bug is fixed.

The subsequent visual upgrades pass four new native GPU cases and 38 existing
renderer cases, plus HUD/map/menu and profile/launcher contracts. Their runtime
patch reconstructs all 99 changed or added runtime files from the pinned
toolkit, with fresh apply/reverse checks. The internal resolutions remain the
same; the stronger MSAA settings have not been compared in a matched performance run.

## Building from a fresh clone

You need Windows 10/11 x64, Visual Studio 2022 or newer with **Desktop development with C++**
(it includes CMake), Python 3.12+ recommended (3.11.4+ supported), git,
and your own Midtown Madness 3 disc.
Most examples use PowerShell 7 (`pwsh`); the pinned-version commands below use
Windows' built-in `powershell` and also work with `pwsh`.

```powershell
git clone --recurse-submodules https://github.com/lakshya16157/midtown-madness-3-PC-Native.git
cd midtown-madness-3-PC-Native
pwsh scripts\setup.ps1 -Iso "D:\Games\Midtown Madness 3 (USA).iso"
pwsh scripts\play.ps1            # -Keyboard to drive with the keyboard
```

`setup.ps1` checks the toolchain, makes `tools\venv` (pinned capstone), verifies the disc and
extracts it to `game_files\`, generates the translated C into `src\recomp\gen\` (about six
minutes, deterministic) and builds `build\RelWithDebInfo\mm3_recomp.exe`. Re-running skips
finished steps; `-Force` redoes them. Nothing from the disc is committed.

This repository includes its runtime fixes in `patches/xboxrecomp-mm3.patch`.
Setup applies them to the pinned Xbox runtime submodule automatically and safely
skips an already applied patch. The upstream toolkit and its generator history
remain available through the submodule. Before building manually, run
`python scripts/apply_runtime_patch.py` once after initializing submodules.

For a checkout with local tools installed, activate them in the current PowerShell session:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
. .\scripts\enter-env.ps1
pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup.ps1 -Iso "D:\Games\Midtown Madness 3 (USA).iso"
```

The local environment uses `tools\venv`, portable PowerShell at `tools\bin\powershell`,
and MSVC/Windows SDK at `tools\bin\msvc` (via `setup_x64.bat`). With local MSVC,
install CMake and Ninja in the venv (`python -m pip install "cmake>=3.20,<4" ninja`);
setup uses Ninja Multi-Config and keeps the same `build\<Config>` output paths.
Run these commands from the repository directory, not its parent folder.

### Current version on main

The default [main branch](https://github.com/lakshya16157/midtown-madness-3-PC-Native/tree/main)
contains the square bottom-left race map, aligned corner routes and markers,
useful file/crash diagnostics, desktop shortcuts and icons, native tree
physics/audio fixes, and car workshop improvements. The tested runtime remains
pinned, with this fork's changes carried in the runtime patch.

The October 14 update adds native graphics choice lists, original-first vehicle
ordering with speed-sorted DLC/custom cars, and live aspect changes that expand
the gameplay view while preserving menu proportions.

The normal clone and setup instructions above include these updates. Run
`scripts\setup.ps1 -Iso "<your supported disc image>"`, then
`scripts\play.ps1 -Keyboard`. Game files and build output are not included.

### Reproduce an earlier version (`v2026.10.12.3`)

The tag **[v2026.10.12.3](https://github.com/lakshya16157/midtown-madness-3-PC-Native/tree/v2026.10.12.3)**
pins this fork's gameplay improvements at that snapshot: race/menu loading and heap fixes, building
detail fades, visibility and rendering work, corrected imported-car shadow
generation, and the ten researched car presets with their gearing and gauges.
It also includes the joined pause-menu buttons, Space handbrake, the closest
chase camera's 0.25 m adjustment, adjustable driving FOV, aligned race-map
checkpoints and model credits. This snapshot adds map zoom and race guidance,
facade-capacity scaling, building/tree diagnostics, car light and reflection
repair tools, and restored factory driving defaults. The earlier `v2026.10.12`,
`v2026.10.12.1` and `v2026.10.12.2` tags remain available as previous snapshots.
Use this tag to reproduce that source and settings version after `main` moves on.
The minimal-logging launcher default and detailed future-fix checklist were
added later on `main`.

If you have already extracted or recompiled the game with upstream, the generated
Xbox C alone does not contain this fork's native hooks, runtime patch or car data.
Build this checkout in a separate directory, using your own supported disc image:

```powershell
git clone --branch v2026.10.12.3 --recurse-submodules https://github.com/lakshya16157/midtown-madness-3-PC-Native.git mm3-improved
cd mm3-improved
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\setup.ps1 -Iso "D:\Games\Midtown Madness 3 (USA).iso"
tools\venv\Scripts\python.exe tools\car-mods\prepare.py --config tools\car-mods\cars.json --apply
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\play.ps1 -Profile Balanced -Keyboard
```

Setup applies the committed runtime patch to the pinned submodule, generates the
supported translation, configures all native hooks and builds the game. Installing
the car preset is a separate step: setup alone does not install the ten extras.
The explicit `--config` selects the published settings even if a machine has a
different `cars.local.json`. `git describe --tags --exact-match` should report
`v2026.10.12.3`; `git submodule status` should show the pinned toolkit, without a
leading `+`. A dirty runtime submodule is expected after applying this fork's patch.

**To reproduce the imported car bodies and repaired shadows**, obtain the nine
original model archives listed in the [model guide](tools/car-models/README.md),
install its conversion dependencies and follow its import/preview/package recipes.
Then close the game and select the portable imported-model preset:

```powershell
tools\venv\Scripts\python.exe tools\car-mods\prepare.py --config tools\car-mods\cars.imported.json --apply
tools\car-mods\editor.cmd --config tools\car-mods\cars.imported.json
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\play.ps1 -Profile Balanced -Keyboard
```

The preset points at `out/car-models/packages/<alias>` for each imported car and
uses the same tuning as `cars.json`. Mini Rally keeps its native Mini body. Game
files, source-model downloads and prepared packages are not bundled in the tag;
without building those packages, use `cars.json` for the same car slots and tuning
with donor visuals. Keep the source authors' readmes and applicable usage terms.

For an existing checkout of **this tag** with extracted game files, run setup
again without `-Iso` to rebuild, then reapply your chosen preset. Setup always
reconfigures CMake; manual rebuilds should also configure before building:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\setup.ps1
tools\venv\Scripts\python.exe tools\car-mods\prepare.py --config tools\car-mods\cars.json --apply
```

To regenerate just the translated code without re-extracting your game data,
run these commands in the existing build environment, then reapply the preset:

```powershell
. .\scripts\enter-env.ps1
python scripts\generate_mm3.py
cmake -S . -B build
cmake --build build --config RelWithDebInfo --target mm3_recomp --parallel
```

Use a fresh checkout when moving from an older fork version whose runtime patch
is already applied. A plain pull does not replace that dirty runtime with the
new patch, and the patch helper intentionally rejects mixed versions. Keep old
local work and saves in the previous directory. `-Force -Iso <path>` re-extracts
and regenerates the game; reapply the car preset afterward.

The tag reproduces the published source, presets and conversion recipes. It does
not promise a byte-identical executable across compiler versions, identical
historical model-package packing, saved progression or identical FPS on another
computer. Unpublished race-car values and MM3's hybrid/handling approximations
remain identified in Workshop's **Car specs** tab and the specification catalog.

### Which disc

Any dump whose `default.xbe` carries the retail code: a redump `.iso` (video + game partitions)
or a bare XISO. `scripts\extract_disc.py` gates on a SHA-256 of the XBE's section data
(`9c437324…6bbf8ba4`) with the one byte XISO tools patch to skip the DVD media check
(VA `0x87226`, `jge` → `jmp`) normalised, so region/certificate differences and that patch do
not matter. USA and Europe/Australia share the same code. Extraction applies that same media
patch to `game_files\default.xbe` (the emulated drive can't authenticate a pressed disc), so every
supported disc translates identically. Images tested end to end:

| SHA-1 | Image |
|---|---|
| `52cd998731eb67477b44c49c4152f56c0fc58a56` | Midtown Madness 3 (USA) — redump `.iso`, 7.8 GB |
| `3411caf817c8d63bfbd582d9a347da81ef652cb5` | Midtown Madness 3 (Europe, Australia) (En,Fr,De,Es,It) — redump `.iso`, 7.8 GB |
| `8ed003adaf504883afc980d2888df6125496d044` | Midtown Madness 3 (Europe, Australia) (En,Fr,De,Es,It) — XISO, 3.6 GB |

An untested image with matching code is accepted with a note; anything else is refused with
this list.

### Regenerating

`scripts\generate_mm3.py` is the only generation entry point. It runs the recompiler pinned to
toolkit commit `967ec30` (extracted to `tools\.generator\`): its output is the translation the
game was proven on, and regenerating reproduces it function for function. The submodule's
newer recompiler (upstream v0.13.x) translates ~3.6k functions differently and has not been
through a play session yet; try it with `MM3_GENERATOR_DIR=tools\xboxrecomp`. The hooks in
`CMakeLists.txt` find each wrapped function's chunk themselves, so any regeneration builds
without edits.

## Running

Create a **Midtown Madness 3** desktop shortcut with the game icon:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\create-shortcut.ps1
```

Double-click it to launch with keyboard controls and the normal play settings.
Run the command again after moving the game folder to update the shortcut.
Windows builds also embed the game icon in the executable and use it for the
game window and taskbar.

`scripts\play.ps1` sets the defaults below and runs the build from the repository root (the
game reads `game_files\`). By hand:

```powershell
$env:RECOMP_PB_EXEC='1'; $env:RECOMP_FB_WINDOW='1'; $env:RECOMP_USB='1'
$env:RECOMP_APU_DSP_ACK='0x80458810'
build\RelWithDebInfo\mm3_recomp.exe
```

Use `scripts\play.ps1 -Keyboard -NoLightDiagnostics -NoConsoleLog` for ordinary
play after a lighting investigation. `-NoLightDiagnostics` clears inherited
`MM3_LIGHT_DUMP` and `MM3_LIGHT_DEBUG` for that launch and restores the shell's
settings afterwards. Lighting captures synchronously read GPU textures and
write images at three intervals, then keep checking for capture requests; they
can cause brief frame-time spikes even with console output hidden.

Car-light caches now expire once at the native light-queue reset, including
frames with no car callbacks. Each lamp's frame check uses constant time;
masked lamps no longer rescan every cached light record as traffic grows.
Same-frame beam alignment, source fades and warm colors are retained.

Front lenses, headlight beams and their road fill now activate only in the
native evening/night periods. Morning/noon rear-light illumination is faint
and close to the lamps: tail lights use a 0.35 m radius and brake lights
0.5 m, with 93% and 90% less intensity respectively. Visible lenses retain
their authored appearance; evening/night retain the wider, brighter gradient.

Post-race dialogue and result panels retain one centred canvas after the race
HUD disappears, keeping separately drawn caps, blue fill and text together.
The renderer recognizes stock, expanded and masked map windows, and preserves
explicit world-name/route placement. The preceding frame's HUD evidence is
retained for one transition frame; this fallback does not eliminate a possible
first-frame seam. `python tools/xboxrecomp/tests/test_menu_placement.py` checks
subsequent dialogue frames, HUD restoration and the existing map layouts.

Audio output keeps exact 48 kHz scheduling instead of discarding fractional
microseconds per buffer. Voice playback preserves inclusive loop/stream end
samples and mixes cached one-shot tails before completion. Renderer/DSP
profiling flags set to empty or `0` now disable their timing work. These fixes
pass focused native contracts; they are not a measured whole-game frame-time
improvement. `python tests/mm3_renderer_profile_contract.py` verifies the
renderer makes no profiling clock calls while disabled.

The 18 October 2026 release-mode car-light replay measured 20 cars at
0.091 to 0.028 ms/frame and 80 cars at 1.097 to 0.130 ms/frame, before/after
the reset-hook change. Each car had eight additional masked lamp callbacks;
51 native fixture records shared the queue. The crowded case eliminates
155,629 whole-record comparisons (13,072,836 bytes) per replayed frame.
This measures producer CPU cost, not complete gameplay frame times. Run
`python tests/mm3_car_lighting_performance.py --variants baseline current`
after `scripts/enter-env.ps1` to repeat the native producer replay. The
car-light regression separately checks native reset ABI, skipped-frame reuse,
paired fades and current-frame alignment.

Crowded-source lookup uses a bounded hash table and cached native record
positions, with pointer/image validation when the queue changes. Failed
allocations do not create repeated empty groups, and unchanged source callbacks
reuse their records. The native 256-light limit remains enforced. Run
`python tests/mm3_car_lighting_performance.py --variants current --stress --prepass`
to measure source emission with both light-only and visible callbacks; this
excludes hierarchy traversal and GPU time. The visibility regression
`python tests/mm3_car_light_visibility_regression.py` replays both native
rejected-body entries, live motion and lamp controls, all installed lamp
ancestries, conservative volume culling and 320-model queue pressure.
Add `--benchmark` to that visibility regression to time the complete source
hierarchy walk and emission in `/O2`, using installed retail hierarchy shapes,
transforms and lamp parameters. The 18 October run measured medians of
0.168 ms/frame for 80 visible models and 0.171 ms with their bodies behind the
camera; 320 models measured 0.748 and 0.719 ms respectively. Seven samples
include the native queue reset and 51 scene slots, but exclude body drawing
and GPU work. Fully rejected models leave those 51 slots and no car groups.

The player-priority regression in `tests/mm3_car_lighting_regression.py` also
fills every native slot, varies traffic order over 24 frames, and checks that
the player's beam/fill survive subsequent paired, environment and displaced
traffic callbacks without aliasing records. Native queue order and all reusable
record pointers stay intact. `python tests/mm3_pushbuffer_run_ahead_regression.py`
checks the conditional 256 KiB/wrap handshake and guest ABI at `/Od` and `/O2`;
the runtime `nv2a-wbc-wait` test exercises the actual acknowledgment and host
wait, including completion races and shutdown. GPU local-light fixtures cover
bounded overlap with 1 through 32 sources in both shader paths and MSAA/scales.

Read-only light draws now reuse the converted scene depth until a submitted
depth/stencil write or clear changes it. Previously, merely binding a depth
target invalidated the view, so repeated lighting batches could repeat the
fullscreen conversion. A GPU regression replays 16 native LightDraw submissions:
the conversion count falls from 16 to 1 with the same illuminated output.
It also checks depth/stencil writes, clears and projection metadata changes
across both shader paths, 1x/4x MSAA and 1x/3x rendering scale. With
`RECOMP_D3D_PROFILE=1`, the per-frame light-draw, depth-view conversion and
depth-target alias counts show whether a gameplay scene can reuse these views.
The optimization removes verified redundant GPU work; the synthetic conversion
ratio is not a whole-game speedup. Audio producer shortfalls in existing logs
remain a separate observation, without an established link to driving hitches.

Ordinary roadside-object draws and collision updates also avoid unnecessary
Windows memory reads in the tree hooks. Native stack arguments and the title's
contact counter are read directly; indirect model/body metadata still uses
checked reads. An optimized replay of 100,000 calls reduced ordinary draw
overhead from 0.3540 to 0.0093 microseconds and collision-hook overhead from
1.4271 to 0.0155 microseconds, removing one and four OS reads respectively.
These are hook timings, not whole-game frame times. Run
`python tests/mm3_tree_hotpath_regression.py --benchmark` to repeat the checks;
the existing native tree contact, body, renderer and audio regressions also pass.

The game enforces a maximum of **60 FPS** for presentation and emulated vblank.
`RECOMP_FPS_LIMIT` may select an integer limit from 1 to 60. When unset, it uses
saved setting, or the selected profile's cap when no setting has been saved:
60 for Balanced/Quality or 30 for LowPower. Invalid, uncapped (`0`), or higher
values use the saved setting or profile default, including direct executable launches.

If driving motion is uneven, compare VSync on and off at the same FPS cap using
Display Settings or the temporary `RECOMP_VSYNC=0` environment override. An
evening Paris Cruise investigation with maximum traffic on a hybrid NVIDIA/AMD
laptop isolated a presentation wait: the same frontend ran at about 50 FPS with
VSync and a steady 60 FPS without it. The VSync-off driving capture held 60 FPS,
with a 16.92 ms p99 guest-flip interval and a 19.13 ms maximum; actual host
Present intervals remained less regular (22.49 ms p99, 25.69 ms maximum).
That driving capture used the post-Present pacing override. A final frontend
check with normal before-Present pacing and profiling off held 60 FPS, with
17.32 ms p99 and 17.95 ms maximum host Present intervals. Lighting stayed enabled.
Driving routes diverged, so these are observations
from that machine, not a general performance guarantee. Moving the limiter
after Present and reducing the DXGI queue did not resolve the VSync-on cadence;
neither experiment changes the normal renderer defaults.

Choose **Graphics Settings** below **Options** and above **Profile** in the
main menu. Select a setting to open its choice list, move to the exact value,
and press **A/Enter** once to select it. The current choice is marked.
**Advanced Graphics** contains aspect ratio,
anti-aliasing, texture filtering, **Object Detail Distance** and **Building View
Distance**. **Apply** saves both pages;
**Restore Defaults** changes the pending choices, and **B/Back** from the main
graphics page discards changes made since the last Apply. Restart requirements
appear in the help text. These pages use the game's original layout, font,
sounds and navigation; the retail archives are left intact.

Press **F10** to open **Display Settings** while the game is running. The same
command appears in the game window's title-bar menu (Alt+Space). Choose the
window resolution, windowed or borderless fullscreen mode, maximum FPS, VSync,
an FPS counter and rendering quality, then click **Apply**. These changes take
effect immediately. Window resolution controls the window size; rendering
quality controls the internal image resolution.

Aspect ratio takes effect immediately. **Automatic** follows the window's
shape: wider views reveal more scenery horizontally, while taller views reveal
more sky and road vertically. Frontend and pause menus keep their original
proportions, and the gameplay HUD stays proportionate. An explicit aspect ratio
fits inside the window with bars where needed. Saved explicit ratios remain
available; choose Automatic to follow resolution changes.

MSAA, anisotropic filtering and both distance controls are also available in the dialog.
These changes are saved for the next launch, and the dialog shows when a
restart is required. Settings are stored per Windows user in
`%LOCALAPPDATA%\MM3Recomp\display.ini`. Set `MM3_DISPLAY_CONFIG` to use another
settings file, for example for a separate benchmark configuration. At launch,
saved settings override profile defaults; explicit environment settings take
priority over saved settings.

Object Detail Distance changes object and pedestrian LOD ranges and blob
shadows. Building View Distance changes city visibility and the camera far
plane. Both accept 1×–4× and require a restart. Existing `draw_distance`
preferences also supply the building value until a separate `view_distance`
is saved. Facade detail zones stay capped at 2× to match their allocated
buffers, even when the city view is farther away; different asset types still
retain their authored visibility and LOD thresholds.

F11 or Alt+Enter toggles fullscreen. The default **Balanced** profile renders at
960 vertical pixels and an automatic aspect ratio that follows the window or
fullscreen size. The internal height stays fixed when you maximize the window. Profiles
provide these initial defaults when no display preferences have been saved.
Choose a profile when launching:

```powershell
pwsh scripts\play.ps1 -Profile Balanced
pwsh scripts\play.ps1 -Profile Quality
pwsh scripts\play.ps1 -Profile LowPower
```

| Profile | Internal height | MSAA | Object detail | Building view | FPS cap |
|---|---|---|---|---|---|
| Balanced (default) | 960 | 4× | 2× Xbox | 2× Xbox | 60 |
| Quality | 1440 | 8× | 2× Xbox | 2× Xbox | 60 |
| LowPower | 720 | Off | Xbox | Xbox | 30 |

All three retain 16× anisotropic filtering. LowPower reduces resolution, edge
smoothing and view distance and caps motion at 30 FPS. Balanced and Quality
now increase edge and cutout smoothing without increasing their resolution.
Unsupported MSAA counts fall back to the highest supported count.
Direct executable launches use the same profiles
through `MM3_PERFORMANCE_PROFILE=Balanced`, `Quality` or `LowPower`; unset or
unrecognized values select Balanced. Individual `RECOMP_*` environment settings
take priority. `-FpsLimit 45` selects a different cap when `RECOMP_FPS_LIMIT` is
unset. The launcher restores environment defaults it added when the game exits.

Matched stationary runs on 2026-10-11 used a Ryzen 9 PRO 7940HS (8 cores,
16 threads), an RTX 2000 Ada Generation Laptop GPU with 8 GB VRAM (8,188 MiB
reported by `nvidia-smi`), and 32 GB system RAM. Each profile used Senna GTR in
Paris at the same position, about 132 draws per frame, and approximately 31.8
seconds. These historical runs used the earlier Balanced 2× / Quality 4× MSAA
defaults; they do not measure the current stronger MSAA settings:

| Profile | Mean FPS | 99th-percentile frame interval | Process CPU | NVIDIA GPU | GPU power |
|---|---|---|---|---|---|
| Quality | 59.97 | 19.62 ms | 11.39% | 43.37% | 28.18 W |
| Balanced | 60.00 | 17.34 ms | 11.20% | 31.30% | 14.37 W |
| LowPower | 30.00 | 33.85 ms | 8.70% | 18.73% | 10.56 W |

CPU percentages use all 16 logical processors as 100%. GPU utilization and
power are device-wide samples, so they include other activity. These short,
stationary measurements support Balanced's lower GPU demand while retaining
60 FPS; they do not establish a renderer CPU improvement or predict busy
driving scenes on other machines. No recorded frame interval exceeded 50 ms.
LowPower's approximately 33.33 ms intervals follow its 30 FPS cap.

Four native scene loads also completed with no detected guest-heap overlap.
That checks the repaired allocation path within those runs; it does not prove
that every possible crash or menu stall is fixed.

A separate 45-second Balanced driving run averaged 57.80 FPS, with a
99th-percentile interval of 22.86 ms and one 506.39 ms hitch. Its busier scene
is not comparable with the stationary table. An intermittent intro-video
decoder crash was also observed; additional instrumented runs did not
reproduce it. These two issues remain unresolved.

Set `RECOMP_RENDER_SCALE=auto` to follow the window resolution instead;
`RECOMP_SUPERSAMPLE` applies in that mode. Set `RECOMP_ASPECT=auto` to follow the
window's aspect, or select another aspect explicitly.

With `scripts\play.ps1 -Keyboard`, hold **Up** to accelerate, **Down** to brake/reverse,
and **Left/Right** to steer. Arrows also navigate menus. **Z** confirms, **X** goes back,
and **Escape** also goes back. **Enter** pauses/resumes. **C** changes the camera
view while driving. Hold **Space** for the handbrake (the game's default Xbox A
control); **Z** still works for A. Space is active only while driving with the
MM3 keyboard layout. The original
**3** accelerator, **1** brake and numpad **4/6** steering keys still work. Keyboard input
releases when the game loses focus.
Close the focused game with **Alt+F4**, or click the window's **X**. Closing
the display ends the process and releases the game archives. The launcher
captures output without attaching the game to the terminal, so terminal
**Ctrl+C** is not the game-close shortcut.
Hold **Ctrl** to enlarge the race map; its checkpoint icons stay aligned with the roads.
Choose **Pause → Options → HUD Settings → Minimap: Stock / Improved**.
The choice saves immediately and takes effect when driving resumes. It is also
available in **Graphics Settings → Advanced Graphics → Minimap Style**, and
**F10 → HUD → Minimap style**, where **Apply** saves the selection.
Improved is the default. Set `MM3_MINIMAP_IMPROVED=0` or `1` to override the
saved selection at startup.

The improved minimap shows only gray streets, blue race guidance and native
markers over the game. Roads, route and markers share a tilted perspective,
with distant roads narrowing and the player positioned below the center.
The driving frame is 37.5% larger than the previous improved map, with the
car around 70% down the visible road plane. The stopped view shows nearby
streets with an additional 10% zoom, while the player marker keeps its original size.
It stays in the bottom-left corner. Holding Ctrl opens a flat, centered
overview; its roads and checkpoints share the same expanded window.
The north-letter sprite is hidden in both map styles unless Mods > "Show the
north marker on the minimap" (`MM3_HUD_NORTH_MARKER=1`) keeps it; it then sits
where the game places it, on the rim of the original round map.
The map rotates with your heading. Cruise also shows roads without race data.
Washington's park, plaza and block paths use thin, subdued strokes underneath
the streets. Fine path details fade as the map zooms out, keeping dense areas
readable; zooming back in restores them from the complete native networks.
Paris roundabouts include their native traffic lanes and street entrances,
while pedestrian paths through their center islands stay clear.
Both cities use narrower minor streets and a limit on avenue thickness so
overlapping roads stay clearer. Street weight still follows the lane layout
and map zoom, while blue race guidance remains prominent.
Stock restores the native city artwork, circular border, marker sizes and zoom,
and retains the existing amber race guidance.
The driving map smoothly shows more road as speed rises above 20 km/h, reaching
60% more visible distance at 120 km/h. The frame, perspective and marker
sizes stay fixed while the contents zoom out. Holding Ctrl expands the visible
area at the same world scale and speed zoom, so roads and checkpoints stay in
range throughout expansion and release. Driving-map framing stays stable as
you approach checkpoints.
Both map sizes draw the ordered checkpoint course along actual streets with
a blue path. The brighter leg updates from the car to the next checkpoint,
including after a wrong turn. The remaining course stays visible through the final checkpoint. Checkpoint
icons use 75% of their original width and height at every speed, retaining
their colors and pulsing. World hoops retain their original size.
Undercover jobs also route to the currently selected pickup, drop-off or other
visible objective. The path updates when the mission selects its next stop and
clears when the mission hides that objective. Jobs that show all their zones at
once, such as the paper routes, draw the whole remaining course: the bright leg
from the car to the selected zone, then the dimmer path through every later
zone the mission is showing. Visited zones drop off the front of the course.
Both Blitz and Checkpoint races end at their final gate. Return legs only appear
when the ordered checkpoints explicitly revisit a road.
Choose **Manual** transmission in car setup, then tap **R** to shift up and **F** to shift down
(the default Xbox B/X gear controls). These aliases operate only while driving.
Set `RECOMP_KEYBOARD_LAYOUT=legacy` to use only the original runtime bindings.

The closest rear chase camera sits 0.25 m farther back. Its original smoothing
and collision adjustment still apply; the other camera views retain their
original distances.

Choose a wider driving field of view when launching, for example:

```powershell
pwsh scripts\play.ps1 -Profile Balanced -Keyboard -Fullscreen -Fov 100
```

`-Fov` accepts 60–110 degrees of horizontal FOV at your display aspect, including
decimal values. Omit it to retain the existing widescreen FOV. The setting
applies to driving views, with matching city visibility; menus and mirrors use
their original projection. Restart the game to change it. An existing
`MM3_FOV_DEGREES` environment setting takes priority over the launch argument.

The 35 original cars follow the game's earned availability rules, including hidden bonus
cars. Optional custom cars remain available as configured by their installer.
Set `MM3_UNLOCK_ALL_CARS=1` to make every installed car selectable; unset, `0` and
other values preserve the original rules. Event-specific vehicle classes, bonus paints
and career statistics still follow the game and profile. The override does not write
unlock keys to your save.

## Added cars and editing stats

The optional [extra-car pack](tools/car-mods/README.md) adds nine separately
selectable slots alongside the 35 original cars:
Panoz GTR-1, McLaren F1, Formula 1, Ford GT,
McLaren Senna, McLaren Senna GTR, Ferrari FXX K, Koenigsegg Regera and Lamborghini
Aventador. Each has its own name, paint settings, menu ratings and driving setup.
The [model names, download sources and creator credits](tools/car-models/README.md#extra-car-names-and-model-credits)
identify each imported exterior and its original conversion pack.
The remaining slots retain their IDs. Profiles that remember a removed custom
car use its original donor vehicle, without restoring the removed menu entry.

Original cars appear first in their retail order. Added and DLC cars follow
together, ordered from slowest to fastest by the game's top-speed rating;
equal ratings use vehicle ID to keep the order consistent. This also applies
to existing car-pack installations without reinstalling them.

The supplied defaults use stock donor visuals. Workshop's **Model** tab can
select prepared imported body, wheel, texture and menu-icon packages; reapplying
stats preserves that selection. The [model workflow](tools/car-models/README.md)
documents the verified Panoz, F1 GTR, Ferrari F2007, Ford GT, Senna, Senna GTR,
FXX K, Regera and Aventador conversions. Downloaded models and generated packages
are not distributed in this repository. Imported initial liveries remain fixed,
glass is opaque, and interior, sounds and collision remain based on the donor.
Roadside trees detach at or above 18 km/h into the game's native movable-prop
physics, keeping collision with cars after breaking. Tree impacts use the
original sound bank, with a guarded fallback for missed collision audio.
Defaults retain the donor's engine curve, automatic shifting, gearbox and
handling. Only mass and native torque scale are tuned for arcade driving.
Manufacturer specifications remain available as references in Workshop.

### Visual editor

1. Close the game, then double-click [tools/car-mods/editor.cmd](tools/car-mods/editor.cmd)
   or run `tools\car-mods\editor.cmd` from the repository root.
2. Select an added car. **Car specs** shows its reference variant, source links
   and calibration notes. Edit its paint, menu ratings, physics, gearing, RPM
   settings and speedometer dial ranges. Every value shows its **Base** value,
   a **Compare** car's value, the **Max allowed** by the installer and the **Game
   max** among the original and DLC cars. Blank values use the **Base stats** car:
   the body donor by default, or any original car you pick for its engine, gearbox,
   weight, brakes, grip and drag. **Compare with** picks any added, original or DLC
   car. **Restore shipped tuning** on the Driving tab restores the weight/power
   preset and the donor's driving values while retaining paint, ratings, gauges
   and model choices.
   For speedometers, open **Appearance → On-screen speedometer (HUD)**, choose
   **HUD style**, then enter both **Dial max km/h** and **Dial max mph**. Leave
   both blank for the selected style's native artwork and range. The previews
   show the dial that will be used.
3. Choose **Save** to write the selected manifest, or **Save & Apply**
   to save and install the changes into your extracted `game_files` data.
   Invalid values are marked in red as you type, and unsaved cars show a dot.
4. Restart the game and select the car to load the changes.

The paint view is a schematic color preview; it does not display or edit the 3D
car mesh. The editor is a small Python/Tkinter desktop app with no web server or
extra GUI packages. Its source is [editor.py](tools/car-mods/editor.py). The
[car-pack guide](tools/car-mods/README.md#visual-editor) explains each column and
the optional `driving_base` manifest field behind **Base stats**.

### JSON and command-line editing

The editor and installer select `tools/car-mods/cars.local.json` when present,
otherwise [tools/car-mods/cars.json](tools/car-mods/cars.json). The local file is
ignored by Git and keeps machine-specific model selections and tuning. Copy the
tracked defaults to `cars.local.json` to start local settings on a fresh checkout.
You can edit the selected file directly with a text editor. From the repository
root, run the preview first, then apply with
the game closed:

```powershell
tools\venv\Scripts\python.exe tools\car-mods\prepare.py
tools\venv\Scripts\python.exe tools\car-mods\prepare.py --apply
pwsh scripts\play.ps1 -Keyboard
```

Both the editor and installer accept `--config <path>` and `--game-dir <path>`
for another manifest or extracted game directory. For example:

```powershell
tools\venv\Scripts\python.exe tools\car-mods\editor.py --config tools\car-mods\cars.json --game-dir game_files
tools\venv\Scripts\python.exe tools\car-mods\prepare.py --config tools\car-mods\cars.json --game-dir game_files --apply
```

Keep `id`, `alias`, `donor` and `display_name` unchanged: these identities are
registered by the runtime. Adding a new identity or donor body requires code
changes as well as a manifest entry. The supplied editor and manifest tune the
added variants; they do not provide a general editor for every original car.

### What each setting changes

Menu ratings are integers from 0 to 9: `TOPSPEED`, `ACCELERATION`, `MASS`,
`HANDLING` and `DURABILITY`. They control the menu bars. Actual driving behavior
comes from the separate `physics` settings, and `colors` contains one to three
paint colors in `#rrggbb` format.

| Physics setting | Effect |
|---|---|
| `TorqueScale`, `TorqueScaleFactor` | Peak torque in Nm with a factory profile, or donor torque scale; its multiplier. |
| `Mass` | Physical weight. |
| `BrakeTorque`, `HandBrakeTorque` | Braking and handbrake strength. |
| `LatFrictionFactor` | Cornering grip. |
| `AirKZZ`, `AirKZY` | Aerodynamic coefficients. |
| `WheelDrive` | 0: front wheels; 1: rear wheels; 2: all wheels. |
| `SteeringEndSpeed` | Speed-dependent steering setting. |
| `WheelRadius` | Wheel radius, also used in automatic shift-speed calculations. |
| `GearRatioVector` | Reverse ratio followed by one through seven forward ratios. |
| `WheelRatio` | Final drive multiplier; a smaller value makes gears longer. |
| `TimeChangeGear`, `TimeChangeGearFR` | Forward shifting and forward/reverse transition times, in seconds. |
| `MinRPM`, `MinRPMGearChange`, `CutRPM`, `MaxRPM` | Idle, minimum shift, cutoff and redline settings. |
| `MaxTorqueRPM`, `MaxTurboTorqueRPM` | Peak torque RPM references; automatic shift target in donor mode. |
| `GearStatusDownGearFactor` | Automatic downshift factor. |

For example, changing Panoz's `physics.TorqueScale` from `750` to `800`
increases engine torque. Changing `ratings.ACCELERATION` changes only its menu bar.
The installer validates supported value ranges before applying changes.

### Gearing and speedometers

Use a negative reverse ratio followed by positive, decreasing forward ratios,
for example `[-3.3, 2.5, 1.72, 1.28, 1.0, 0.82, 0.7]`. Gear count is derived
from this array; do not add `NGears`. Keep
`MinRPM < MinRPMGearChange < CutRPM <= MaxRPM`, and keep both peak-torque RPM
settings above idle and below cutoff. Donor mode retains the stock torque curve's
8,000 RPM peak limit. The supplied presets leave the donor's gear ratios,
final drive, RPM limits and shift times unchanged. Formula 1 uses the Lotus
five-speed gearbox; McLaren F1 and Regera use the Koenigsegg six-speed gearbox.
The optional advanced `engine_profile: "factory"` mode remains available for
experiments with researched curves and transmissions; it is disabled in the
default and imported presets.

Automatic upshifts depend on road speed calculated from gear ratios, final drive,
wheel radius and the factory profile's shift target, or peak torque RPM in donor
mode (the turbo peak for a turbo-equipped donor).
Wheelspin can raise RPM before the car reaches its shift speed. Lowering
`MinRPMGearChange` alone does not make automatic upshifts happen earlier.
The default presets restore the stock shifting behavior, and the installer
rejects conflicting thresholds in custom edits. Select **Automatic** or **Manual**
in the game's car setup; Manual uses **R/F** on the MM3 keyboard layout or Xbox
**B/X** to shift up/down while driving.

For added cars, optional `hud_style` selects an original car's native HUD family
and needle arc; omitting it uses the fixed MM3 donor's family. This selection is
independent of **Base stats**, which changes driving physics.

The optional `speedometer.max_kmh` and `speedometer.max_mph` values set both the
on-screen dial range and interior needle calibration. Enter both limits, or
leave both blank for the selected HUD style's native artwork and range. Custom
maxima render a new dial with matching markings and needle calibration. Each
unit retains its native artwork when its maximum equals the selected family's
native printed maximum. Formula 1's current Koenigsegg style at **380 km/h /
240 mph**, for example, uses the native metric dial and a custom mph dial;
Koenigsegg's native mph maximum is 280. Changing a dial limit does not boost
performance, and Workshop does not infer dial maxima from physics or the
`TOPSPEED` menu rating. Choose **Save & Apply**, then restart MM3 to load changes.

**Original HUD speedometers...** independently changes original sport and
Koenigsegg HUDs. Original cars default to their complete retail gauges, artwork,
scales, needles and numeric-only readouts; blank original limits restore that
HUD. Their physics and interior gauges stay unchanged. Added cars retain their
existing hidden numeric-only readout.

The installer backs up affected game archives and reuses those originals when
reapplying, so edits do not accumulate. It does not edit saved profiles. To
remove the pack and restore the original game data, close the game and run:

```powershell
tools\venv\Scripts\python.exe tools\car-mods\prepare.py --restore
```

## Options

| Variable | Default | Effect |
|---|---|---|
| `RECOMP_PB_EXEC` | off | Execute the push buffer on the D3D11 renderer. **Required** for the game to draw. |
| `RECOMP_FB_WINDOW` | off | Open the game window. **Required.** |
| `RECOMP_USB` | off | Emulate the USB controller ports so pads (XInput) reach the game. **Required** for input. |
| `RECOMP_USB_PADS` | saved setting or `1` | Number of controllers in game (1-4), selected in launcher Controls. Keyboard input shares player 1. |
| `RECOMP_APU_DSP_ACK` | unset | DSP acknowledgement address(es) the audio needs; use `0x80458810`. |
| `MM3_APU_IDLE_TRAP_FIX` | `0` | Experimental, for the report that engine and ambient sound vanish after several collisions: `1` re-reports the idle voice to DirectSound each frame, keeps guest threads from overwriting its trap registers and passes over an idle voice DirectSound keeps declining. Unproven; default off keeps the idle-voice handling unchanged. |
| `RECOMP_APU_IRQ_HOLDOFF` | `0` | Diagnostic: delay the audio interrupt up to this many frames while a guest thread runs at DISPATCH_LEVEL or above (0 = never delay). |
| `MM3_DISPLAY_CONFIG` | `%LOCALAPPDATA%\MM3Recomp\display.ini` | Override the saved display-settings file. F10 opens the settings dialog. |
| `MM3_LAUNCHER` | automatic | `1` opens Settings/Controls/Mods; `0` skips the window while applying saved preferences. The play script opens it by default; `-NoLauncher` skips it. |
| `MM3_LAUNCHER_CONFIG` | beside `display.ini` | Override `launcher.ini`, which stores bindings, Mods and network settings. |
| `RECOMP_KEYMAP` / `MM3_DRIVE_KEYMAP` | saved bindings | Keyboard/mouse bindings and driving actions, edited through Controls. Explicit environment maps use `ACTION:VK` pairs with hexadecimal Win32 key codes. |
| `RECOMP_PADMAP` | saved bindings | Controller button/trigger bindings, edited through Controls; maps use `DESTINATION:SOURCE` pairs such as `A:RB`. Applies to all four XInput ports. |
| `RECOMP_LOADING_SPINNER` | saved setting or `1` | Show the native loading animation during long gaps between game flips. |
| `MM3_NATIVE_FOG` | saved setting or `1` | Enable native depth fog, skyline masking and camera-aware sky blending. |
| `MM3_HUD_NORTH_MARKER` | saved setting or `0` | `1` shows the game's north letter on the minimap (Mods tab). |
| `MM3_HUD_RACE_ARROW` | saved setting or `0` | `1` shows the game's top-screen arrow toward the next race checkpoint (Mods tab). |
| `RECOMP_NET` | saved setting or `0` | `nat` or `1` enables the System Link adapter and UDP peer transport. |
| `RECOMP_NET_PORT` / `RECOMP_NET_PEERS` | `3075` / empty | Local UDP tunnel port and up to eight comma-separated peer `host:port` entries. |
| `RECOMP_DSP_PROGRAM_DIR` | `game_files/dsp` | Runtime DSP capture cache. Native compilation uses CMake's `XBOXRECOMP_DSP_PROGRAM_DIR`, with the same default. |
| `RECOMP_DSP_INTERPRETER` | `0` | `1` uses the DSP interpreter for reference comparisons, even when native programs are compiled in. |
| `MM3_SHOW_FPS` | saved setting or `1` | Show the FPS counter in the window title; `0` hides it. Can also change live in Display Settings. |
| `RECOMP_KEYBOARD` | off | `1`: keyboard as a pad (arrows drive/navigate, Z X A S = A B X Y, Enter = Start, Backspace = Back; MM3 layout also maps Escape to B/menu back). |
| `RECOMP_KEYBOARD_LAYOUT` | `mm3` | Arrow driving controls; `legacy` restores the original D-pad-only arrows and 1/3 trigger keys. |
| `MM3_PERFORMANCE_PROFILE` | `Balanced` | Shared rendering defaults for launcher and direct executable; `Quality` uses 1440 lines and 8× MSAA, `LowPower` uses 720 lines and a 30 FPS cap. Individual settings override it. |
| `RECOMP_FPS_LIMIT` | saved setting or profile: `60` or `30` | Presentation and emulated-vblank cap, 1–60. Uncapped, invalid, or higher values use the saved setting or profile default. |
| `RECOMP_VSYNC` | `0` | Present sync interval passed to DXGI. |
| `RECOMP_FULLSCREEN` | off | Start fullscreen. |
| `RECOMP_WINDOW_SIZE` | auto | Initial window client size, `WxH`. |
| `RECOMP_ASPECT` | `auto` | Follow the window, or pin `16:10`, `16:9`, `4:3`, or a numeric ratio (0.5–4). Gameplay adapts its view; menus retain their proportions. |
| `MM3_NIGHT_WORLD_LIGHT` | `25` | Night-only world fill, as an integer percentage of authored ambient, diffuse and backlight colors (0-100). Daytime and emissive materials retain their normal strength; source-specific local illumination is independent of this setting. Set `100` with both boosts below at `0` for retail world fill. Requires restart. |
| `MM3_EVEN_LIGHTING` | `0` | Optional readability boost: `1` imposes a 48/255 minimum on world ambient, diffuse and backlight colors (75% unlit texture brightness). Default `0` lets night lighting come mainly from local sources. Native local lights, material alpha, UI and emissive lamps are preserved. Requires restart. |
| `MM3_AMBIENT_LIFT` | `0` | Optional added packed-RGB ambient lift, integer 0-16. Default `0` adds no artificial ambient light. Both lighting boosts are disabled by default. Requires restart. |
| `MM3_LOCAL_LIGHT_GAIN` | `1.3` | Strength of the local-light buffer (headlights, street lamps, tail lights, signals), 1-8; `1` uses the retail composite gain. Overlap uses bounded screen accumulation per pixel, so intersections brighten progressively without changing isolated beams or adding render passes. Daylight, night world fill and emissive lamps are unaffected. Requires restart. |
| `MM3_GLOW_LIFT` | `1` | Lamp glows (car lenses, street lamps) are tested a little nearer the camera, along the line of sight: 12 cm plus 0.3% of the distance, at most 3 m. Their screen position is unchanged, but a glow no longer blinks out behind its own lens rim or lamp housing; buildings and hills still hide it. `0` keeps the retail glow positions. |
| `MM3_ALL_STREET_LAMPS` | `1` | Retail lights only one in N street lamps of a type (half of most Paris and Washington lamps stay dark). With this on, every lamp is lit, glow and pool. The added lamps' pools are chosen by distance from the camera (full within 70 m, easing out by 100 m) and only while fewer than 144 of the shared 256 light records are queued, so at least 112 stay free for car lights; beyond that they still glow. Every lamp of the driver's own car can reclaim a record when the queue is full. With renderer profiling on, `[MM3_LAMPS]` reports added lamps lit/glow-only once a second. `0` restores the retail pattern. Requires the street-lamp lighting option. |
| `MM3_FOV_DEGREES` | unset | Driving horizontal FOV at the display aspect, 60–110 degrees; unset or invalid values retain the existing widescreen FOV. Set with `scripts\play.ps1 -Fov 100`. |
| `RECOMP_SUPERSAMPLE` | `2` | In auto resolution mode, render N×N pixels per window pixel (1–4). |
| `RECOMP_RENDER_SCALE` | profile: `2.0` | Internal height as a multiple of 480 lines (0.5–8): Balanced 960, Quality 1440, LowPower 720; `2.25` = 1080. `auto` follows the window and uses supersampling. |
| `RECOMP_MSAA` | profile: `4` | MSAA sample count (1/2/4/8); 1 disables it. Quality uses 8; LowPower uses 1. Unsupported counts fall back. |
| `RECOMP_ANISO` | `16` | Anisotropic filtering (1–16). |
| `RECOMP_DRAW_DISTANCE` | profile: `2` | Object/pedestrian LOD and blob-shadow distance multiplier, 1–4. LowPower uses 1. Requires restart. |
| `RECOMP_VIEW_DISTANCE` | saved setting or resolved draw distance | Requested city/building visibility and camera far-plane multiplier, 1–4, bounded by the native resident-cell range. Unset values inherit the legacy draw setting; Balanced/Quality start at 2, LowPower at 1. Facade detail zones are capped at 2. Requires restart. |
| `MM3_PARTICLE_MAX_OVERDRAW` | `24` | Particle overlap budget at extended draw distances, 1–255. The Xbox distance retains its original budget of 12. |
| `RECOMP_BUFFERED_ASSET_READS` | `1` | Allow Windows file caching for existing read-only assets on `d:`/`z:` that deny write/delete sharing. All writable, device and other opens keep their original flags. `0` restores unbuffered asset reads; requires restart. Generic runtime default is off. |
| `RECOMP_SHARP_POINT` | `1` | `0`: plain point filtering for fonts and menu art instead of sharp outlines. |
| `RECOMP_PS_SPEC` | `1` | `0`: skip specialised pixel shaders (slow; for debugging). |
| `RECOMP_PS_PREWARM` | `1` | Prepare previously used pixel shader states during startup; `0` disables learning and prewarming. |
| `RECOMP_SHADER_CACHE` | `1` | Save compiled shaders between launches; `0` disables the cache. |
| `RECOMP_SHADER_CACHE_DIR` | built-in | Shader cache folder; defaults to `%LOCALAPPDATA%\xboxrecomp\ShaderCache`. |
| `MM3_SAVE_DIR` | built-in | Explicit save root for UDATA/TDATA, including mounted U:/T: and direct HDD paths. |
| `MM3_UNLOCK_ALL_CARS` | off | Exactly `1` makes every installed car selectable; otherwise earned availability applies to original cars. |
| `MM3_DEBUG_TELEPORT` | off | Enable the teleport pipe (`tools\powershell\Send-MM3Teleport.ps1`). |

### Automation and measurement

| Variable | Effect |
|---|---|
| `RECOMP_PAD_LIVE=<file>` | Read pad input appended to a file, one `button:hold_ms` per line (e.g. `a:150`). |
| `RECOMP_PAD_SCRIPT=<spec>` | Timed scripted pad input (format in `src/usb/usb_gamepad.c`). |
| `RECOMP_PAD_PRESS=<mask>[,period,hold]` | Pulse digital buttons (`0x10` = Start) periodically. |
| `RECOMP_FRAME_CSV=<file>` | One row per flip: QPC time, frame time, pacing time, and successful DXGI Present return time/interval. Summarise flip intervals with `tools\xboxrecomp\tools\bench\frames.py`. |
| `RECOMP_BLACK_FRAME` | On by default; `0` disables. Samples every flip at 64x40 and logs `[BLACK_FRAME]` short dark runs and `[ODD_FRAME]` frames unlike both neighbours, (nearly) unchanged pictures held while the scene moves, and parts of the picture that change for one frame only, plus `[LIGHT_FRAME]` flips whose street-lamp and car-light pass differed from the flips either side (lights missing or extra, the light buffer laid over the frame a different number of times, the blend changed, or the light volumes elsewhere), each with its time, per-frame renderer activity and pictures next to the frame CSV; flashes and light changes are logged a few a minute, and a per-minute count of everything follows even when nothing was found. **F9** on the game window saves the last 2.5 s as one `-black-capture-HHMMSS.bmp` sheet (16 frames a row), a `.csv` with each flip's numbers, light pass and what the screen did with it, then the next 6 flips at 1280x800 as `-black-capture-HHMMSS-sharp.bmp` (up to 12 per run, the first 6 with the sharp pictures; the renderer pauses briefly while it saves). |
| `RECOMP_DISPLAY_STATS` | On by default; `0` disables. Reads the swap chain's frame statistics after every present, plus one GPU event query per present (sent at once) checked at the next one: `[DISPLAY]` logs every hold of two or more refreshes at once and a per-minute summary of refreshes repeated (after a late frame, after an on-time frame the GPU had not finished a refresh later, or after an on-time finished frame, which points at the compositor or display), frames replaced before reaching the screen, the panel's measured refresh rate and where paced presents land in the refresh. |
| `RECOMP_PACE_AFTER_PRESENT` | `1`: restore the previous wait-after-submission order for timing comparisons. |
| `RECOMP_D3D_PROFILE` | Print per-second fps, worst frame and renderer stage costs to stderr. |
| `RECOMP_KERNEL_SUMMARY` | `1`: collect and print periodic kernel-call totals (off during normal play). |
| `RECOMP_CS_COUNTERS` | `1`: count critical-section entries/exits for contention diagnostics (off during normal play). |
| `RECOMP_CS_TRACE_CONTENTION` | `1`: print the first 16 contended lock waits, holder sites, guest stacks and matching acquisitions. Off during normal play to avoid synchronous logging around allocator/audio locks. |
| `RECOMP_HITCH_TRACE` | `1`: report slow command submissions, shader compilation, GPU waits, and file reads with QPC timestamps (off during normal play). |
| `RECOMP_AUDIO_PROFILE` | `1`: bounded one-second audio processing, deadline, condition-wait and output-queue observations, with QPC timestamps; stops after 600 reports. Queue observations are not an underrun counter. Off during normal play. |
| `RECOMP_AUDIO_HIRES_WAIT` | MM3 game profiles default to `1`: use a native Windows high-resolution timer and wake event for audio deadlines. Ordinary commands wake the idle producer without interrupting an unchanged native deadline; pause, exit and backend changes remain urgent. Falls back to the existing condition wait if unavailable. Set `0` to use the condition wait. |
| `MM3_NATIVE_APU_MMIO` | Default `1`: route checked constant-register sound updates directly to the same native APU register backend, avoiding Windows access-violation dispatch. `0` retains the original exception path for comparisons. Explicit `RECOMP_APU_TRACE` also retains the original path. |
| `RECOMP_XA2_STARTUP_BUFFERS` | Default `5`: queue five immutable 256-frame silence buffers immediately before the first sound, adding 26.667 ms of startup headroom against scheduler jitter. `0` disables this padding for comparison. Subsequent sound samples and submission cadence are unchanged. |
| `RECOMP_AUDIO_DSP_HANDOFF` | MM3 game profiles default to `1`: hand the DSP lock to pending control requests at existing execution-quantum boundaries, avoiding unconditional OS yields when no control request is pending. Set `0` to retain the original yield path for comparisons. |
| `RECOMP_COLLISION_PROFILE` | `1`: time native contact processing and count contact-producing calls and collision-kind effects. Reports do not classify wall versus vehicle impacts. Off during normal play. |
| `RECOMP_READBACK_POLL_US` | Default `250`: minimum microseconds between repeated pending GPU-completion queries, `1`–`1000`. `0` disables the interval; invalid values also disable it. The first query and blocking staging-buffer reuse remain immediate. |
| `RECOMP_READBACK_FENCE` | MM3 game profiles default to `1`: prefer native D3D11 fences for GPU readback readiness at the same polling cadence. Unsupported adapters or failed fences use the existing EVENT query. Pixel copying and guest completion ordering are preserved. Set `0` to use EVENT queries. |
| `RECOMP_READBACK_IDLE_WAIT` | Experimental `1`: when the trapped-MMIO renderer is idle with unchanged PUT and GET held on a valid native readback fence, wait on its completion event and a short high-resolution timer. Wakeups never publish guest completion before the normal copy checks. Unsupported or failed waits retain yielding/polling. Default off. |
| `RECOMP_FILE_READ_CPU_TRACE` | `1`: add thread CPU-cycle samples around file reads when hitch tracing is enabled. Full diagnostics enables it; light diagnostics clears it. Slow reads include their actual path and native caching mode after timing ends. CPU cycles are not elapsed milliseconds. |
| `RECOMP_PB_CAPTURE=<prefix>` | Diagnose the first bounded-parser limit by saving fetched command words, parser state, and guest RAM; disabled during normal play. |
| `RECOMP_PRESENT_CAPTURE=<prefix>` | With `<prefix>.flag` containing N, save the next N frames as `<prefix>-*.bmp`. |
| `RECOMP_CAPTURE_SURFACE` | Capture at internal resolution instead of the window. |
| `RECOMP_XA2_PCM_DUMP=<file>` | Dump the mixed audio (s16 stereo 48 kHz). |
| `RECOMP_WATCHDOG_SECS=<n>` | After n seconds of running, report where the game thread is (for hangs). |

`tools\bench\run.ps1 -Exe <exe> -Save <save template> -Profile Balanced -Tag <unique-name>`
runs a frontend and stationary gameplay benchmark. It copies the save, pins the
profile settings, creates a separate shader cache, and records the executable
hash, effective settings, player positions, frame intervals and process CPU.
`tools\bench\summarize.py` reports guest-flip and host Present submission intervals
separately; neither measures when a frame reaches the display. The run rejects
startup/scene failures and waits for complete CSV persistence before stopping.
Omit `-Exe` to use the current build. Optional `-X <x> -Z <z>` requests a verified
teleport; otherwise it measures the saved scene's spawn. Use the same save,
settings and coordinates for comparisons. `-ShaderCacheSeed <directory>` copies
an identical cache snapshot into each run; without it, each cache starts empty.
The frontend scene is selected by timed input and is not verified as a specific
menu. Traffic, scene timing and other system activity still affect comparisons.
`tools\powershell\Test-MM3AiStart.ps1 -Exe <exe> -Save <save template> [-FpsLimit 60]` takes the
same path into the first race and checks every AI car leaves the line forwards (exit 0 = pass).

Compiled shaders are cached automatically. New specialised gameplay shaders
compile in the background. The runtime remembers up to 512 exact shader states
and prepares them during later startups using the existing two background
workers; shaders requested by current draws take priority. The first visit to
a new state still uses the generic shader while compilation finishes. Changes
to shader source or compiler settings invalidate the matching cache entries.
Disabling the shader cache also disables state learning and prewarming.

The renderer selects the high-performance GPU on laptops with integrated and
dedicated graphics. Guest workers and shader compilation run on separate host
threads; rendering keeps the ordering that the game expects. Texture conversion
uses contiguous tile/block copies, and repeated guest locks reuse a thread-local
lookup. Audio DMA also reuses successful address lookups per thread, avoiding
allocator locks while preserving gaps and newly allocated ranges. Optimized
MSVC builds allow automatic inlining in the DSP interpreter; positional-audio
filtering reuses ring indices while retaining the same floating-point operations.
Audio processing skips idle interrupt handling and selects the fractional-delay
tap loop once per channel, preserving exact filter state and output samples.
Optimized MSVC builds request inlining for the checked DSP instruction fetch helper; debug
builds retain its function boundary and memory checks.
`MM3_DSP_AUTO_INLINE=OFF` restores the compiler's default inlining setting for
build comparisons. Normal play skips shared diagnostic counters and redundant
GPU flushes.
Draws retain fixed vertex resources and update vertex shader bindings only when
the pass changes. Repeated texture stages reuse validated sampler-table entries
and use the mip count already computed during texture lookup.
Pending native PCM uses a bounded circular queue instead of shifting the backlog
on every consumption. Its storage remains capped at 64 KiB; geometric growth
preserves queued frames through wrapping and allocation failures. Custom HUD
dials cache their fixed background/tick directions per guest thread and reuse
the needle's trigonometric results without changing geometry or guest state.
Large texture-content buffers use native vector integer arithmetic when both
the CPU and Windows support it. Hash values and byte coverage match the scalar
implementation exactly; small shader keys retain their scalar path.
Specialized shader lookup reuses the previous exact key and hashes changing keys
in four lanes; full key comparisons still resolve collisions and pending shaders
keep their compilation priority. Vertex program and constant versions advance
only after successful GPU uploads. Failed uploads skip the draw and retry, so
unchanged versions cannot leave stale shader data in subsequent frames.
Pixel shader constants reuse their last successful upload only when all bytes,
including dynamic lighting, fog, alpha and texture scales, are identical.
GPU readbacks use one completion query per copy batch before mapping the pixels;
pending queries are spaced by 250 microseconds to reduce repeated driver calls.
The first query in each batch is immediate, and blocking staging-buffer reuse
bypasses the interval. Clock failures fall back to immediate polling; query
failures retain the original per-surface polling path. Guest completion still
waits for the real pixel copies. Pushbuffer packets
and called command lists pause at the published PUT position and resume with
their packet and return state intact. A bounded parser pass cannot acknowledge
commands it has not consumed.
MM3 fences complete from executed semaphore-release commands after pending
readbacks finish. Advancing the game's next-fence counter does not complete
an unsubmitted command.
Both queue-pointer views follow actual command consumption. GPU notifications
become visible after their matching consumed position and readbacks, so a ring
wrap cannot wake the producer with a read pointer from the previous lap.
Native waits retain the game's final queue and GPU-idle checks.
Deferred callbacks and due timers acquire the runtime's DISPATCH exclusion
before being removed from their queues. Cancellation while a guest thread
holds DISPATCH can therefore still find the pending work; the worker cannot
retain a canceled callback while waiting for permission to execute it.
Interrupt synchronization callbacks in the kernel bridge use the same modeled
ISR exclusion and IRQL as interrupt delivery, restoring the caller's IRQL afterward.
Concurrent guest workers publish and remove native handle tokens under a short
table lock, with pointer-sized acquire/release access for resolution. Native
file operations and waits run outside that lock.
Guest CRT reservations and ordinary heap blocks now retain exact,
non-overlapping ownership, including addresses above Xbox RAM. Allocations
skip occupied ranges without claiming alignment gaps or overwriting another
allocator's data.
MM3 enables NT heap reclamation before startup so scene destruction returns
large car-model buffers to the guest heap. Repeated race/menu changes reuse
those buffers instead of exhausting the heap. Facade detail fades also retain
their retail proportions when building view distance is extended.
Facade zones use the city view multiplier capped at 2×, independently of
object detail. Their vertex buffers grow at allocation time with that same scale,
using the same increased counts for their real storage and allocation limits.
The default 2x distance receives four times the original capacity; growth is
capped at four times to preserve the guest's 16-bit vertex offsets. The Xbox
distance and custom buffer capacities retain their original sizes.
The native depth view uses an exponential haze tail above the original knee.
Later eligible depth-tested, depth-writing w-buffer draws use matching foliage
coverage fading after the real depth-as-Y16 haze view was sampled. The final
ordered 2×2 coverage distribution preserves existing alpha-test cutouts and
excludes sky draws without depth writes. Its appearance in motion still needs
manual review across MSAA, fixed render scales and window sizes.
The `nv2a_graphics_quality` GPU cases `haze_depth`, `haze_gates` and
`haze_coverage` pass on the updated renderer and fail on the preserved old
renderer. They check 6,144 depth-view pixels, 1,200 exact sky-depth pixels and
2,592 coverage pixels, including the title's 0.78 knee; these are isolated
rendering checks rather than gameplay motion tests.
The hot city visibility test uses native scratch variables while preserving
the original floating-point operations and guest state. Its differential
regression compares 200,000 complete state and memory cases with the generated
Xbox function: `python tests/mm3_visibility_regression.py` after entering the
build environment. Independent distance ownership, original PAL setters,
the original city-loader clamp, temporary restores, matching facade capacities and the custom-pedestrian
initialization order are checked with
`python tests/mm3_render_distance_regression.py`. A successful custom pedestrian
setting is marked initialized so later object-manager construction cannot scale
it twice. City visibility and camera range retain the native limit of 95% of
one streaming cell: loading and drawing still visit only the camera cell and
its eight neighbors. Extending this limit made distant rows of buildings appear
at cell crossings. Haze now uses the effective far plane relative to the native
range, including cities whose requested range fits without clamping. The default
object detail and 560 m facade detail remain unchanged. The facade fade contract is checked with
`python tests/mm3_facade_fade_regression.py`.
The buffer creation and allocation contract is checked with
`python tests/mm3_facade_capacity_regression.py`.

At startup, the game cache helper compares `Data_hd.zip` contents and retail
creation-time metadata with the disc source. A mismatch invalidates the cached
archive so the game rebuilds it, including a byte-identical copy whose creation
time fails the retail check. Fully matching archives are retained. The helper
does not rewrite archive timestamps. Native file opens preserve the guest's
access and sharing flags and report failures without retrying or closing
another owner's handle. Optional file diagnostics can record sharing-conflict
evidence; an external lock can still cause a disc-error screen.

Collision-call fallbacks preserve the argument block reused by the following
helper, preventing a second stack cleanup from consuming saved registers.
Supported DXT textures upload their original compressed blocks for GPU decoding,
reducing upload traffic and texture memory. Short mip chains retain CPU conversion
and generated mip levels. Native BC decoding follows Direct3D's precision rules
and can differ slightly from the integer CPU decoder.
GPU completion wakes waiting guest threads instead of keeping a CPU core busy;
the same command, notification, and fence completion checks still apply.
Display pacing keeps its one-millisecond spin tail. If the high-resolution
timer fails, it falls back to an ordinary timer or short sleeps; these fallbacks
can wake late on a coarse-resolution system, but avoid a full-frame busy spin.
These reduce CPU work without lowering resolution, filtering, or draw distance.
Frame-time benchmarks exclude screenshot capture, which adds a GPU readback and
disk writes that can create spikes of its own.
Extended draw distance uses a particle overlap budget of 24, instead of raising
the Xbox's 12 to the maximum 255. This bounds smoke, spray and collision effects;
the title's explicit particle settings still take priority. Use
`MM3_PARTICLE_MAX_OVERDRAW=255` to restore the previous effects budget.
Frame pacing waits after composition and immediately before DXGI Present, so
variable game and rendering work does not shift each submission within the
frame interval. Each guest flip waits once, including missing-surface and failed
presentation paths; CPU scanout and movie-only ticks remain unpaced.
CSV `present_ms` records intervals between successful DXGI Present returns for
guest flips, excluding movie-only ticks and failed submissions.
Compare it with `frame_ms` when investigating uneven presentation. Neither
column measures when the monitor actually displays a frame.

### Performance review (2026-10-11)

The first code-review assessment in this audit was **7/10 for implementation, 7/10 for
efficiency and 6/10 for maintainability**. These are engineering judgments,
not benchmark-derived scores. Native recompilation, bounded caches, asynchronous
shader compilation, sleeping GPU waits and focused guest-ABI tests are strengths.
The large compatibility/renderer modules and unresolved gameplay hitches limit
the assessment; this pass does not establish a broad FPS increase.

| Improvement | Verified result | Scope of evidence |
|---|---|---|
| Circular pending PCM queue | 64 to 7 growth allocations; no backlog shifts | 100,000 saturated consume/refill operations: median 61.689 to 3.820 ms over five runs; maximum queue remains 64 KiB |
| Cached custom-dial geometry | 198 to 2 trigonometric calls per warm dial | Bit-identical 2,260,900-byte quad streams across 82 scenarios; guest state preserved |
| Deferred-work claim ordering | Canceled DPCs/timers cannot escape a raised-IRQL owner's cancellation | Both tests fail before and pass after the fix; uncanceled callback arguments and stack recovery preserved |
| Interrupt synchronization | Callback excludes modeled ISRs and restores IRQL | Both regressions fail before and pass after; callback context, Boolean result and guest stack preserved |
| Benchmark runner | Isolated saves/caches, pinned settings, validated gameplay and complete CSV | Launch-failure environment restoration, safe copy boundaries and summary interval checks |

Validation passed 32 renderer tests, 16 DSP tests, 22 kernel-dispatch tests,
four benchmark tests, and the custom-dial/readout regressions. The production
RelWithDebInfo build succeeds. The packaged runtime patch applies to the pinned
toolkit and reproduces the working source after Git newline normalization.

The delivered build completed a 30-second Balanced stationary gameplay window
at the same saved spawn, with isolated empty shader caches in both runs:

| Build | FPS | Frame p99 | Maximum interval | Process CPU | Hitches over 2x median |
|---|---|---|---|---|---|
| Preserved baseline | 59.97 | 17.41 ms | 20.65 ms | 13.66% | 0 |
| Delivered build | 60.00 | 17.39 ms | 21.59 ms | 13.41% | 0 |

These are single capped windows with changing traffic, not proof of a general
FPS or CPU improvement. Three additional baseline startup checks and three
checks with the PCM/HUD/DPC changes, without the graphics experiment, completed
successfully. The delivered build also includes the interrupt synchronization
fix. This limited validation cannot establish that every startup failure is fixed.

A fixed graphics-state retention experiment passed pixel/state regressions and
reduced unchanged-draw calls from 12.59 to 3.32 ns in a microbenchmark, only about
0.0074 ms per 800 draws. Builds containing it repeatedly failed startup in deferred
DirectSound voice work, despite one completed gameplay run. No cache defect was
identified, and its connection to the failure remains uncertain. The experiment
is excluded from the delivered source and build while that issue is investigated.
Its completed gameplay run averaged 59.71 FPS, with a 160.82 ms hitch; the initial
matched baseline averaged 59.97 FPS without that hitch. This is not evidence of
a whole-game performance increase.

The synchronization races above are independently proven; neither establishes
the cause of the DirectSound voice-lifecycle failure. Startup reliability and
gameplay hitches remain priorities. Further performance work should measure draw
setup, uploads and submission separately, then examine surface synchronization,
texture hashing and the GPU vertex-program interpreter in busy driving scenes.
Texture eviction rescans and quadratic HUD grouping remain lower-priority
candidates until profiles show a cost. The follow-up below addresses temporary
mip buffers and the surface synchronization upload-failure path.

Local measurements, preserved before sources/executable, geometry streams and
regression logs are retained under `conformance_tmp/performance-audit-20261011`;
full-game run evidence is under `conformance_tmp/bench`. These generated artifacts
are excluded from source control.

### Performance follow-up (2026-10-11)

The revised code-review assessment is **7.5/10 for implementation, 7.5/10 for
code efficiency and 6/10 for maintainability**. These provisional judgments
credit the verified local changes and failure handling; they are not a measured
whole-game speedup.

This pass preserves the earlier changes and improves three specific paths:

| Weak point | Change | Verified result |
|---|---|---|
| Repeated audio volume exponentiation | Reuse a 4,096-entry attenuation table per APU; rebuild if the audio thread's rounding mode changes | 8,388,608 conversions: median 53.435 to 5.627 ms over five trials (9.5x for this operation); exact float and PCM comparisons for all 65,536 input encodings under all four rounding modes |
| Temporary generated-mip buffers | Reduce each mip in place in the existing decode buffer | Two temporary allocations and a copy removed per applicable upload; 1,400 to zero temporary allocations across 700 updates; isolated 128x128 mip generation 0.0603 to 0.0296 ms |
| Surface shadow committed before upload success | Stage guest bytes and commit them only after successful submission; retry failed resources and constants | 38 surface regressions cover exact pixels, injected failures, same-epoch retry, writes during submission, multiple surface sizes and release, and 1x/4x MSAA |

The audio table adds 16 KiB per APU. Surface synchronization retains one reusable
raw snapshot for changed surfaces, with extra copying on that path; unchanged
surfaces allocate, copy and upload nothing. This is a correctness improvement,
not a claimed synchronization speedup. Full texture-upload timings overlapped
between builds, so the mip microbenchmark does not establish a general upload
or FPS gain. Ten surface failure regressions fail against the preserved original
renderer and pass with the fix.

The benchmark runner now accepts `-Scenario Driving`, which holds the throttle
for the measurement window and verifies that the car moved. It retains the input
script, scenario, executable hash, copied-save identity and start/end positions.
The default remains `Stationary`. Driving uses repeatable inputs, but traffic
and collisions can change the route; compare its results separately from the
stationary window.

Validation passes 72 renderer tests, 17 DSP tests and four benchmark tests. The
RelWithDebInfo game build succeeds, and the refreshed runtime patch reproduces
the working runtime from its pinned toolkit revision. Local source snapshots,
microbenchmarks and test evidence are under
`conformance_tmp/performance-followup-20261011`; gameplay runs use the
`followup-*` directories under `conformance_tmp/bench`.

Matched 30-second Balanced stationary windows used the same saved spawn and
isolated empty shader caches:

| Build | FPS | Frame p99 | Maximum interval | Process CPU | Hitches over 2x median |
|---|---|---|---|---|---|
| Preserved follow-up baseline | 60.00 | 17.53 ms | 17.94 ms | 13.22% | 0 |
| Optimized build | 60.00 | 17.41 ms | 20.70 ms | 13.32% | 0 |

Separate 30-second driving windows used the same save, spawn, profile and
throttle input, again with isolated empty shader caches:

| Build | FPS | Frame p99 | Maximum interval | Process CPU | Hitches over 2x median |
|---|---|---|---|---|---|
| Preserved follow-up baseline | 60.00 | 17.29 ms | 20.28 ms | 12.74% | 0 |
| Optimized build | 59.99 | 17.51 ms | 23.94 ms | 12.77% | 0 |

These single capped windows do not establish an overall FPS, CPU or frame-pacing
improvement. The remaining priorities are the intermittent deferred-voice
startup crash, reproducible long driving hitches, and profiling of draw setup,
surface synchronization, texture hashing and vertex interpretation in busier
scenes. Large renderer and compatibility modules still limit maintainability.
Texture eviction restructuring is deferred until measurements show that the
768 MiB texture budget is exceeded; avoiding scans would otherwise add
bookkeeping to ordinary cache hits without demonstrated benefit.

### Stutter and crash investigation (2026-10-11)

This pass prioritizes deferred callbacks, timer scheduling and thread memory.
These are correctness faults that can create extra work or corrupt state; they
are more urgent than small renderer optimizations.

| Weak point | Repair | Evidence |
|---|---|---|
| Pending DPC completions could outlive the protected object's cleanup | Drain queued completions when a guest lowers IRQL below DISPATCH, while retaining processor exclusion | Lifetime, ordering, nested-callback and boundary regressions fail before and pass after |
| Positive absolute timer deadlines fired immediately | Convert against guest system time; round relative deadlines up safely | A synthetic audio rearm scenario falls from 200 callbacks to one for the same action over two seconds |
| Timer signaling happened after callbacks | Publish the signal before the callback; clear it before publishing a rearmed timer | Rearm and retired-storage regressions pass |
| Canceled DPC entries kept consuming queue slots | Compact the pending FIFO when removing work | 1,000 insert/cancel cycles preserve capacity and callback order |
| The 0x40-byte thread header overlapped guest TLS | Reserve the full 0x280-byte KPCR and initialize each thread's IRQL/DPC state independently | Layout and inherited-state regressions fail before and pass after |

Synchronous callback execution preserves general registers, x87/MMX/XMM state,
direction flags and the host floating-point environment. It leases an independent
guest stack for each batch, with an owned allocation if the worker pool is full.
An initial implementation reused the interrupted stack and caused two startup
timeouts in three trials. A trace identified a USB request callback overwritten
by the return address `0035FBD3`; the regression reproduces that overwrite, and
the isolated-stack version passes three out of three startup trials. Concurrent
pool-exhaustion coverage also rejects sharing a fallback stack between batches.
The normal path performs no guest heap allocation, and an empty queue skips the
stack lease and state snapshot.

The historical DirectSound failure is consistent with completion work observing
a voice after its destruction changed its vtable. The repaired ordering prevents
that tested lifecycle pattern, but the exact intermittent crash was not reproduced
in this pass. Existing skipped USB/null indirect-call warnings occur in both
baseline and repaired runs; their original cause remains unresolved. This is
still a multithreaded approximation of the guest processor, not a complete
single-CPU scheduler.

The final kernel suite passes **43/43** tests; the memory suite passes **5/5**.
The RelWithDebInfo game builds successfully. Evidence, preserved executable/PDB
pairs and unsuccessful diagnostic variants are under
`conformance_tmp/stability-20261011`, `conformance_tmp/dpc-lower-20261011`,
`conformance_tmp/kernel-lifecycle-20261011` and `conformance_tmp/bench`.

Both 90-second Balanced driving runs completed startup and loading from the
same copied save and spawn, using isolated empty shader caches and the same
throttle input:

| Build | FPS | Frame p99 | Maximum interval | Process CPU | Hitches over 2x median |
|---|---|---|---|---|---|
| Preserved stability baseline | 59.80 | 17.76 ms | 68.26 ms | 12.10% | 4 |
| Repaired build | 59.95 | 17.69 ms | 30.79 ms | 15.16% | 0 |

The final run had fewer large spikes, but this single comparison does not
establish causation. Traffic/collisions produced different routes: the baseline
ended around 212 draws per frame and the repaired run around 1,435. The higher
CPU figure therefore cannot isolate callback overhead or establish a CPU
efficiency improvement. The baseline's longest gap included 41 ms between
readback polls, without a correspondingly slow individual readback call.
The repaired run's longest frame includes 16.48 ms inside readback processing;
the existing probe does not separate query, map, pixel copying and unmap costs.
A 56.17 ms menu-to-race transition gap also remains outside the driving window.
These residual stalls need more precise attribution before changing GPU
completion behavior.

A second final-build run, with extra hitch tracing disabled, completed startup,
loading and 120 seconds of driving without an observed crash: **59.96 FPS**,
**17.53 ms p99**, **26.76 ms maximum**, and **zero intervals over twice the
median**, at 14.29% process CPU. Both final-build runs verified movement of the
live player car. These are bounded checks of one saved scene, not exhaustive
coverage of every vehicle, map, mode or intermittent failure.

`tools/bench/run.ps1 -HitchTrace` enables optional renderer, readback, submission,
ACK-worker and DXGI Present timing. Normal runs leave these extra timing probes
disabled. Background shader compilation and a pending readback's elapsed time
must not be mistaken for time actively spent blocking the render thread.

### File and graph-call repairs

The original graph-edge lookup ends through shared tail `0x00144ED9` with
`POP EDI; POP ESI; RET 4`. Its unresolved fallback omitted both pops, which
corrupted the outer route helper's saved registers even though `LEAVE` restored
the final stack pointer. `mm3_runtime_entry_repairs.json` now recovers this exact
tail through the pinned translator. The graph regression compares the real
lifted lookup and its caller with the original XBE instructions.

Non-code indirect-call diagnostics now decode the actual call operand and
record its object/register state. A null target previously matched the first
zero in an unrelated startup table, falsely naming `0x00392834` as its slot.
Diagnostic reads tolerate inaccessible memory, and normal logging remains
limited to 50 reports.

File diagnostics distinguish `NOT_FOUND` and `EOF` from other errors. Host
open errors belong to the calling thread and successful opens clear old errors.
The guest still receives the original NTSTATUS and byte counts. The car cache
and platform-directory misses in the supplied log precede the Shared fallback;
missing `rats.dps` and `ScoreImage.xbx` were also absent from the retail disc.
These probes do not require replacement game assets.

After entering `scripts/enter-env.ps1`, run
`python tests/mm3_graph_edge_regression.py` and
`python tests/mm3_icall_diagnostics_regression.py`. The toolkit's
`kernel-file-sharing` regression checks concurrent error provenance, malformed
open requests, genuine absence and EOF.

### Race map guidance

The blue route in Improved (amber in Stock) follows the native Paris and Washington street splines and
their connected junctions. The brighter leg leads from the car's current road
position to the next checkpoint and recalculates after moving four metres,
changing checkpoints or turning thirty degrees, including after a missed turn.
The route tracks the car's heading independently of the expanded map's orientation.
It prefers forward travel through junctions; checkpoints behind on the same native
street can require a reversal, and dead ends permit recovery when no forward route
exists. Future legs carry their incoming direction through each checkpoint.
Height helps distinguish bridges and roads below them. Both native race scripts
remove passed gates and finish at the final checkpoint; a single remaining gate
still receives guidance, without an invented return to the first gate.

Undercover Work also publishes objectives through the native HUD's two other
marker collections. Guidance copies the live position of every visible stop
after its native getter runs, including taxi pickup and drop-off zones, and
clears hidden or unavailable targets. Hidden stops never join a route, so taxi
jobs, which enable one zone at a time, keep a single leg to the selected stop.
Paper-route style jobs enable every zone together and bind the arrow to the
lowest unvisited one while hiding visited zones. When the selected stop is the
first of the stops visible in its collection, the collection is in mission order
and the course runs through all of them; when it is the last, the collection
is exactly reversed and the course runs from the end back to the front. A
selected stop in the middle of the list gives no sign of the ordering, so it
keeps the single leg rather than an invented course. Only stops of the
selected stop's own collection and native class join the course, and a stop
listed twice counts once. Both map sizes share this behavior. `[RACE_ROUTE]`
logs objective changes with the number of stops and the order used
(`forward`, `reverse` or `single`).

Mission zones (Undercover pickups such as Paris "Go for the gold") count when any
of the car's seven reference points comes within a zone's radius, and only while
the vehicle is placed (player `+0x14A`). `[MM3_ZONE]` lines record how many zones
the player has enabled (a pickup that counts lowers it), each time the car is
inside one, whether the zone check was on, and a zone still enabled 2 s after the
car entered it (`src/mm3_zone_diagnostics.c`, read-only, at most 200 lines a run).

Road geometry is copied after native city loading and cleared on unload.
Unavailable or disconnected paths are omitted. The line uses the same map
projection, rotation, zoom and viewport clipping as the checkpoint markers.
Drawing retains bends within a quarter pixel while combining straight samples,
so expanded maps avoid a native draw submission for every six metres of road.
Improved reuses that graph for gray roads, caching connected street chains and
culling distant chains before drawing. The renderer suppresses only the
title-owned city map base and applies one perspective transform to roads,
guidance and map icons. Speed zoom is smoothed in simulation time. Camera
metadata travels with each queued native city vertex, so later Ctrl updates
cannot change the projection of an older map frame. Packed mask UVs retain
the full map window for the roads, guidance and checkpoints.
After entering `scripts/enter-env.ps1`, run
`python tests/mm3_race_guidance_regression.py` for routing, city-asset, native
spline, projection and submission checks.

### Tree contact response

Tree contacts use the original solid-contact and penetration correction path
for the initial impact. At or above 18 km/h, the struck piece's authored geometry
and transform create a native movable prop with a rigid collision body. The
standing instance is removed only after that body is ready. The detached tree
uses the same physics controller and lifecycle as other moving street props,
so cars can push it and collide with it while it falls and after it lands.
Its initial linear and angular velocity follows the impact; later motion comes
from native gravity and contact impulses. Breaking also removes a little
forward car speed once (up to 8%, capped at 1.5 m/s), preserving the native
lateral and vertical response. Slower contacts remain solid. Invalid or stale
instances and failed body creation keep the standing collision intact.
The retail upward-hop response is bypassed for trees. Native metadata identifies
the selected tree contact; other roadside props retain their original response.
Leaf effects retain their native path. The existing tree sound bank handles
both cities' impact definitions; a positional fallback restores sounds skipped
by the shared collision cooldown, and successful native playback prevents a
duplicate. Repeated contact substeps have a 400 ms per-instance sound cooldown.
Detached trees use their original full mesh and physics transform: the native
upright sprite shortcut is bypassed for these draws. Static trees and unrelated
street props retain their normal detail levels.

The build applies a checked seam at the selected-contact decision in `0x001D137B`;
regeneration that changes or duplicates that boundary fails configuration.
After entering `scripts/enter-env.ps1`, run
`python tests/mm3_tree_contact_regression.py` for native/original-XBE contact
and object-removal comparisons, and
`python tests/mm3_tree_diagnostics_regression.py` for guarded trace contracts.
`python tests/mm3_tree_body_regression.py` executes the native prop constructor,
string handling, authored collider handoff, pose/velocity setters, registration
and failure cleanup. `python tests/mm3_tree_native_render_regression.py` exercises
the native renderer and both mesh queues with tilted transforms, including a
billboard negative control and unchanged nearby props.
`python tests/mm3_tree_audio_regression.py` and
`python tests/mm3_tree_audio_contact_regression.py` check native playback tracking,
both cities' tree metadata, positional fallback, sound cooldowns and guest-state
preservation. These fixtures isolate backend leaves; audible gameplay playback
is checked separately.
`MM3_TREE_TRACE=1` adds bounded object-render, collision-event and tree-contact
reports. Tree-contact reports have their own budget so frequent rendering cannot
hide a physical impact.
`MM3_TREE_TRACE=contacts` skips object-render tracing while keeping physical
contact and collision-event reports for lighter driving captures.

### Building pop-in capture

Every `scripts/play.ps1` launch now writes a unique `logs/play-*.log` and a
matching `.json`. Both output streams are captured concurrently with UTC line
timestamps. Pipe callbacks enqueue records; a dedicated writer batches output
and flushes every 100 ms, keeping the log readable during play. The bounded
queue applies backpressure at capacity and never drops records; exit waits for
the final batch. `-NoConsoleLog` saves every record without echoing it to the
console. The JSON records
the executable hash, launch options, runtime environment, profile defaults,
installed car configuration and exit status. Existing environment overrides
still take priority.

`Play with full diagnostics.cmd` starts an interactive keyboard session with
frame CSV, hitch/parser capture, file probes, texture/building traces and lock
diagnostics. It uses `-NoConsoleLog` and saves a separate
`logs/full-diagnostics-*` folder. After rendering has begun, its observer captures
a dump, bounded guest RAM, registers and matching symbols if frame progress
stops for 15 seconds. This can also trigger during a long loading transition;
the capture is evidence, not proof of a deadlock. It does not close the game.
Full tracing still adds work; use normal `scripts/play.ps1` for ordinary play
and lighter frame/hitch tracing for performance comparisons.

`python tools/diagnostics/analyze_hang.py <capture-directory>` inspects a saved
AMD64 dump with its matching local symbols and writes `native-threads.json`.
It resolves guest registers and x87 state from captured thread-local storage;
stack scans are labeled as candidate return addresses. It never attaches to or
changes a running process.

For the next manual driving check, use **Play with performance diagnostics.cmd**.
It selects `play-diagnostics.ps1 -Light`, retains frame/hitch measurements,
producer-gap timings and the passive hang observer, and removes high-volume tracing. Results go to
`logs/diagnostics-*`. Change one distance control at a time, restart, and compare
the same approach to objects and buildings. Use the full-diagnostics shortcut
when detailed command, texture or building evidence is needed.

After an interactive capture, correlate slow frames with its Play log:

```powershell
tools\venv\Scripts\python.exe tools\diagnostics\analyze_stutter.py logs\full-diagnostics-<run>\frames.csv logs\play-<run>.log --top 5
```

The report separates guest flips from host Present intervals and lists overlapping
hitch phases with log line references. Use `--start-ms` and `--end-ms` with QPC
timestamps to select a known driving segment; the complete capture also includes
menus and loading. Overlapping phases are evidence of timing, not proof of cause.
Use `--json` for a machine-readable report.

For repeated traffic, steering, collision and police stress tests, use the
[native stress harness](tools/bench/STRESS.md). It validates native scene values,
resets an explicit car pose for each window, and records actual actor IDs and
body positions when testing nearby active police. Shader preparation, captures,
spawning and resets occur outside the measured windows.

The D3D11 renderer retains unchanged topology, rasterizer, scissor, blend and
depth/stencil bindings across draws, including auxiliary passes. Its one-second
profile reports issued and avoided pipeline setters. Presentation reuses the
swap-chain buffer/view until a resize or window replacement; allocation failures
retry on a later frame. The DSP worker notifies queue-drain waiters only when
the queue becomes empty. These changes preserve rendering and audio output.
Opt-in hitch traces distinguish presentation resource acquisition, compositing,
capture probing, pacing, CSV writing and housekeeping from the Present call.

The later `play-20261015-182224-716-2bc3afaa.log` capture contains seven
post-load frames above 40 ms, with a maximum of 121.98 ms. The corresponding
game-thread submission gaps last 46–120 ms; the following renderer submissions
take less than 0.6 ms. Five gaps contain no main-thread file reads, and the
other two spend about 0.05 ms reading. These records locate the delay before
submission but do not identify every remaining cause. Full building tracing
performed 283,058 deep samples and roughly two million sampled allocation
observations. Building-cache lookups now examine at most 32 entries, and a
renewable deep-sample budget limits this diagnostic work. Gaps also occur with
little building activity, so these changes are not evidence that every hitch
is eliminated. The log review is saved in
`logs/stutter-log-review-20261015-182224.json`. These are log/source findings;
the performance effect of these changes has not been measured in gameplay.

The 2026-10-15 diagnostic drive confirmed gameplay spikes up to 97.77 ms.
All 18 texture records above 8 ms immediately followed a summary flush: the
old duration calculation included that output. Upload duration now ends before
any diagnostic output; a slow-sink regression reports 1.5 ms instead of the
old 301.5 ms for the same synthetic upload. This corrects attribution, while
broader draw timings still include tracing overhead. The earlier permanent
pushbuffer/CALL hang did not recur in this drive and remains unproven as fixed.

The later intended light-mode manual capture, `logs/play-20261015-152930-318-6e4d70f2.log`,
recorded 12,615 gameplay frames over 218.8 seconds: 57.66 FPS, p99 24.66 ms,
maximum 80.23 ms, with five frames above 50 ms. Those five gaps occurred between
submissions; this does not identify their remaining cause. This capture predates
the separate-distance, haze/foliage and explicit-zero wrap-logging changes.
PowerShell 7 left disabled diagnostic variables present but empty, which the
old presence-only wrap check treated as enabled, producing 20,525 unwanted wrap
records. Light mode now removes those variables, and the native wrap check
rejects empty and `0` values. That capture is therefore not a fully quiet
benchmark or a before/after result for these changes. The earlier permanent
hang remains open pending a reproduction with command/RAM capture.

Loose asset searches log `[FILE_PROBE]` with one example per logical asset
(up to 256 assets) and a count every 1024 misses. A missing cache, platform or
DLC candidate can still resolve from another directory or a ZIP archive;
it does not establish a missing game asset. `RECOMP_FILE_PROBE_TRACE=1`
records every candidate when the full search history is needed. Required
archive failures, failed writes, sharing conflicts and access errors remain
`[FILE_ERROR]` records, with the host path, exact status, thread and time.
`[FILE_CALLER]` adds the issuing guest path, return address and stack pointer.
Crash reports safely capture guest frames, stack words and pause/race context,
including when the memory being inspected is inaccessible.

To check the available fallback locations for a captured or pasted log:

```powershell
tools\venv\Scripts\python.exe tools\diagnostics\summarize_log.py logs\play-<run>.log
```

The audit reads existing files and archive indexes. It reports inaccessible
or locked archives as not verified, and does not count installer backups as
runtime fallbacks. `--json` includes all paths and `--limit` controls examples.
Launch metadata also records explicit error-line counts, the most recent
error and the exit code in hexadecimal. The default launcher picks the newest
executable from the normal build locations and `build/current/RelWithDebInfo`,
so an update can be built separately while a game session remains open.

Normal launches use minimal logging: focused building, texture-stream and
asynchronous-wait tracing is off by default. Use `-TraceBuildings` to opt in
when investigating pop-in; `-NoTraceBuildings` remains available to explicitly
disable that switch. Existing environment overrides still take priority.
Normal launches prevent concurrent use of the same writable saves and cache.
System Link launches can use distinct `-Instance` names, each with separate
saves and preferences. Read-only game archives are shared between instances.

For the repeatable upper-building gap in Paris, Blitz **Sacré Leap**, approaching
**Boulevard Malesherbes**, close the current game when convenient and launch:

```powershell
tools\bin\powershell\pwsh.exe -NoProfile -ExecutionPolicy Bypass -File scripts\play.ps1 -TraceBuildings -Exe out\building-diagnostics\RelWithDebInfo\mm3_recomp.exe
```

`Play with building logs.cmd` prefers this separate diagnostic build when present
and otherwise uses the normal setup build. It uses local portable PowerShell when
installed, with Windows PowerShell as a fallback for a fresh checkout.
Add `-Keyboard` when using the keyboard. Reproduce the approach, drive back and
approach again, then quit normally and keep both files from that run. This build
adds evidence; the cause of this specific gap has not yet been confirmed.

`-TraceBuildings` sets three switches only where they are unset, and prints
their actual values at launch:

- `MM3_BUILDING_TRACE=1`: actual near/mid/far render paths, early visibility and
  auxiliary rejection, camera and facade coordinates, closest distance, floor
  cap, per-cell visibility rejection, and geometry queue/counter changes.
  Both 28-byte vertex streams report their native head, tail and capacity,
  before/after read validity, and middle-detail/upper-floor allocation attempts,
  successes, failures and requested quad totals. Checked guest reads are sampled at
  most ten times per second per facade, with a default limit of 256 deep
  samples per second per render thread. `MM3_BUILDING_SAMPLE_BUDGET=0` restores
  unlimited deep sampling; a positive integer sets the per-second limit.
  Summaries report samples skipped because of this budget, so unobserved
  allocations must not be interpreted as successful or absent. Output retains
  64 change events per second per render thread and summaries each second.
  Allocation failures observed in sampled facades have a separate
  eight-event-per-second budget and suppressed-failure counts. Counter deltas do not prove that
  an asset is loaded or absent.
- `RECOMP_TEXTURE_STREAM_TRACE=1`: real uploads, changed texture content, upload
  duration, mip counts, cache occupancy, eviction totals and creation failures.
  Cache hits stay quiet; an 80-event-per-second budget bounds output.
- `RECOMP_ASYNC_WAIT_TRACE=1`: wait status, handle resolution, caller, stack and
  relevant USB request snapshots. Each wait kind has separate bounded budgets
  for ordinary results and failures, so early polling cannot hide invalid
  handles. It leaves `RECOMP_TITLE_KEVENTS` unchanged; an explicit trace value
  of `0` disables it even when building tracing is enabled.

Diagnostic summaries flush buffered runtime output each second while active;
wait failures and non-code indirect-call reports flush immediately. The default
launcher keeps normal buffering. Focused tracing adds work and is intended for
these reproduction runs, rather than performance comparisons.

The building and indirect-call contracts run with
`python tests/mm3_building_diagnostics_regression.py` and
`python tests/mm3_icall_diagnostics_regression.py` after entering the local build
environment. Launcher coverage is in `tests/test_play_profiles.py`. The toolkit's
`kernel-async-wait-trace-*` cases verify real HLE wait outcomes and unchanged
guest state, memory and host errors with tracing off/on. Its `nv2a_textures`
suite compares actual D3D11 texture pixels and upload retry behavior with
tracing off/on.

The 2026-10-12 12:00 UTC capture contains a confirmed USB request lifetime
failure: XPP's stack event reaches `KeWaitForSingleObject` as an invalid native
handle, the wait returns immediately, and a later USB completion reads reused
request memory. MM3 registers four verified XPP setter/wait return addresses
for inline event handling. This leaves unrelated title-built GPU events on
their existing default path. The focused native USB event fixture covers both
completion orders, timeout/cancel follow-up, reused event storage and invalid
headers. This startup issue has not been linked to the building gap.

That capture's original `zero_queue_delta` values omit the two 28-byte streams
used by middle detail and the near renderer's upper-floor fallback. They cannot
establish missing geometry. The revised trace measures those streams directly;
invalid reads produce an unknown result. Raw-XBE comparisons of middle-detail
and near/fallback geometry, including capacity exhaustion, match the generated
code.

The 12:26 UTC capture confirms buffer exhaustion behind the upper-building gap.
Facade `04AE20F0` passes all 30 cell visibility tests and renders its bottom ten
cells, while all 20 upper cells fail allocation at the opaque stream's 12,288
vertex limit. Later, the same facade remains in the near path and draws those
20 upper cells when space is available. The capture contains 4,263 sampled
allocation failures, and all 40 priority failure events concern that full
opaque stream. The constructor now increases actual buffer storage and its
matching limits before allocation at extended draw distances. A focused native
regression checks the real guest creation path, byte counts, allocation failure
rollback, unchanged stock/custom sizes and successful allocation of the logged
upper-floor workload. The repaired USB waits also complete successfully in this
capture with intact request callbacks.

The fourth pool, the 32-byte facade detail stream (+0x48, taken by
`sub_00162EBA`), had kept its Xbox 1,536 vertices while the doubled facade
zones cover four times the area. Requests past it are refused, so the
buildings whose detail came last lost it, and which ones changed with the
camera. Every stream is drawn as a quad list from its own VB, so the detail
stream now grows with the other three. Every 30 s while buildings are drawn,
`[MM3_DISTANCE] facade streams last 30 s: ...` gives each stream's highest use
in a frame, the detail and far streams' demand (use plus refused vertices) and
their refused requests (`src/mm3_facade_streams.c`). The one-frame flicker of
distant buildings seen at midday (F9 captures, 2026-10-21) was the native fog
falling back to the retail fog for single frames; see below.

### Diagnostics

A launch that bypasses `scripts/play.ps1` (a Steam shortcut, or opening the exe) writes
its log to `logs/direct-<UTC time>.log` with a per-frame CSV and once-a-second renderer
timing; the newest five are kept and `MM3_DIRECT_LOG=0` keeps the console instead.
To see where an already running game spends CPU time, run
**Profile the running game (CPU).cmd** while driving: it samples for a minute and
writes `logs/cpu-profile-<time>`. Read that profile against the same exe build.

The settings below are off unless set; they are for debugging the runtime, not for play.
See the source named for each.

- **Renderer** (`nv2a_d3d11.c`, `nv2a_pb_exec.c`): `RECOMP_D3D_TRACE`, `RECOMP_D3D_DEBUG`,
  `RECOMP_D3D_NO_WRITEBACK`, `RECOMP_D3D_SKIP_CMASK`, `RECOMP_D3D_SKIP_SFACTOR`,
  `RECOMP_PB_SCAN`, `RECOMP_PB_EXEC_VERBOSE`, `RECOMP_PB_REPORT_MS`, `RECOMP_PB_UNHANDLED_ALL`,
  `RECOMP_PB_WRAP_TRACE`, `RECOMP_NV2A_TRACE`, `RECOMP_FRAME_TRACE`, `RECOMP_FRAME_TRACE_METHODS`,
  `RECOMP_FRAME_TRACE_CONSTANTS`, `RECOMP_TEX_DUMP`, `RECOMP_TEX_DUMP_EVERY`, `RECOMP_TEX_STATE`,
  `RECOMP_TEX_GENERIC`, `RECOMP_DXT_VALIDATE`, `RECOMP_FB_DUMP`, `RECOMP_FB_DUMP_FLIPS`, `RECOMP_FB_VA`,
  `RECOMP_FIND_NAN`, `RECOMP_FIND_QUAD`, `RECOMP_VSH_TRACE`, `RECOMP_VSH_DUMP`, `RECOMP_NO_VSH`,
  `RECOMP_NO_COMBINERS`, `RECOMP_RC_TRACE`, `RECOMP_RC_GENERIC`, `RECOMP_BLEND_GENERIC`,
  `RECOMP_SOFT_RASTER`, `RECOMP_RASTER_TEST`, `RECOMP_RASTER_THREADS`, `RECOMP_PEEK`,
  `RECOMP_PEEK_CHAIN`, `RECOMP_POKE`, `RECOMP_VBLANK`, `RECOMP_VIDEO_FLAGS`
- **Video/movies** (`fb_present.c`, `video_pump.c`): `RECOMP_PVIDEO_TRACE`, `RECOMP_PVIDEO_DUMP`,
  `RECOMP_PVIDEO_FRAME_AUDIT`, `RECOMP_FMV_DUMP`, `RECOMP_FMV_HOST`, `RECOMP_KEY_TRACE`
- **Audio** (`apu_*.c`): `RECOMP_APU_TRACE`, `RECOMP_APU_MIXDOWN_ALL`, `RECOMP_DSP_ACK`, `RECOMP_AC97_READY`
- **USB/input** (`ohci.c`, `usb_gamepad.c`): `RECOMP_USB_HC`, `RECOMP_USB_NDP`, `RECOMP_USB_PADS`,
  `RECOMP_USB_PORT`, `RECOMP_USB_STATS`, `RECOMP_USB_TRACE`, `RECOMP_INPUT_DIAG`
- **Kernel/memory/threads** (`kernel_*.c`, `xbox_memory_layout.c`): `XBOX_LOG_LEVEL`,
  `RECOMP_KERNEL_WATCH`, `RECOMP_KERNEL_WATCH_ALL`, `RECOMP_KERNEL_LOG_BUDGET`, `RECOMP_IRQL_TRACE`,
  `RECOMP_CS_MODE`, `RECOMP_CS_WATCH`, `RECOMP_CS_TRACE_CRT`, `RECOMP_WORKERS`, `RECOMP_ASYNC_IO`,
  `RECOMP_CMDLINE`, `RECOMP_CONTIG_TRACE`, `RECOMP_HEAP_WATCH`, `RECOMP_HEAP_LARGE_BREAK`,
  `RECOMP_WATCH`, `RECOMP_WATCH_RAW`, `RECOMP_TRAP_NULL`, `RECOMP_FORCE_RETURN`,
  `RECOMP_THREAD_TLS_TRACE`, `RECOMP_TRACE_ARGS`, `RECOMP_TRACE_BUDGET`,
  `RECOMP_TRACE_DEREF`, `RECOMP_TRACE_PROFILE`
- **Game layer** (`src/`): `MM3_THREAD_MODE=inline`, `MM3_LOG_UNBUFFERED`, `MM3_TRACE_BOOT_STATE`,
  `MM3_ASSET_TRACE`, `MM3_STREAM_TRACE`, `MM3_POST_MOVIE_WATCHDOG`, `MM3_FRONTEND_ICALL_TRACE`,
  `MM3_FRONTEND_WRITE_WATCH`, `MM3_HEAP_FRONTIER`, `MM3_HEAP_FREE_TRACE`, `MM3_HEAP_RETURN_TRACE`,
  `RECOMP_HEAP_LINK_BREAK`, `RECOMP_HEAP_RETURN_BREAK`, `RECOMP_BINK_ALLOC_BREAK`,
  `RECOMP_STREAM_COPY_ORIGINAL`, `RECOMP_UNIMPL_TRAP`, `RECOMP_TRACE_EFD85`

### Build options

CMake options default to a clean build. `MM3_FRONTEND_WATCH`, `MM3_MOVIE_INPUT_TRACE`,
`MM3_TRACY` and `MM3_RECOMP_ABI_CHECK` add diagnostics that cost performance (the first
roughly halves menu frame rate); leave them off for play.

### PC runtime upgrades

The requested collision repair, native launcher with keyboard/controller
bindings and individual Mods, loading animation, eight-region clipping,
camera-aware depth fog, native DSP worker and System Link integration are
implemented. These changes retain the pinned toolkit and are distributed in
`patches/xboxrecomp-mm3.patch` rather than changing its Git revision.
Controls includes saved keyboard/mouse and controller button/trigger bindings,
plus a **Controllers in game** choice from one to four for local split screen.

Paris collision cells are validated once when decoded, with bounded linear work
even when many grid entries share a list. Invalid indexes are repaired before
native road queries use them. Valid cells and cache hits keep their normal path.

The loading animation runs at a limited cadence during long gaps between game
flips and stays out of movies and guest-surface captures. Window clipping implements all eight
inclusive/exclusive regions, including split-screen and scaled views.

Native depth fog replaces the low-resolution fog overlay and horizon strip while
retaining the skyline alpha mask. Its sky blend follows each camera; fog uses
the existing depth samples without changing them. Shaders are prepared during
startup. Failed preparation or an invalid guest submission retains the original
fog/horizon path. Switch it in Mods or use `MM3_NATIVE_FOG=0` for comparisons.
When the fog packet does not fit before the push-buffer threshold, the hook
asks the title's own MakeSpace (0x344770) for room, as the title's command
writers do. It used to fall back to the retail fog for that one frame, so
distant buildings lost their haze and the horizon brightened for a frame, a
flicker in the distance. Every 30 s in a race,
`[MM3_FOG] last 30 s: N native fog passes, M after asking the title for
push-buffer space, F retail fog fallbacks` shows how often that happens; F
should stay 0 (`src/mm3_native_fog.c`).

Audio uses a bounded eight-frame DSP worker queue. The worker sleeps when idle,
uses normal Windows thread priority, and discards obsolete queued work after a
reset. A failed worker creation retains synchronous processing. Existing PCM
buffering, HRTF, resampling and fractional 48 kHz scheduling remain in use.

The audio path was made cheaper without changing a single output sample
(2026-10-21; every change is checked byte for byte against the old code):
- The voice processor, which runs with the sound processor on or off, takes
  about half its former time on an MM3-like load: the HRTF filter computes
  four samples per SSE instruction and stops smoothing settled coefficients,
  ADPCM decoding has no data-dependent branches, voice registers and sample
  pages are translated once instead of per read, pitch and low-pass constants
  are reused while their inputs stay put, and silent bins are not mixed.
- The accurate sound processor replays MM3's race frame about 17% faster
  (the GP; the EP 24%): one 64-bit multiply instead of 12-bit partial
  products, the hot arithmetic helpers inlined, and the mix buffer copied
  directly instead of one memory call per word.
`tests/dsp/vp_frame_bench.c` replays the voice processor (`--emit` for a
byte comparison of two builds, `--bench` for timing); the GP/EP captures under
`conformance_tmp/audio-live-replay` time the sound processor with
`dsp_frame_replay <capture> <repeats>`.

`scripts\setup.ps1` captures the supplied game's GP/EP programs with isolated
saves and muted device output, then compiles their executed instructions to
native C. The interpreter handles unknown or changed instructions. Gameplay can
teach the local `game_files\dsp` cache additional effects paths; run the normal
incremental CMake build again to compile those too. Captures and generated DSP
code are derived game data and are not stored in Git. To refresh them manually:

```powershell
pwsh scripts\capture_mm3_dsp.ps1 -Executable build\RelWithDebInfo\mm3_recomp.exe
cmake --build build --config RelWithDebInfo --target mm3_recomp --parallel 4
```

The capture helper does not change the user's saves or preferences. It validates
complete programs before replacing existing files. Native instruction contracts
include loops, DMA, interrupts and self-modifying code; captured MM3 frames also
replay against the interpreter with exact transfer payloads and final DSP state.

System Link can be enabled in the launcher. Each instance needs its own UDP
port and writable save root. For two copies on this PC, run these commands in
separate terminals, then use Multiplayer > System Link inside the game:

```powershell
pwsh scripts\play.ps1 -Instance Host -Network -NetworkPort 3075 -NetPeers '127.0.0.1:3076'
pwsh scripts\play.ps1 -Instance Client -Network -NetworkPort 3076 -NetPeers '127.0.0.1:3075'
```

Named instances store their saves and preferences under
`%LOCALAPPDATA%\MM3Recomp\instances\<name>`; they start with a separate profile.
For separate PCs, internet connection options and the in-game host/join steps,
see [Play online (System Link)](#play-online-system-link). Packet queues and descriptor walks are
bounded, sockets are nonblocking, and idle workers sleep. Shutdown wakes and
joins the workers before releasing their guest memory.
Stable MAC-derived guest addresses use 24 host bits in the private `10/8`
subnet, reducing duplicate-address risk in larger lobbies compared with the
upstream 200-address pool.

The final build passed a two-instance System Link Paris Cruise on this Windows
PC: discovery, joining, Ready propagation, race start and moving vehicle state
in both directions. The host moved 100.74 metres and the client 73.65 metres;
their settled remote replicas matched at the probe's reported precision.
Evidence is in `logs/pc-upgrades-20261018-systemlink.json`. Separate physical
PCs and network latency/loss conditions were not tested. The live test used
owned process termination; lifecycle contracts separately verify bounded
worker shutdown and handle cleanup.

The captured gameplay DSP replay has 106 transfers. Native execution matches
the current interpreter's transfer payloads and complete final state; payloads
also match the pinned xemu reference. The latter has documented pre-existing
register-state differences, so it is not a full-state match. Three alternating
2,000-repeat trials measured median replay time of 270.754 microseconds native
versus 394.866 interpreted for this GP frame (31.4% less time), and 111.563
versus 151.704 microseconds for the intro EP frame (26.5% less). These are DSP
replay measurements, not whole-game FPS or power measurements.

The 18 October Windows smoke comparison used 30-second stationary Paris Cruise
windows: Clear, Noon, Summer, no cops, Medium traffic/pedestrians, a Lotus
Esprit V8 with Automatic transmission, Balanced rendering and a 60 FPS cap.
Both runs used X=698.755981, Z=1346.96997 and a requested heading of 1.5707963
radians. Captures were taken outside the measured windows.

| Observed result | Before upgrades | Final build |
|---|---:|---:|
| FPS | 60.0 | 60.0 |
| Presentation interval, 99th percentile | 17.81 ms | 17.12 ms |
| Maximum presentation interval | 19.81 ms | 18.86 ms |
| Hitches over twice the median interval | 0 / 1,801 frames | 0 / 1,804 frames |
| Process CPU, normalized across 16 logical processors | 15.04% | 13.48% |

Traffic, pedestrians and settled camera framing differed between runs; these
are observations under the same configured scene, not a causal CPU speedup or
a guarantee for every route. The earlier Career-based driving captures are
streaming/functionality smokes rather than matched performance comparisons.
The final executable's SHA-256 is
`49AF9102B9A20966D060B507F35347332F1D0C3362C9D34654F52A83E9D4D0CF`.

The focused contracts are `tests/mm3_collision_cells_regression.py`,
`tests/mm3_launcher_regression.py`, `tests/mm3_window_clip_regression.py`,
`tests/mm3_native_fog_regression.py` and
`tests/mm3_native_fog_decoder_regression.py`. Renderer GPU fixtures live under
`tools/xboxrecomp/tests/nv2a_loading`, `nv2a_window_clip` and `nv2a_native_fog`.

Controller mapping, capture and configuration contracts use synthetic input;
physical XInput controllers were not attached during validation. An existing
optional car-lighting asset audit still reports missing front-light data in
car 213's `body.cmp`; that asset issue is separate from these runtime upgrades.

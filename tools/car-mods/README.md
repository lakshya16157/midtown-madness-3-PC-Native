# Extra cars and editable stats

Car Workshop supports nine separately selectable extra-car identities.
The original 35 cars remain in the catalog. Each variant has its own name,
ratings and driving setup, with an original MM3 donor providing physics,
interior, sound and collision. A selected prepared model package supplies its
custom body, wheels, textures and livery; a blank package selection uses the
donor visuals. Defaults keep the donor's gearbox, RPM thresholds, engine curve,
turbo, grip, brakes and drag. Only `Mass` and native MM3 `TorqueScale` are tuned
for enjoyable arcade driving. The real car specifications remain available as
reference information.

| Added car | Original MM3 donor |
|---|---|
| Panoz GTR-1 | Dodge Viper |
| McLaren F1 | Koenigsegg |
| Formula 1 | Lotus |
| Ford GT | Koenigsegg |
| McLaren Senna | Koenigsegg |
| McLaren Senna GTR | Koenigsegg |
| Ferrari FXX K | Koenigsegg |
| Koenigsegg Regera | Koenigsegg |
| Lamborghini Aventador | Koenigsegg |

## Vehicle order and story progression

Original cars come first in their retail order. Added cars and downloaded DLC
cars follow together, sorted from slowest to fastest by the game's **TOPSPEED**
menu rating (0 to 9). Equal ratings use vehicle ID as a deterministic tiebreaker.
The two hidden retail bonus cars remain part of the original group. This order
compares the game's displayed speed ratings; it is not a claim about measured
or real-world maximum speed.

Every apply recalculates `Cars/Tweak/SortOrder` without changing physics,
ratings, IDs or model selections. The native vehicle-menu hook also handles
downloaded cars and older installations, so updating the executable fixes their
order without reinstalling the car pack. The preview prints each packaged car's
position and speed rating; original cars use their archive aliases. Changing an
extra car's TOPSPEED rating changes its position on the next apply. Other ratings,
physics edits and speedometer dial limits do not affect its order.

The original 35 cars retain their original `LOCKED`, `HIDDEN`, and `ADMITTANCE`
requirements byte for byte. With normal story progression enabled, the menu
keeps the originals' retail availability groups: available original cars first,
then visible locked original cars, followed by the extra cars. Hidden locked cars
stay hidden, and race-class filters still apply. Custom cars remain available.
Reapplying the pack does not reset saved progress or selected vehicle IDs.

## Visual editor

Double-click [editor.cmd](editor.cmd), or run it from the repository root:

```powershell
tools\car-mods\editor.cmd
```

To open a specific car and tab directly, for example Panoz's model package:

```powershell
tools\car-mods\editor.cmd --car-id 9004 --tab model
```

The searchable sidebar has **Added**, **Original** and **DLC** lists. The original
list reads all 35 retail cars from the pristine HD archive backup, or the game
archive when no backup exists. The DLC list reads installed car packs from the
game folder's `TDATA/4d53002a/$C` directory, including their vehicle names.
Original and DLC stats are read-only references; browsing them does not change
the game or your added-car tuning.

**Compare with**, above the tabs, picks any other added car (its current draft),
original car or DLC car. Its values fill a Compare column on every tab and the
Game stats table; click a Compare value to copy it into the car you are editing.

**Game stats** shows every stored value, including engine, gearing, wheel,
suspension, camera, paint and menu fields, beside the compared car. Search values
or native setting names, and use **Copy selected value** to bring a reference value
into an added-car field. Selecting a stat shows its explanation and its range
across all original and DLC cars. Added-car previews combine your draft values
with the base stats car and donor using the installer's rules; these are
configuration values, not measured driving performance, and imported geometry
can change the actual wheel radius.

The Driving, Gears & RPM and More settings tabs list every value with these columns:

| Column | Shows |
|---|---|
| Your value | Your override. A blank box shows the inherited value in grey; the arrow keys step from it, and ↺ clears the override. |
| Base | The value a blank box uses, from the Base stats car (see below). |
| Compare | The compared car's value. |
| Max allowed | The highest value the installer accepts for your value. |
| Game max | The highest value among the original and DLC cars, naming the car (+N more cars with the same value). ▲ or ▼ marks a value above or below every game car. |

Menu ratings show the same columns with a clickable 0 to 9 bar; small ticks mark
the Base and Compare cars' ratings, and **Copy base ratings** copies the base car's
bars. The gearbox has a per-gear grid with Base, Compare and Game max rows, plus a
text field for pasting all ratios; editing one gear of an inherited gearbox fills
the other gears from the base. A live line checks the RPM order.

Values are checked as you type. An invalid value turns its box red, its tab shows
⚠ and the status line explains the problem; nothing is saved until you choose
Save. Unsaved cars show a dot (•) in the list and an **Unsaved changes** badge, and
the window title starts with `*`. Hover over any label for its tuning tip, accepted
range and game range. Paint, model packages and interior dial controls are
grouped under **Appearance**, which also offers a matching prepared package from
`out/car-models/packages` with **Use it**. **Ctrl+S** saves settings, **Ctrl+F**
searches the car list (or Game stats on that tab), **Ctrl+Tab** switches tabs and
**Ctrl+C** copies the selected Game stats value.

### Base stats

Each added car's MM3 donor is fixed: it supplies the interior, sound, collision,
suspension, cameras and nominal `WheelRadius`. It also supplies exterior visuals
unless a selected prepared model package replaces them. **Base stats** chooses which
original car supplies the driving values instead: engine torque and turbo, RPM
limits, gearbox and final drive, shift times, weight, brakes, grip, drag and
steering speed. Blank values use the base car; your own values always win, and
menu ratings do not change. The first entry is the MM3 donor, which is the
default. Each original car in the list names the DLC cars that drive exactly the
same, because every official DLC car reuses one original car's driving values.
The factory engine profile and a base stats car cannot be combined.
Base stats does not select the on-screen speedometer: choosing Koenigsegg here
changes driving values, while **Appearance → On-screen speedometer (HUD) → HUD
style** selects its native dial separately.

A base car may bring values that are outside the override limits, such as a
bus's 15000 kg mass or its gearbox. Those install as original game data, but the
same numbers cannot be typed in as overrides. Base stats does not change the
donor's nominal `WheelRadius`; your own override can change that setting. The
loaded wheel geometry determines the actual rolling radius, so imported wheels
can change road speeds with the same gearbox.

Command-line tabs are `stats`, `specs`, `driving`, `gears`, `advanced` and `model`
(Appearance). `--car-id 18 --tab stats` opens the Lotus Esprit V8 reference;
installed DLC IDs can be selected the same way. The added car IDs are
9004–9006 and 9009–9014. Removed IDs 9001–9003, 9007 and 9008 remain
unused, so the remaining cars keep their existing identities. Older manifests
containing a removed car must have that entry removed before applying.

The editor and installer use `cars.local.json` beside these tools when that
file exists, otherwise [cars.json](cars.json). The local file is ignored by
Git, so your model selections and tuning can stay on this computer. To start
local settings on a fresh checkout, copy `cars.json` to `cars.local.json`
before opening Workshop. An explicit `--config <path>` always selects that
manifest. The tracked nine-car defaults contain no imported model paths;
downloaded sources and prepared model packages are kept outside the release.

The **Car specs** tab opens first and identifies the actual model: the F1
import is a **1995 McLaren F1 GTR**, the Formula 1 import is a **Ferrari F2007**,
the Ford GT is the **2017 model**, and the Aventador is the **LP700-4**. It shows
manufacturer figures with their weight basis and source links. Unknown figures
stay unknown; calibration notes explain the estimates used by the game.
**Restore shipped tuning**, on the Driving tab, clears custom drivetrain and
handling overrides and the base stats car, restoring the shipped weight and power
tuning on top of the donor's driving setup. It keeps model packages, paint, menu
ratings and dial limits. **Clear all overrides** blanks every driving value so the
car uses its base stats car unchanged. Save & Apply installs either change.
The reset values live in [driving_defaults.py](driving_defaults.py), so saving
custom tuning into a manifest does not change the reset target.

Search cars by name, alias or ID on the left. While searching, each catalog tab
shows matching cars out of its total (for example, **Added (2/9)**); clearing
the search restores the full lists.

Choose a car on the left. Edit its paint colors, menu ratings, physics, gearing
and RPM thresholds. In **Appearance → On-screen speedometer (HUD)**, **HUD style**
selects an original car's native dial family and needle arc, independently of
Base stats. **Dial max km/h** and **Dial max mph** set the on-screen dial range
and calibrate the interior needle. Enter both, or leave both blank to use the
selected style's native HUD artwork and range. Without a HUD style override,
the fixed MM3 donor supplies the family.

Custom maxima redraw the dial with matching markings and needle calibration.
Each unit keeps its native artwork when its maximum equals the selected style's
native printed maximum. For example, Formula 1's current Koenigsegg style with
380 km/h / 240 mph uses the native 380 km/h dial and a custom 240 mph dial;
Koenigsegg's native mph dial ends at 280. Existing saved ranges on other added
cars also control their HUD when applied. Dial maxima do not boost performance.
Workshop does not infer a dial maximum from physics or the menu's Top speed
rating.

**Original HUD speedometers...** opens a selector for the original sports
cars, including Koenigsegg, Viper and Lotus. Edit their dial maxima, then choose
**Keep changes** before saving in the main window. Original-car physics stay
unchanged. The paint preview is a
schematic, not a model editor. **Save** writes the settings; **Save & Apply**
also installs them into the game data. Close the game before applying, and
restart it afterward. Invalid settings are rejected before installation, and
the original data backups are retained.

**Save & Apply** installs a snapshot of the settings that this window just
saved. Editing the JSON file elsewhere while installation is starting cannot
change that apply. Relative model-package paths keep their original meaning.
Edits made in Workshop during installation remain unsaved and need another
Save & Apply; the completion message makes this clear.

The footer badge separates **Unsaved edits** from **Saved · apply needed**.
**Saved · matches last apply** means the file matches the installer's recorded
settings; **Saved · no recorded apply** means that record is unavailable.
During installation it shows **Applying saved settings...**. After success,
**Applied · restart MM3** identifies settings applied in this session. Matching
the recorded settings does not prove game-file integrity; the installer checks
that before applying.

Artwork previews use Pillow and the existing local `tools/cache/decdds` decoder
to read the game's HUD textures. These dependencies are optional: without them,
Workshop still displays the verified native markings as text.

The header shows the fixed MM3 donor separately from the selected exterior
visuals. It updates when the package selection changes and describes the current
Workshop selection; **Save & Apply** installs that selection into the game.

The **Appearance** tab selects a prepared MM3 model package folder containing
`package.json`. Custom visuals are applied after donor cloning each time, so
editing stats and choosing **Save & Apply** keeps the selected body and
textures. **Use MM3 donor model** clears the package selection. Raw MM2 `.ar` and
`.pkg` models require conversion first. A prepared package supplies its own
materials and textures; the schematic paint preview does not show those
materials or guarantee that changing its paint colors recolors the imported
body. The title's contact, suspension, collision and sound systems are retained;
the default extra cars also retain their donor's engine curves and gearing.

The optional per-car `model_package` setting is a folder path relative to
the selected manifest, or an absolute folder path. Existing manifests without this field
continue to use their donor visuals. For example, a Panoz package has:

```json
{
  "format": "mm3-car-model-v1",
  "alias": "Panoz",
  "car_id": 9004,
  "files": [
    {"archive": "loose", "path": "Data/Shared/Cars/Panoz/body.cmp", "source": "body.cmp"},
    {"archive": "Data_hd.zip", "path": "Cars/Panoz/materialSet00.omb", "source": "materialSet00.omb"},
    {"archive": "Data_dvd.zip", "path": "Cars/Panoz/panoz.cdds", "source": "panoz.cdds"}
  ]
}
```

Provide at least `alias` or `car_id`; both, when supplied, must match the
selected car. File paths use forward slashes. Only loose `.cmp` meshes and
archived `.omb` materials or `.cdds` textures in that alias's car/LOD folders
are accepted. Packages may also supply archived menu icons at
`Menu/Gfx/Vehicles/<alias>.cdds` and `<alias>_Small.cdds`, and the associated
`LoadingScreens/Default_<alias>.tga`. The car's selection background is allowed
at the exact loose path `Data/Shared/Cars/<alias>/bg.cdds`. Other loose textures,
other cars' icons, backgrounds and loading screens are
rejected. Source files must remain inside the package and cannot be links
or junctions. Keep the package folder available for future reapplication.
Package assets participate in the normal backup, integrity, rollback and
restore process; restoration removes installed custom visuals without
deleting the prepared source package.

The editor uses the checkout's Python environment and standard Tkinter; it does
not require a web server or additional packages. The code is in
[editor.py](editor.py), and the selected manifest remains editable by hand.

## Command-line installation

From the repository root, with the game closed:

```powershell
tools\venv\Scripts\python.exe tools\car-mods\prepare.py
tools\venv\Scripts\python.exe tools\car-mods\prepare.py --apply
scripts\play.ps1 -Keyboard
```

The first command previews the changes. The second installs the pack into your
extracted `game_files` data. Use a build containing the custom car name hook so
the extra cars have readable names in the vehicle menu.

Edit the selected `cars.local.json` or [cars.json](cars.json), then run
`prepare.py --apply` again to change their
stats. Menu ratings and driving physics are separate: changing a rating only
changes its menu bar. Physics values control engine torque, mass, drag,
brakes and grip. Keep the car identities unchanged; their IDs and display names
are registered by the runtime. Changes take effect after restarting the game.

Menu ratings use the game's original range, 0 through 9. The main physics
settings are:

| Setting | Changes |
|---|---|
| `TorqueScale` | Native MM3 engine power tuning value in the default donor mode. |
| `Mass` | Physical weight. |
| `BrakeTorque` | Braking strength. |
| `LatFrictionFactor` | Cornering grip. |
| `AirKZZ` | Drag, affecting speed at high engine power. |
| `WheelDrive` | 0: front wheels; 1: rear wheels; 2: all wheels. |
| `GearRatioVector` | Reverse ratio, then one through seven forward ratios. |
| `WheelRatio` | Final drive multiplier: a smaller value makes the gears longer. |
| `TimeChangeGear` | Time spent shifting between forward gears, in seconds. |
| `TimeChangeGearFR` | Time spent changing between forward and reverse, in seconds. |

`GearRatioVector` is a JSON array, such as
`[-3.3, 2.5, 1.72, 1.28, 1.0, 0.82, 0.7]`. The first value must be negative;
the forward ratios must be positive and decrease with each gear. The installer
sets the gear count from the array, so do not add a separate `NGears` field.
One through seven forward gears are supported. A smaller first-gear ratio reduces
the torque sent to the wheels at launch; closer neighboring ratios reduce the
RPM drop during an upshift.

The shipped defaults leave all gear ratios, final drive, shift times and RPM
fields blank, so installation copies them from the pristine donor rather than
keeping a previous modified setup. Formula 1 uses the Lotus's five-speed
transmission and normal RPM range. Regera uses the Koenigsegg's six-speed
transmission. Neither enables the custom factory engine profile. This avoids
the earlier 19,000 RPM Formula 1 setup and single-speed Regera setup.

Weight and power are arcade tuning values. Formula 1 is 600 kg with a torque
scale of 420, compared with its Lotus donor's 1,380 kg and 700; lowering both
weight and engine torque gives brisk acceleration without excessive launch
wheelspin. Regera is 1,590 kg with a torque scale of 1,250, compared with its
donor's 1,526 kg and 1,000. These choices are starting points for driving feel,
not measured acceleration or manufacturer torque claims.

Advanced custom RPM edits are still supported. Keep `MinRPM`,
`MinRPMGearChange`, `CutRPM` and `MaxRPM` in that order from lowest to highest;
`CutRPM` can equal `MaxRPM`. Both peak torque RPM values must stay below the
cutoff. The installer rejects conflicting thresholds. For normal driving,
keep these fields blank and tune only weight and engine power.

The sourced reference catalog is [factory_specs.json](factory_specs.json).
Explicit `engine_profile: "factory"` remains an advanced opt-in for old custom
manifests, with a per-car torque curve and shift target written to
`Data/mm3_factory_physics.ini`. No shipped or imported preset enables it.
Factory profile torque values are in Nm, unlike the native MM3 scale. A default
driving reset removes the opt-in and its drivetrain overrides together.

Select the transmission in the game's car setup. Automatic shifts for you;
Manual requires gear changes. With the MM3 keyboard layout, tap **R** to shift
up and **F** to shift down while driving (Xbox **B** and **X** on a controller).
Automatic calculates its upshift speeds from gearing, wheel radius and the
factory shift target, or `MaxTorqueRPM` (`MaxTurboTorqueRPM` for a turbo-equipped
donor) in donor mode; it checks road speed
to change gear. Wheelspin can therefore raise engine RPM before the car reaches
its upshift speed. Changing `MinRPMGearChange` alone does not change these
automatic upshift speeds.

For example, change Panoz's `physics.TorqueScale` from `750` to `800`
for more engine torque, then reapply the pack. Its acceleration menu bar is
controlled separately by `ratings.ACCELERATION`.

The optional per-car `driving_base` names an original car whose driving values
fill every `physics` key you leave out (the editor's **Base stats**). It takes
the car's tweak name in any letter case, for example:

```json
{"id": 9009, "alias": "Ford_GTcar", "donor": "Koenigsegg", "display_name": "Ford GT",
 "driving_base": "Viper", "physics": {"Mass": 1520, "TorqueScale": 1000}, ...}
```

It covers every key in the physics table above except `WheelRadius`, plus the
gear ratios with their gear count and the turbo spin-up that pairs with
`TurboSize`. Other native settings, including suspension, tyres, collision and
cameras, stay with the MM3 `donor`. Nominal `WheelRadius` also stays with the donor
unless you provide a physics override. A selected prepared package can replace
exterior visuals and wheel geometry; the loaded wheels determine the actual
rolling radius. The installer reads the base car from the
pristine retail archive, keeps your `physics` values on top and rechecks the RPM
rules on the result. When the base car omits `LatFrictionFactor` or
`SteeringEndSpeed`, the variant also leaves it at the game default, exactly as the
base car does. `driving_base` cannot name the car's own donor or be combined with
`engine_profile`, and the preview lists it as "driving values from <car>".
**Restore shipped tuning** removes it. Older installers reject manifests that
contain this field.

The optional per-car `hud_style` names an original car alias, for example
`"hud_style": "Koenigsegg"`. It copies only that original car's
`CarInfo.SPEEDOMETERTYPE`, selecting the native HUD family and needle arc.
Omit it to keep the MM3 donor's family. The fixed donor, identity, interior,
sound, collision and driving values stay unchanged; `driving_base` does not
select this style. Original-car custom
HUD ranges in `original_speedometers` remain independent and are not copied.

The optional `speedometer` object contains `max_kmh` and `max_mph`. These set
both the added car's on-screen HUD range and its interior needle calibration.
The HUD uses a custom rendered dial and matching needle for an explicit maximum
that differs from the selected family's native printed maximum. Matching that
native maximum retains the original artwork for that unit. Omitting the object
(both editor fields blank) uses the selected HUD style's native artwork and
range. Allowed ranges are 100–1200 km/h and 60–750 mph; both values are required
for an override. These limits do not predict road top speed or change driving
performance, engine power or the menu's `TOPSPEED` rating.

The optional top-level `original_speedometers` map sets on-screen dial maxima
for original sports cars without changing their physics, paint or interior
camera configuration. Shipped settings use the retail HUD ranges; only explicit
entries override them. For example:

```json
"original_speedometers": {
  "Koenigsegg": {"max_kmh": 1000, "max_mph": 621}
}
```

The installer writes HUD overrides to `Data/mm3_speedometers.ini`. Added-car
interior calibration also stays in the car camera settings. The sidecar is
tracked with the installed assets, rebuilt on reapplication and
removed by restoration. The game reads it at startup, so restart MM3 after
applying changes. An original car with no explicit override uses its retail HUD.

The installer preserves a backup of each affected archive and checks unchanged
resources keep their original bytes and local-header offsets. It never edits
your saved profiles. Cancelling installation, reapplication or restoration with
Ctrl+C restores the previous files and installation metadata. Restore the
pre-install data with:

```powershell
tools\venv\Scripts\python.exe tools\car-mods\prepare.py --restore
```

`--config <path>` selects a different settings file and `--game-dir <path>`
selects another extracted game directory. Reapplication uses the backed-up
originals so tuning does not accumulate. Removing a car from the manifest and
reapplying also removes its installed catalog entries and owned assets; prepared
model packages and pristine archive backups remain available.

To inspect a running native instance without changing game memory:

```powershell
tools\bin\powershell\pwsh.exe -NoProfile -File tools\car-mods\verify.ps1 `
    -ProcessId <pid> -Log <stderr.log> -ExpectedManifest tools\car-mods\cars.json
```

The verifier reports the actual loaded catalog IDs, names and ratings, and
checks the expected extra entries. It also reports the current player's
simulation when a car is active.

Run the editor/installer and native HUD checks without starting the game:

```powershell
. .\scripts\enter-env.ps1
python -m unittest discover -s tests -p "test_car*.py"
python tests/mm3_speedometer_regression.py
python tests/mm3_car_order_regression.py
python tests/mm3_speedometer_dial_regression.py
```

The HUD checks cover both units, drawn numbers, needle calibration, retail
fallbacks, native rendering arguments and preservation of guest registers.

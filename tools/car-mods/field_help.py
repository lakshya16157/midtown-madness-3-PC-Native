"""Short Workshop explanations for manifest and original MM3 tweak fields.

The editable-field rules come from prepare.py and the native gearbox contracts.
The raw-field names come from the retail Cars/Tweak sections. Keep unidentified
native coefficients descriptive: do not promise a tuning direction or SI units
that have not been established by the native consumers.
"""
from __future__ import annotations


LABELS = {
    "TorqueScale": "Engine torque scale", "Mass": "Mass (kg)",
    "BrakeTorque": "Braking strength", "LatFrictionFactor": "Cornering grip",
    "AirKZZ": "Air drag", "WheelDrive": "Driven wheels",
    "WheelRatio": "Final drive", "TimeChangeGear": "Shift time (seconds)",
    "TimeChangeGearFR": "Reverse shift time", "MinRPM": "Idle RPM",
    "MinRPMGearChange": "Minimum shift RPM", "CutRPM": "Cutoff RPM",
    "MaxRPM": "Redline RPM", "MaxTorqueRPM": "Peak torque RPM",
    "MaxTurboTorqueRPM": "Turbo peak torque RPM",
    "TorqueScaleFactor": "Torque multiplier", "HandBrakeTorque": "Handbrake strength",
    "AirKZY": "Vertical air-force scale", "TurboSize": "Donor turbo boost",
    "GearStatusDownGearFactor": "Downshift factor",
    "SteeringEndSpeed": "Steering end speed", "WheelRadius": "Wheel radius",
    "GearRatioVector": "Gear ratios", "PHYSICSID": "Physics ID",
    "TOPSPEED": "Top speed rating",
    "ACCELERATION": "Acceleration rating", "MASS": "Mass rating",
    "HANDLING": "Handling rating", "DURABILITY": "Durability rating",
    "max_kmh": "Dial max km/h", "max_mph": "Dial max mph",
    "model_package": "Model package", "engine_profile": "Engine profile",
    "colors": "Paint colors", "driving_base": "Base stats", "hud_style": "HUD style",
}

PHYSICS_HELP = {
    "Mass": "Weight in kg. Less weight improves acceleration and changes collision and suspension response.",
    "TorqueScale": "Engine torque tuning: higher values add torque but can cause launch wheelspin. Donor mode uses native MM3 units; factory mode uses Nm.",
    "TorqueScaleFactor": "Multiplies engine torque on top of the engine torque scale.",
    "TurboSize": "Native donor turbo boost; zero disables turbo. Factory engine profiles supply their own combined torque curve.",
    "WheelDrive": "Driven wheels: 0 = front, 1 = rear, 2 = all wheels. This changes which tyres receive engine torque.",
    "BrakeTorque": "Brake strength. Higher values apply more braking torque and can lock the wheels more easily.",
    "HandBrakeTorque": "Handbrake strength. Higher values apply more handbrake torque to the rear wheels.",
    "LatFrictionFactor": "Cornering-grip multiplier. Higher values increase lateral tyre force; it is a game tuning value.",
    "AirKZZ": "Forward air resistance. Lower values reduce the drag that opposes high-speed driving.",
    "AirKZY": "Native vertical aerodynamic-force coefficient. It affects the car's response to airflow; its value is not a measured downforce figure.",
    "MaxTorqueRPM": "Peak torque RPM in donor mode. With turbo disabled, it also sets automatic upshift road speeds; keep it below cutoff and at or below 8000 RPM.",
    "MaxTurboTorqueRPM": "Turbo peak torque RPM in donor mode. With turbo enabled, it sets automatic upshift road speeds; keep it below cutoff and at or below 8000 RPM.",
    "MaxRPM": "Engine redline/reference RPM. Keep it at or above cutoff; raising it alone does not add power or change the automatic upshift target.",
    "CutRPM": "RPM where engine power cuts. Keep both peak-torque RPM values below it and keep it at or below redline.",
    "MinRPM": "Engine idle RPM. Keep it below minimum shift RPM, both peak-torque RPM values and cutoff.",
    "TimeChangeGear": "Seconds spent changing between forward gears. Lower values shorten the shift interruption.",
    "TimeChangeGearFR": "Seconds spent switching between forward and reverse.",
    "WheelRatio": "Final drive multiplier. Higher values increase wheel torque and shorten every gear; lower values make the gears longer.",
    "MinRPMGearChange": "Native minimum shift RPM setting. Automatic upshift road speeds come from peak torque RPM, gearing and rolling radius, so this alone does not set the upshift speed.",
    "GearStatusDownGearFactor": "Automatic downshift threshold as a fraction of the previous gear's upshift speed. Higher values trigger downshifts at a higher road speed.",
    "SteeringEndSpeed": "Speed at the end of the native steering-response curve. This affects steering response as road speed changes, not engine performance.",
    "WheelRadius": "Nominal wheel radius in metres; Base stats leaves this MM3 donor value unchanged unless you override it. The loaded wheel model supplies the actual rolling radius, which affects gearing and automatic shift speeds.",
    "GearRatioVector": "Reverse first, then 1–7 forward ratios: reverse must be negative and forward ratios must decrease. Higher ratios add wheel torque and shorten gears; leave blank to use the base gearbox.",
}

RATING_HELP = {
    "TOPSPEED": "Top-speed menu bar, from 0 to 9; it does not change driving speed. Added and DLC cars are ordered by this rating, then vehicle ID.",
    "ACCELERATION": "Acceleration menu bar, from 0 to 9. Actual acceleration comes from the driving physics, not this rating.",
    "MASS": "Weight menu bar, from 0 to 9. Physical weight is the separate Mass value in kg.",
    "HANDLING": "Handling menu bar, from 0 to 9. Actual cornering and steering come from the driving physics.",
    "DURABILITY": "Durability menu bar, from 0 to 9. This rating does not change the car's physical damage settings.",
}

OTHER_HELP = {
    "driving_base": "An original car whose driving values fill blank engine, RPM, gearbox, weight, brake, grip, drag and steering-speed settings. The fixed MM3 donor retains its interior, sound, collision and nominal WheelRadius; a selected model package can replace exterior visuals and wheel geometry. HUD style is selected separately in Appearance. Your own values always win, and menu ratings do not change.",
    "hud_style": "An original car whose native HUD family and needle arc are used for this added car through CarInfo SPEEDOMETERTYPE. Omit it to use the MM3 donor's style. Dial maxima can redraw the dial for a custom range; leave both blank for this style's native artwork and range. Identity and driving physics stay unchanged; original-car HUD overrides remain separate.",
    "model_package": "A converted MM3 model folder containing package.json. It can replace exterior body, wheels and textures while retaining the MM3 donor's interior, sound and collision. Blank uses the donor model; loaded wheel geometry determines the actual rolling radius.",
    "engine_profile": "Factory mode uses a per-car torque curve and fixed shift target, with torque scale in Nm. Donor mode retains the native MM3 engine and turbo behavior.",
    "max_kmh": "Maximum km/h on the added car's on-screen HUD dial and its interior needle calibration. A custom range redraws the dial with a matching needle; a value matching the selected style's native printed maximum keeps its native artwork. Enter both units, or leave both blank for the selected style's native HUD range. It does not increase driving performance. In Original HUD speedometers, this field changes only the original-car HUD override.",
    "max_mph": "Maximum mph on the added car's on-screen HUD dial and its interior needle calibration. A custom range redraws the dial with a matching needle; a value matching the selected style's native printed maximum keeps its native artwork. Enter both units, or leave both blank for the selected style's native HUD range. It does not increase driving performance. In Original HUD speedometers, this field changes only the original-car HUD override.",
    "colors": "One to three paint choices in #rrggbb form. Fixed livery textures in an imported model may not respond to these colors.",
}

_INFO_HELP = {
    "ID": "Native vehicle identity. The game and saved profiles use this number to identify the car.",
    "PHYSICSID": "Native physics identity, separate from the vehicle ID. Installed DLC can reference a retail car's physics identity here; this number is not a performance rating.",
    "COLOR": "A paint choice: color index followed by the native material color values. Imported fixed textures may not recolor.",
    "SKIN": "Native livery choice and its associated material color values.",
    "ICONWIDTH": "Vehicle-menu icon width. This is a presentation setting, not a body or collision dimension.",
    "SPEEDOMETERTYPE": "Selects the native HUD family and needle arc. Added cars copy it through HUD style, separately from Base stats. Explicit dial maxima can redraw that HUD for another range with a matching needle. It does not change actual top speed.",
    "LOCKED": "Marks a vehicle as locked until its progression requirements are met.",
    "HIDDEN": "Marks a vehicle as hidden while its availability requirements are unmet.",
    "ADMITTANCE": "Native story/race availability requirements. Multiple lines can define different ways to unlock the vehicle.",
    "VEHICLECLASS": "Native vehicle class used by race and vehicle-menu filters.",
    "PAINTJOBNAME": "Native paint/livery name used by the game's vehicle resources.",
}

_BODY_HELP = {
    "NWheels": "Number of wheel entries expected by the native car setup.",
    "NGears": "Number of forward gears. Workshop derives this from the forward entries in GearRatioVector when custom ratios are supplied.",
    "NColPoints": "Number of collision points expected by the native car setup.",
    "TyreType": "Native tyre-model selector. Its numeric values are game-specific rather than real tyre categories.",
    "Transmission": "Native transmission setting: 0 = automatic, 1 = manual. The driver's in-game setup can select the transmission.",
    "FrictionLevel": "Native body-friction tuning coefficient. Its scale is specific to MM3.",
    "RollLevel": "Native body-roll tuning coefficient. Its scale is specific to MM3.",
    "bUseSpinTorque": "Enables the native spin-torque behavior controlled by SpinTorqueScaleValue.",
    "SpinTorqueScaleValue": "Scale for native spin torque; relevant when bUseSpinTorque is enabled.",
    "UseSpinTorqueDamping": "Enables damping of the native spin-torque behavior.",
    "SpinTorqueDampingValue": "Damping coefficient for spin torque; relevant when UseSpinTorqueDamping is enabled.",
    "UseAntiSpinSystem": "Enables the native anti-wheelspin system. AntiSpinFactor supplies its tuning value.",
    "AntiSpinFactor": "Native anti-wheelspin tuning coefficient; relevant when UseAntiSpinSystem is enabled.",
    "UseAbsSystem": "Enables the native anti-lock braking system. AbsSystemFactor supplies its tuning value.",
    "AbsSystemFactor": "Native anti-lock braking tuning coefficient; relevant when UseAbsSystem is enabled.",
    "WheelWidth": "Nominal tyre width in metres. This is a physics setup value, not a replacement for the visible wheel model.",
    "vInertia": "Three components of the native body's rotational inertia setup. They affect resistance to rotation around the car's axes.",
    "RollCenterY": "Vertical position of the native roll center. This changes how the body responds to lateral forces.",
    "TurboSpinup": "Native turbo spool-response setting. It is used with the donor turbo system.",
    "WheelScale": "Native wheel simulation scale. Its effect is game-specific; it is not the imported model's physical rolling radius.",
    "TWMaxMom": "Native tyre aligning-moment limit. It belongs to the game's tyre-response model.",
    "TWMomLimStart": "Start parameter for native tyre aligning-moment limiting.",
    "TWMomLimScale": "Scale parameter for native tyre aligning-moment limiting.",
    "TurboIdle": "Native turbo behavior at idle; relevant to the donor turbo system.",
    "TurboWasteGate": "Native turbo wastegate tuning value; relevant to the donor turbo system.",
    "BrakeMinLimit": "Native minimum brake-input threshold. This is an input tuning value rather than maximum braking strength.",
    "ThrottleMinLimit": "Native minimum throttle-input threshold. It also participates in the gearbox's throttle checks.",
    "ThrottleTime": "Native throttle-response timing setting. It controls how throttle input changes over time.",
    "UseEngineBrakeConstant": "Selects the native constant engine-braking behavior controlled by EngineBrakeConstant.",
    "EngineBrakeConstant": "Native constant engine-braking value; relevant when UseEngineBrakeConstant is enabled.",
    "EngineBrakeScale": "Native engine-braking scale. Engine braking slows the car when engine power is reduced.",
    "UseBodyFrictionDamping": "Enables native body-friction damping for wheel and body contact.",
    "BodyFrictionDampingWheelContactFactor": "Native body-friction damping factor during wheel contact; relevant when damping is enabled.",
    "BodyFrictionDampingBodyContactFactor": "Native body-friction damping factor during body contact; relevant when damping is enabled.",
    "BrakeBalance": "Native brake-distribution tuning between the wheels. Its numeric scale is game-specific.",
    "ToeFront": "Native front-wheel toe/alignment tuning. The stored value is a game coefficient, not a confirmed angle in degrees.",
    "ToeRear": "Native rear-wheel toe/alignment tuning. The stored value is a game coefficient, not a confirmed angle in degrees.",
    "CamberFront": "Native front-wheel camber/alignment tuning. The stored value is a game coefficient, not a confirmed angle in degrees.",
    "CamberRear": "Native rear-wheel camber/alignment tuning. The stored value is a game coefficient, not a confirmed angle in degrees.",
    "SteeringMinStartAngle": "Starting value for the native minimum steering-angle curve.",
    "SteeringMaxStartAngle": "Starting value for the native maximum steering-angle curve.",
    "SteeringMinEndAngle": "Ending value for the native minimum steering-angle curve.",
    "SteeringMaxEndAngle": "Ending value for the native maximum steering-angle curve.",
    "SteeringFadeSpeed": "Native rate for steering fade. It is a response coefficient, not the car's road speed.",
    "SteeringReturnSpeed": "Native rate for returning steering toward center.",
    "DamageScale": "Native damage scaling coefficient. This is separate from the vehicle-menu durability rating.",
    "MaxSuspensionDamage": "Native suspension-damage limit. It is part of the damage setup, not suspension stiffness.",
    "RpmTable": "Native engine RPM-table selector. Its numeric value selects game engine data rather than specifying an RPM or manufacturer torque figure.",
}

_WHEEL_HELP = {
    "WheelBasePos": "Wheel mounting position as an X, Y, Z vector in the car's local coordinates, in metres.",
    "SpringK": "Suspension spring stiffness. Higher values give a stiffer spring response.",
    "DampK": "Suspension damping coefficient. Higher values resist suspension movement more strongly.",
    "SpringLength": "Suspension spring/rest length in metres. It contributes to the wheel's suspension position and travel.",
    "RollStiffness": "Suspension roll stiffness. Higher values resist body roll more strongly.",
    "RollK": "Native suspension-roll coefficient. It works with the wheel's roll-stiffness setup.",
    "WheelInertia": "Wheel rotational inertia. Higher values make changes in wheel rotation harder to produce.",
    "MomentXDist": "Native X-axis lever-distance parameter for tyre forces and moments.",
    "MomentZDist": "Native Z-axis lever-distance parameter for tyre forces and moments.",
    "TWStaSteer": "Native static tyre steering-response coefficient. Its scale is specific to MM3.",
    "TWKinSteer": "Native kinetic tyre steering-response coefficient. Its scale is specific to MM3.",
    "TWStaMY": "Native static tyre aligning-moment coefficient. Its scale is specific to MM3.",
    "StaticSlipAngle": "Native static tyre slip-angle parameter. Slip angle describes the difference between wheel heading and travel direction.",
    "SteerTweak": "Native per-wheel steering adjustment. It is part of the tyre-response tuning.",
    "TireRelaxK": "Native tyre-relaxation coefficient. It describes how tyre response develops as the wheel moves.",
    "AlignMomScale": "Scale for the tyre's aligning moment, which tends to align the wheel with its travel direction.",
    "LatPosK": "Native lateral tyre-force curve coefficient. It affects cornering response; its scale is game-specific.",
    "LongPosK": "Native longitudinal tyre-force curve coefficient. It affects drive/brake response; its scale is game-specific.",
    "LatNegK": "Native lateral tyre-force curve coefficient. It works with LatPosK; the scale and tuning direction are game-specific.",
    "LongNeg": "Native longitudinal tyre-force curve coefficient. It works with LongPosK; the scale and tuning direction are game-specific.",
}

_CAMERA_HELP = {
    "HoodPos": "Hood-camera position as an X, Y, Z vector in the car's local coordinates, in metres.",
    "InsidePos": "Interior-camera position as an X, Y, Z vector in the car's local coordinates, in metres.",
    "SpeedometerMaxKmh": "Interior dashboard needle calibration maximum in km/h. Workshop speedometer overrides for added cars also set the on-screen HUD dial to this range. Dial limits do not increase the car's actual top speed.",
    "SpeedometerMaxMph": "Interior dashboard needle calibration maximum in mph. Workshop speedometer overrides for added cars also set the on-screen HUD dial to this range. Dial limits do not increase the car's actual top speed.",
    "fov": "Authored camera field of view. It controls how much of the scene the camera shows.",
    "eye": "Authored camera eye position as an X, Y, Z vector.",
    "lookAt": "Authored point that the camera looks toward, as an X, Y, Z vector.",
    "up": "Authored camera up-direction vector. It establishes camera orientation.",
}
for _view, _name in (("Near", "near"), ("Far", "far")):
    for _stage in ("Start", "End"):
        _CAMERA_HELP[f"Out{_view}{_stage}Y"] = (
            f"{_stage} height of the {_name} chase-camera curve, in metres. Y is the vertical coordinate.")
        _CAMERA_HELP[f"Out{_view}{_stage}Z"] = (
            f"{_stage} distance of the {_name} chase-camera curve, in metres. It positions the camera along the car's local Z axis.")
    _CAMERA_HELP[f"Out{_view}GoalY"] = (
        f"Vertical target position of the {_name} chase camera, in metres. It changes the height the camera aims toward.")


def tip_for(section: str, key: str) -> str:
    """Return context-aware help without confusing menu MASS with physical Mass."""
    area = str(section).strip().casefold()
    if area in {"ratings", "rating", "carinfo"}:
        tip = RATING_HELP.get(key) or _INFO_HELP.get(key)
    elif area in {"physics", "simulation", "carbody", "driving", "gears", "transmission"}:
        tip = PHYSICS_HELP.get(key) or _BODY_HELP.get(key)
    elif area.startswith("carwheel") or area in {"wheel", "wheels", "suspension"}:
        tip = _WHEEL_HELP.get(key)
    elif area.startswith("carcamera") or area in {"camera", "cameras"}:
        tip = _CAMERA_HELP.get(key)
    else:
        tip = OTHER_HELP.get(key)
    if tip:
        return tip
    return f"Underlying MM3 config field {key} in {section}. Its tuning effect and units have not been established here."

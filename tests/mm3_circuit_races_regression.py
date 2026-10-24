"""Circuit races: generated race files, lap logic in the game's Lua 4.0, names.

Run after scripts/enter-env.ps1 (needs cl.exe once, to build Lua 4.0.1 into
tools/cache/lua4 from the official source, sha256-pinned). Checks:

- tools/races/circuits.py produces Config/Setup/Logic for every circuit and a
  menu picture, valid for the game's Config parser and Lua 4.0;
- each race runs through the retail logic_general.lua + logic_checkpoint.lua
  with Scripts/logic_circuit.lua, with the game's functions mocked: every
  player only ever has its next gate armed, out-of-order and repeated gates
  are ignored, opponents get their waypoints before each gate, laps are
  announced, the player finishes after laps x gates and opponents finish;
- the race names in src/mm3_circuit_races.c match circuits.py;
- tools/car-mods/prepare.py adds the races with and without extra cars.
"""
from pathlib import Path
import base64
import hashlib
import importlib.util
import io
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LUA_URL = "https://www.lua.org/ftp/lua-4.0.1.tar.gz"
LUA_SHA256 = "df746e149cf6939e90009d2e540eee918d585b4d1bc6d68b19316a050d484d2a"
LUA_DIR = ROOT / "tools" / "cache" / "lua4"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def lua4():
    exe = LUA_DIR / "lua4.exe"
    if exe.is_file():
        return exe
    if not shutil.which("cl"):
        raise SystemExit("Run scripts/enter-env.ps1 first so cl.exe can build Lua 4.0.1.")
    LUA_DIR.mkdir(parents=True, exist_ok=True)
    data = urllib.request.urlopen(LUA_URL, timeout=60).read()
    if hashlib.sha256(data).hexdigest() != LUA_SHA256:
        raise SystemExit("Lua 4.0.1 download does not match the pinned sha256")
    with tempfile.TemporaryDirectory(prefix="mm3-lua4-") as temporary:
        folder = Path(temporary)
        with tarfile.open(fileobj=io.BytesIO(data)) as archive:
            for member in archive.getmembers():
                if member.isfile() and member.name.startswith("lua-4.0.1/") and ".." not in member.name:
                    target = folder / member.name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.extractfile(member).read())
        src = folder / "lua-4.0.1"
        sources = [p for p in (src / "src").glob("*.c") if p.name != "ltests.c"]
        sources += list((src / "src" / "lib").glob("*.c")) + [src / "src" / "lua" / "lua.c"]
        subprocess.run(["cl", "/nologo", "/O1", "/w", "/I" + str(src / "include"), "/I" + str(src / "src"),
                        "/Fe:" + str(exe), *map(str, sources)], check=True, cwd=folder,
                       stdout=subprocess.DEVNULL)
    return exe


HARNESS = r'''
-- Mocks for the game's natives (Lua 4.0). A global the game really has
-- (REAL_NATIVES: every name the retail scripts call, and the natives checked
-- in the game's registration list) reads as a no-op native returning 0; any
-- other undefined global reads as nil, so calling it fails as in the game.
-- The ones the race logic depends on are modelled.
std_strsub = strsub
function strsub(s, start, count)
	if count == nil then return std_strsub(s, start + 1) end
	return std_strsub(s, start + 1, start + count)
end
function strtoint(s) return tonumber(s) end
calls = {}
function mock_native(name, value)
	if REAL_NATIVES[name] == 1 then return mock_call end
	return nil
end
function mock_call() return 0 end
settagmethod(tag(nil), "getglobal", mock_native)

armed = {}            -- armed[player] = { gate = 1 }
arm_order = {}        -- arm_order[player] = list of names in arming order
queue = {}            -- queue[player] = targets in the order the game keeps them
shown = {}
messages = {}
finished = {}
finish_called = 0
function queue_of(player)
	if queue[player] == nil then queue[player] = {} end
	return queue[player]
end
function queue_find(list, name)
	local i
	for i = 1, getn(list), 1 do
		if list[i] == name then return i end
	end
	return nil
end
function queue_remove(player, name)
	local i = queue_find(queue_of(player), name)
	if i ~= nil then tremove(queue[player], i) end
end
function playerEnableCheckPoint(player, gate)
	if armed[player] == nil then armed[player] = {} end
	if arm_order[player] == nil then arm_order[player] = {} end
	assert(player == "Human-0" or queue_find(queue_of(player), gate) == nil, player .. " queues " .. gate .. " twice")
	if queue_find(queue_of(player), gate) == nil then tinsert(queue[player], gate) end
	armed[player][gate] = 1
	tinsert(arm_order[player], gate)
end
function playerEnableWayPoint(player, point)
	if arm_order[player] == nil then arm_order[player] = {} end
	assert(queue_find(queue_of(player), point) == nil, player .. " queues " .. point .. " twice")
	tinsert(queue[player], point)
	tinsert(arm_order[player], point)
end
function objectShow(name) shown[name] = 1 end
logs = {}
function _ERRORMESSAGE(text)
	tinsert(logs, text)
	if std_strsub(text, 1, 9) ~= "circuit: " then print("LUA ERROR: " .. text) end
end
active = {}
car_pos = {}          -- car_pos["VH-n"] = {x, z}: where the drive puts each car
function objectSetActive(name)
	active[name] = 1
	current = {name = name}
end
disabled = {}
function playerDisableWayPoint(player, point)
	tinsert(disabled, point)
	queue_remove(player, point)
end
function playerDisableCheckPoint(player, gate)
	if armed[player] ~= nil then armed[player][gate] = nil end
	queue_remove(player, gate)
end
-- Objects the race setup makes: {kind, name, rotation, params, position, model}.
created = {}
created_names = {}
current = nil
function objectCreate(kind, name)
	assert(created_names[strlower(name)] == nil, "two objects named " .. name)
	created_names[strlower(name)] = 1
	current = {kind = kind, name = name}
	tinsert(created, current)
end
function objectRotate(value) current.rotation = value end
function objectSetPos(x, y, z)
	current.x = x; current.y = y; current.z = z
	if car_pos[current.name] ~= nil then car_pos[current.name] = {x, z} end
end
function objectGetCurrentPos(name, axis)
	if car_pos[name] == nil then return 0 end
	if axis == 1 then return car_pos[name][1] end
	return car_pos[name][2]
end
function objectCount(count) object_count = count end
models = {}
function objectLoadModel(name)
	tinsert(models, name)
	current.model = name
end
params = {}
function objectSetParam(value, second)
	tinsert(params, value)
	if current ~= nil then current.param = value; current.second = second end
end
speeds = {}
function playerSpeedCoeff(player, value) speeds[player] = value end
timers = {}
function timerCreate(name, delay, callback) timers[name] = callback end
distance = 0
function hudAddDisplayText(sid) tinsert(messages, sid) end
function raceFinish(result, player) if player ~= nil then finished[player] = result end end
function raceGetOpponentCount() return 3 end
function raceGetRace() return RACE end
function raceGetMode() return "checkpoint" end
function gameGetCity() return CITY end
function gameIsMulti() return 0 end
function gameIsOnline() return 0 end
function lobbyGetNumberOfPlayers() return 1 end
function lobbyGetModel() return "lotus" end
function getVehicleGroup() return 2 end
-- The catalog has performance groups 1-4 (none above, as the game returns
-- nothing for an empty group); each call hands out a new car of the group.
car_counter = 0
car_group = {}
function getRandomVehicleFromGroup(group, pick)
	if group == nil or group > 4 then return nil end
	car_counter = car_counter + 1
	car_group["car" .. car_counter] = group
	return "car" .. car_counter
end
ai_variables = {}
function aiSetVariable(name, value) ai_variables[name] = value end
function soundLength() return 1 end
function rand(n) return 0 end
function playerGetScore() return 0 end
function playerGetFinalTime() return 1 end
function objectGetPos() return 0 end
function objectDistance() return distance end
function gameLoadScript(name) dofile(FOLDER .. "/" .. name) end
'''

SETUP = r'''
-- The race setup makes every object, named once, placed and turned; props
-- with their model, zones with their two sizes.
dofile(FOLDER .. "/setup.lua")
onInit()
kinds = {}
for i = 1, getn(created), 1 do
	local o = created[i]
	kinds[o.kind] = (kinds[o.kind] or 0) + 1
	assert(o.x ~= nil and o.y ~= nil and o.z ~= nil and o.rotation ~= nil and o.param ~= nil, o.name .. " not placed")
	if o.kind == 9 then assert(o.model == PROP_MODEL, o.name .. " has no model") end
	if o.kind == 10 then assert(o.second ~= nil and o.model ~= nil, o.name .. " zone sizes or model missing") end
end
assert(kinds[1] == SETUP_COUNTS[1] and (kinds[5] or 0) == SETUP_COUNTS[2], "gates or waypoints")
assert((kinds[9] or 0) == SETUP_COUNTS[3] and kinds[10] == SETUP_COUNTS[4], "props or zones")
assert(object_count >= getn(created), "objectCount " .. object_count .. " for " .. getn(created))
created, models, params, current = {}, {}, {}, nil
'''

DRIVE = r'''
dofile(FOLDER .. "/race.lua")
-- Every other car the mock catalog hands out stands in for a DLC or extra car.
circuit_original_cars = {"lotus", "car2", "car4", "car6", "car8", "car10", "car12";}
onInit()
for i = 0, players - 1, 1 do
	car_pos["VH-" .. i] = {spawn_location[i + 1][1], spawn_location[i + 1][3]}
end
onSync()
initAI()

-- Pass one gate as the engine does (sub_0020DA6F): it calls checkPoint, then
-- disarms the gate and takes it off the player's queue.
function pass(player, gate)
	checkPoint(gate, player)
	armed[player][gate] = nil
	queue_remove(player, gate)
end

function vehicle_of(player)
	return "VH-" .. (tonumber(std_strsub(player, 7)) + 1)
end

-- An opponent drives as the game's AI: to the first of its armed targets,
-- which must be the next one on its path, reaching it (the engine takes a
-- reached waypoint off) - or, every `miss`-th target, driving past it outside
-- its reach, almost to the target after it. A quarter of a second goes by
-- after each target (circuitFollow).
function drive(player, miss)
	local ply_id, legs, misses, head, a, p, q = playerId(player), 0, 0
	local car = vehicle_of(player)
	while getn(queue_of(player)) > 0 do
		legs = legs + 1
		assert(legs < 4 * circuit_route_count * laps + 50, player .. " never empties its queue")
		head = queue[player][1]
		assert(head == "WP-" .. circuit_at[ply_id], player .. " heads for " .. head .. ", not WP-" .. circuit_at[ply_id])
		assert(getn(queue[player]) <= circuit_window, player .. " has " .. getn(queue[player]) .. " targets armed")
		a = circuitTargetPos(circuit_at[ply_id] - 1)
		p = circuitTargetPos(circuit_at[ply_id])
		q = circuitTargetPos(circuit_at[ply_id] + 1)
		if miss ~= nil and mod(legs, miss) == 0 and (q[1] - p[1]) * (p[1] - a[1]) + (q[2] - p[2]) * (p[2] - a[2]) > 0 then
			car_pos[car] = {q[1] + 0.1 * (p[1] - q[1]), q[2] + 0.1 * (p[2] - q[2])}
			misses = misses + 1
			timers["circuit_follow"]()
			assert(queue[player][1] ~= head, player .. " still heads for " .. head .. ", which it drove past")
		else
			car_pos[car] = {p[1], p[2]}
			queue_remove(player, head)
			timers["circuit_follow"]()
		end
	end
	assert(current_target[ply_id] == total, player .. " passed " .. current_target[ply_id])
	assert(circuit_at[ply_id] == circuit_route_count * laps, player .. " stopped at target " .. circuit_at[ply_id])
	assert(armed[player] == nil, player .. " armed a gate")
	return misses
end

-- The player sees every gate but the one just passed.
function all_shown(missing)
	local i
	for i = 0, total_gates - 1, 1 do
		if "CP-" .. i ~= missing then
			assert(armed["Human-0"]["CP-" .. i] == 1, "the player should see CP-" .. i)
		end
	end
end

players_list = {"Human-0", "Comp--0", "Comp--1", "Comp--2"}
total = total_gates * laps
assert(total_targets == total)
-- Opponents have only their next targets armed, in path order.
for p = 3, 4, 1 do
	assert(getn(queue_of(players_list[p])) == circuit_window, players_list[p] .. " arms " .. getn(queue[players_list[p]]))
	for i = 1, circuit_window, 1 do
		assert(queue[players_list[p]][i] == "WP-" .. (i - 1), players_list[p] .. " target " .. i)
	end
end
assert(shown["CP-0"] == 1 and shown["CP-" .. (total_gates - 1)] == 1)
assert(shown[circuit_start_zone] == 1, "start strip not shown")
assert(circuit_block_strips == 0, "blocks are one row of containers only")
-- Opponents race the retail top personalities, aggressive and passive in turn
-- as on the retail race 09 grid, in original cars only.
assert(raceGetRace() == RACE)
named, calm = 0, 0
for i = 1, getn(params), 1 do
	if params[i] == "04_aggressive_sport" then named = named + 1 end
	if params[i] == "04_passive_sport" then calm = calm + 1 end
end
assert(named == 2 and calm == 1, "personalities: " .. named .. " aggressive, " .. calm .. " passive")
for i = 2, getn(models), 1 do
	assert(circuitOriginalCar(models[i]) == 1 or models[i] == "koenigsegg", "an opponent drives " .. models[i])
end
assert(getRandomVehicleFromGroup ~= circuitVehicle and randomCarInGroup ~= circuitPick)
-- The rival (VH-1) races an original car two groups faster than the
-- player's (group 2).
assert(car_group[models[2]] == 4, "the rival's car " .. models[2] .. " is not from group 4")
assert(circuitOriginalCar(models[2]) == 1)
-- Already in the top group: the best car of the player's own group.
assert(car_group[circuitPick(1, 4)] == 4)
-- With the DLC and extra cars allowed the rival takes the faster group's best,
-- whatever car it is.
circuit_all_cars = 1
assert(car_group[circuitPick(1, 2)] == 4 and car_group[circuitPick(1, 3)] == 4)
circuit_all_cars = 0
-- Opponents keep clear of the blocks with a 4 m margin, not retail's 10 m.
assert(ai_variables["rc::m_avoidRadiusAdd"] == circuit_avoid_radius)
-- Catch-up: out of sight behind only, no launch boost, and the speed moves
-- smoothly towards what the gap asks for (no steps that brake a car), once a
-- second at most.
assert(timers["check_ai_speed"] ~= nil and timers["stop_boosted_ai_stop"] == nil)
distance = 1000
checkAISpeed()
for i = 1, 40, 1 do
	last = speeds["Comp--0"] or 1.0
	timers["circuit_trace"]()
	assert(speeds["Comp--0"] - last <= circuit_pace_step + 0.0001, "the speed jumped up")
end
assert(speeds["Comp--0"] == circuit_pace_top, "far behind: " .. speeds["Comp--0"])
distance = (ai_speed_limit[1] + ai_speed_limit[2]) / 2
checkAISpeed()
for i = 1, 40, 1 do timers["circuit_trace"]() end
assert(abs(speeds["Comp--2"] - (1 + circuit_pace_top) / 2) < 0.03, "halfway: " .. speeds["Comp--2"])
distance = 0
checkAISpeed()
for i = 1, 40, 1 do
	last = speeds["Comp--0"]
	timers["circuit_trace"]()
	assert(last - speeds["Comp--0"] <= circuit_pace_ease + 0.0001, "the speed dropped at once")
end
-- It eases off slower than it rises (a fast drop braked on straights).
assert(circuit_pace_ease < circuit_pace_step)
assert(speeds["Comp--0"] == 1.0 and speeds["Comp--1"] == 1.0 and speeds["Comp--2"] == 1.0)
-- An opponent that stops (the cars stay on the grid) gives up the target it
-- heads for after three checks; no car is ever moved.
checkAISpeed()
checkAISpeed()
checkAISpeed()
stuck, put_back, positions, gate_lines = 0, 0, 0, 0
for i = 1, getn(logs), 1 do
	if strfind(logs[i], "circuit: stuck Comp--0 ", 1, 1) then stuck = stuck + 1 end
	if strfind(logs[i], "circuit: reset ", 1, 1) then put_back = put_back + 1 end
end
assert(stuck == 1 and put_back == 0, "stuck " .. stuck .. ", put back " .. put_back)
-- Positions of every car once a second, for tools/races/trace.py.
assert(timers["circuit_trace"] ~= nil)
timers["circuit_trace"]()
for i = 1, getn(logs), 1 do
	if strfind(logs[i], "circuit: pos 1 ", 1, 1) then positions = positions + 1 end
end
assert(positions == 1, "position lines " .. positions)
-- Opponents: everyone but the last finishes ahead of the player.
-- One drives every target, two miss some (a missed gate counts, as passed).
assert(drive("Comp--0") == 0)
missed = drive("Comp--1", 4) + drive("Comp--2", 3)
assert(missed > 0, "no target was missed")
assert(finished["Comp--0"] == 1 and finished["Comp--1"] == 0 and finished["Comp--2"] == 0)
for i = 1, getn(logs), 1 do
	if strfind(logs[i], "circuit: gate Comp--", 1, 1) then gate_lines = gate_lines + 1 end
end
assert(gate_lines == 3 * total, "gate lines " .. gate_lines)
-- The player: every gate shown, lap messages, then the finish after laps x gates.
last = ""
for step = 1, total, 1 do
	local gate = "CP-" .. circuit_next[1]
	all_shown(last)
	assert(armed["Human-0"][gate] == 1)
	if step < total then
		-- A gate out of order changes nothing and comes back with the next pass.
		local wrong = "CP-" .. mod(circuit_next[1] + 1, total_gates)
		pass("Human-0", wrong)
		assert(current_target[1] == step - 1 and "CP-" .. circuit_next[1] == gate)
	end
	pass("Human-0", gate)
	last = gate
end
assert(current_target[1] == total)
-- Over the line the race is over for the script: the retail avoid margin is
-- back and the opponents' timers stop giving them anything.
assert(circuit_over == 1 and ai_variables["rc::m_avoidRadiusAdd"] == circuit_retail_avoid_radius)
armed_before = getn(arm_order["Comp--0"] or {})
timers["circuit_follow"]()
timers["circuit_trace"]()
checkAISpeed()
assert(getn(arm_order["Comp--0"] or {}) == armed_before, "an opponent was armed after the finish")
finals = 0
lap_messages = 0
missed_messages = 0
for i = 1, getn(messages), 1 do
	if messages[i] == "SID_MM3_LAP_FINAL" then finals = finals + 1 end
	if std_strsub(messages[i], 1, 12) == "SID_MM3_LAP_" then lap_messages = lap_messages + 1 end
	if std_strsub(messages[i], 1, 15) == "SID_MM3_MISSED_" then missed_messages = missed_messages + 1 end
end
-- A gate passed out of turn says which one was missed.
if total > 1 then
	assert(missed_messages >= 1, "no missed-checkpoint message")
end
if laps > 1 then
	assert(finals == 1, "final lap shown " .. finals .. " times")
	assert(lap_messages == laps, "lap messages " .. lap_messages)
else
	assert(lap_messages == 0)
end
-- Opponents armed their path in order, every lap's own waypoints, each once:
-- a target armed a second time (the next lap's) froze opponents at the line.
for p = 2, 4, 1 do
	order = arm_order[players_list[p]]
	assert(getn(order) == circuit_route_count * laps, players_list[p] .. " armed " .. getn(order))
	for i = 1, getn(order), 1 do
		assert(order[i] == "WP-" .. (i - 1), players_list[p] .. " armed " .. order[i] .. " as target " .. i)
	end
end
for i = 1, getn(arm_order["Human-0"]), 1 do
	assert(std_strsub(arm_order["Human-0"][i], 1, 3) == "CP-")
end
print("RACE OK " .. CITY .. " " .. RACE .. ": " .. laps .. " laps of " .. total_gates .. " gates")
'''


# Natives the race logic may call beyond those the retail scripts use, each
# found in the game's Lua registration list (sub_0020646F and the base library).
VERIFIED_NATIVES = {
    "playerDisableWayPoint",   # registered at 0x00206A8C, handler 0x001FE208
    "playerDisableCheckPoint",  # registered at 0x002069C1, handler 0x001FE7D7
    "_ERRORMESSAGE",           # Lua 4.0's error handler (sub_0020F684, src/mm3_lua_errors.c)
    "mm3AiAllCars",            # added after sub_0020646F's list by src/mm3_lua_natives.c
}
# Lua 4.0 standard functions the game's own Lua does not have: retail scripts
# never call them, and the game has no format (or floor) in its registrations.
NOT_IN_GAME = {"format", "floor", "getn", "tinsert", "tremove", "strfind", "tonumber", "tostring", "dofile"}
HARNESS_HELPERS = {"mock_native", "mock_call", "queue_of", "queue_find", "queue_remove"}


def real_natives(hd):
    """Every global function the retail scripts call (natives and their own
    functions), plus VERIFIED_NATIVES."""
    names = set()
    for name in hd.namelist():
        if not name.lower().endswith(".lua") or re.search(r"/2\d(Logic|Setup)\.lua$", name) or "circuit" in name:
            continue
        text = "\n".join(line.split("--")[0] for line in hd.read(name).decode("latin1").splitlines())
        names |= set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", text))
    return (names | VERIFIED_NATIVES) - NOT_IN_GAME - {"function", "if", "while", "and", "or", "not", "return"}


def check_mocks(natives):
    """The harness mocks only functions the game really has, and the circuit
    scripts call only those or their own."""
    mocked = set(re.findall(r"^function\s+(\w+)", HARNESS, re.M)) - HARNESS_HELPERS
    assert mocked <= natives, sorted(mocked - natives)
    logic = (ROOT / "tools" / "races" / "logic_circuit.lua").read_text()
    logic = "\n".join(line.split("--")[0] for line in logic.splitlines())
    called = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", logic)) - {"function", "if", "while", "and", "or", "not"}
    own = set(re.findall(r"^function\s+(\w+)", logic, re.M)) | {"circuit_retail_place", "circuit_retail_race",
                                                                 "circuit_retail_vehicle", "circuit_retail_pick"}
    assert called - own <= natives, sorted(called - own - natives)
    print(f"PASS: logic_circuit.lua calls only natives the game registers ({len(called - own)} of them)")


def check_density(circuit):
    """The game's object grid keeps at most 256 objects in a 50 m cell
    (main.gameobjecthandler, no bounds check): stay far below."""
    cells = {}
    for _, x, _, z, _ in circuit["props"]:
        key = (int(x // 50), int(z // 50))
        cells[key] = cells.get(key, 0) + 1
    assert max(cells.values(), default=0) <= 96, (circuit["name"], max(cells.values()))


def check_stacked(design, circuits, circuit, stacks):
    """No gate or opponent target right over or under another road: the game
    takes a target there for either road (Paris 29 opponents under the
    Peripherique's covered section climbed onto the street above), and retail
    races leave such stretches out. Needs the local street graph."""
    city = circuit["city"]
    if city not in stacks:
        known = (ROOT / "tools" / "races" / "data" / f"{city}.json").is_file()
        stacks[city] = design.Stacked(design.roadgraph.load(city)) if known else None
    if stacks[city] is None:
        return 0
    route = circuits.opponent_route(circuit)
    for _, _, x, y, z, gate in route:
        assert not stacks[city].at(x, y, z), (circuit["name"], gate, x, y, z)
    return len(route)


def check_stack_shadow(design, circuits, circuit, graphs):
    """Past a long stacked stretch (Paris 29's covered Peripherique) the game's
    opponents can take the road above for the one they are on; with a target
    just past the stretch they turned round for it and circled the streets
    above. The layout keeps every target design.STACK_SHADOW metres clear of
    such a stretch, and circuits.json is that layout."""
    city = circuit["city"]
    if city not in graphs:
        known = (ROOT / "tools" / "races" / "data" / f"{city}.json").is_file()
        graphs[city] = design.roadgraph.load(city) if known else None
    if graphs[city] is None:
        return 0
    spec = next(s for s in circuits.DESIGNS if s["city"] == city and s["race"] == circuit["race"])
    c = design.layout(graphs[city], city, spec["corners"], spec.get("start_road"), spec["laps"])
    assert len(c.gates) == len(circuit["gates"]) and all(
        abs(a - b) < 0.01 for g, h in zip(c.gates, circuit["gates"]) for a, b in zip(g, h)), \
        (circuit["name"], "circuits.json is not the current layout: run tools/races/design.py write")
    for s in c.targets:
        assert not c.shadowed(s), (circuit["name"], "target past a stacked stretch", s)
    return len(c.stack_shadows)


def pristine(name):
    state = ROOT / "game_files" / ".mm3-car-mods" / name
    return state if state.is_file() else ROOT / "game_files" / "Data" / name


def c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def check_names(circuits):
    """Data/mm3_circuits.ini, read by the game's own parser and locale labels."""
    ini = circuits.names_ini()
    expected = {(circuits.CITY_IDS[c["city"]], c["race"]): (c["name"], circuits.info_text(c))
                for c in circuits.circuits()}
    source = (ROOT / "src" / "mm3_circuit_races.c").read_text()
    labels = source[source.index("#define CIRCUIT_NAME_LENGTH"):source.index("/* sub_001238B7 runs every frame")]
    cases = "\n".join(f"    {{ {city}, {race}, {c_string(name)}, {c_string(info)} }},"
                      for (city, race), (name, info) in sorted(expected.items()))
    counts = {city: max((race - 19 for c, race in expected if c == city), default=0) for city in (1, 2)}
    program = f"""#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {{ CIRCUIT_FIRST_RACE = 20, CIRCUIT_RACES = 10 }};
{labels}
static const struct {{ unsigned city, race; const char *name, *info; }} cases[] = {{
{cases}
}};
int main(void)
{{
    char key[40], name[48], info[304];
    unsigned city, race, i;
    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {{
        snprintf(key, sizeof key, "SID_CHKPT_%u_%u", cases[i].city, cases[i].race + 1);
        assert(mm3_circuit_label(key) && !strcmp(mm3_circuit_label(key), cases[i].name));
        snprintf(key, sizeof key, "SID_C%uR%uQB", cases[i].city, cases[i].race + 1);
        assert(mm3_circuit_label(key) && !strcmp(mm3_circuit_label(key), cases[i].info));
    }}
    assert(circuit_count(0) == {counts[1]} && circuit_count(1) == {counts[2]});
    assert(!mm3_circuit_label("SID_CHKPT_2_20") && !mm3_circuit_label("SID_C2R10QB"));
    assert(!mm3_circuit_label("SID_C2R31QB") && !mm3_circuit_label("SID_C3R21QB") && !mm3_circuit_label("SID_C2R21Q"));
    assert(!strcmp(mm3_circuit_label("SID_MM3_CIRCUIT"), "Circuit"));
    assert(!strcmp(mm3_circuit_label("SID_MM3_LAP_2_5"), "LAP 2 OF 5"));
    assert(!mm3_circuit_parse_line("3 20 X\\n", &city, &race, name, sizeof name, info, sizeof info));
    assert(!mm3_circuit_parse_line("1 30 X\\n", &city, &race, name, sizeof name, info, sizeof info));
    assert(!mm3_circuit_parse_line("1 20 \\n", &city, &race, name, sizeof name, info, sizeof info));
    assert(!mm3_circuit_parse_line("2 21 Name\\t\\n", &city, &race, name, sizeof name, info, sizeof info));
    assert(!mm3_circuit_parse_line("2 21 \\tInfo\\n", &city, &race, name, sizeof name, info, sizeof info));
    assert(!mm3_circuit_parse_line("2 21 Name\\tBad\\x01\\n", &city, &race, name, sizeof name, info, sizeof info));
    assert(mm3_circuit_parse_line("2 21 Name\\n", &city, &race, name, sizeof name, info, sizeof info));
    assert(!strcmp(name, "Name") && !info[0]);
    assert(mm3_circuit_parse_line("2 21 Name\\tSome info\\r\\n", &city, &race, name, sizeof name, info, sizeof info));
    assert(city == 2 && race == 21 && !strcmp(name, "Name") && !strcmp(info, "Some info"));
    mm3_circuit_set_mode(1);
    puts("names ok");
    return 0;
}}
"""
    with tempfile.TemporaryDirectory(prefix="mm3-circuit-names-") as temporary:
        folder = Path(temporary)
        (folder / "game_files" / "Data").mkdir(parents=True)
        (folder / "game_files" / "Data" / "mm3_circuits.ini").write_bytes(ini)
        (folder / "names.c").write_text(program)
        subprocess.run(["cl", "/nologo", "/W4", "/WX", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
                        "/Fe:" + str(folder / "names.exe"), str(folder / "names.c")],
                       check=True, cwd=folder, stdout=subprocess.DEVNULL)
        subprocess.run([str(folder / "names.exe")], check=True, cwd=folder, stdout=subprocess.DEVNULL)
    print(f"PASS: {len(expected)} circuit names and info texts in Data/mm3_circuits.ini read as the game reads them")


def check_prepare(prepare_module, circuits):
    originals = {relative: pristine(Path(relative).name).read_bytes() for relative in prepare_module.ARCHIVES}
    with tempfile.TemporaryDirectory(prefix="mm3-circuit-plan-") as temporary:
        game = Path(temporary)
        output, counts = prepare_module.build_plan(game, originals, {"version": 1, "cars": []})
    assert counts["races"] == len(circuits.circuits()), counts
    assert output["Data/mm3_circuits.ini"] == circuits.names_ini()
    with zipfile.ZipFile(io.BytesIO(output["Data/Data_hd.zip"])) as hd, \
            zipfile.ZipFile(io.BytesIO(output["Data/Data_dvd.zip"])) as dvd:
        for circuit in circuits.circuits():
            base = f"Scripts/{circuit['city']}/Checkpoint/{circuit['race']:02d}"
            for suffix in ("Config", "Setup.lua", "Logic.lua"):
                assert hd.getinfo(base + suffix).compress_type == zipfile.ZIP_STORED
            # The race list shows the circuit's own route (circuits.icon_cdds),
            # a CDDS picture like the retail ones, not a retail race's.
            picture = dvd.read(f"Menu/Gfx/races/{circuit['city']}/Checkpoint/{circuit['race']:02d}.cdds")
            assert picture[:4] == b"\xc8\x44\x0f\x99" and picture == base64.b64decode(circuit["icon"]), circuit["name"]
            assert all(picture != dvd.read(f"Menu/Gfx/races/{circuit['city']}/Checkpoint/{n:02d}.cdds")
                       for n in range(10)), circuit["name"]
        assert "Scripts/logic_circuit.lua" in hd.namelist()
        # Opponents race the retail personalities: none are added.
        assert not [name for name in hd.namelist() if "mm3c" in name.lower()]
        assert hd.testzip() is None and dvd.testzip() is None
    print("PASS: prepare.py adds every circuit race, its picture and logic_circuit.lua without extra cars")


def main():
    circuits = load("mm3_circuits", ROOT / "tools" / "races" / "circuits.py")
    design = load("mm3_design", ROOT / "tools" / "races" / "design.py")
    prepare_module = load("mm3_prepare", ROOT / "tools" / "car-mods" / "prepare.py")
    exe = lua4()
    with zipfile.ZipFile(pristine("Data_hd.zip")) as hd, zipfile.ZipFile(pristine("Data_dvd.zip")) as dvd:
        entries = circuits.race_entries(hd, dvd)
        retail = {name: hd.read(f"Scripts/{name}") for name in ("logic_general.lua", "logic_checkpoint.lua")}
        natives = real_natives(hd)
    check_mocks(natives)
    stacks, graphs, targets, shadows = {}, {}, 0, 0
    for circuit in circuits.circuits():
        targets += check_stacked(design, circuits, circuit, stacks)
        shadows += check_stack_shadow(design, circuits, circuit, graphs)
    print(f"PASS: {targets} opponent targets a lap, none right over or under another road, "
          f"none just past {shadows} long stacked stretch(es)")
    files = entries["Data_hd.zip"]
    for name, data in files.items():
        # The game skips a script of 32 KB or more (sub_00214FC5).
        if name.endswith(".lua"):
            assert len(data) <= circuits.SCRIPT_BUDGET < circuits.SCRIPT_LIMIT, (name, len(data))
    for circuit in circuits.circuits():
        base = f"Scripts/{circuit['city']}/Checkpoint/{circuit['race']:02d}"
        config = files[base + "Config"].decode("ascii")
        keys = [line.split()[0] for line in config.splitlines() if line.strip()]
        assert keys == ["Season", "TimeOfDay", "Weather", "AmbientDensity", "CopsCount"], keys
        waypoints = len(circuits.opponent_route(circuit)) * circuit["laps"]
        counts = (len(circuit["gates"]), waypoints, len(circuit["props"]), 1)
        assert counts[0] + counts[1] + counts[2] + counts[3] - 1 <= design.OBJECT_BUDGET, circuit["name"]
        assert counts[2] <= design.PROP_BUDGET, circuit["name"]
        check_density(circuit)
        with tempfile.TemporaryDirectory(prefix="mm3-circuit-lua-") as temporary:
            folder = Path(temporary)
            for name, data in retail.items():
                (folder / name).write_bytes(data)
            (folder / "logic_circuit.lua").write_bytes(files["Scripts/logic_circuit.lua"])
            (folder / "race.lua").write_bytes(files[base + "Logic.lua"])
            (folder / "setup.lua").write_bytes(files[base + "Setup.lua"])
            prelude = (f'FOLDER = "{folder.as_posix()}"\nCITY = "{circuit["city"]}"\n'
                       f'RACE = "{circuit["race"]:02d}"\n'
                       f'PROP_MODEL = "{design.CONTAINERS[circuit["city"]][0]}"\n'
                       "SETUP_COUNTS = {" + ", ".join(map(str, counts)) + "}\n"
                       "REAL_NATIVES = {" + "".join(f'["{n}"] = 1, ' for n in sorted(natives)) + "}\n")
            (folder / "harness.lua").write_text(prelude + HARNESS + SETUP + DRIVE)
            result = subprocess.run([str(exe), str(folder / "harness.lua")], capture_output=True, text=True, cwd=folder)
            if result.returncode or "RACE OK" not in result.stdout:
                raise SystemExit(f"{circuit['name']}: Lua run failed\n{result.stdout}\n{result.stderr}")
            print(result.stdout.strip())
    check_names(circuits)
    check_prepare(prepare_module, circuits)
    print(f"PASS: {len(circuits.circuits())} circuit races generate, load in Lua 4.0 and run strict lapped gates")


if __name__ == "__main__":
    main()

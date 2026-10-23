-- ********************************************************************************
-- * Midtown Madness 3 PC: circuit races (tools/races/circuits.py)                *
-- * Loaded after logic_checkpoint.lua. A circuit is a Checkpoint race whose      *
-- * gates CP-0 .. CP-(total_gates - 1) are driven in order, laps times; the last *
-- * gate is the start/finish line. Retail checkpoint gates count in any order:   *
-- * here only the expected gate counts. As in Midtown Madness 1 and 2 the player *
-- * sees every gate armed, and opponents follow a path of points along the       *
-- * course (one on every gate) with only their next few armed. Lua 4.0.          *
-- ********************************************************************************

total_gates = 1						-- Gates in one lap (set by the race)
laps = 1							-- Laps (set by the race)
circuit_next = {}					-- Each player's next gate, 0 .. total_gates - 1
circuit_lap = {}					-- Each player's current lap, 1 .. laps
circuit_shown = {}					-- circuit_shown[gate + 1] == 1: armed for the player

circuit_tier = 0					-- Opponent difficulty, 0 (race 20) .. 9 (race 29) (set by the race)
circuit_start_zone = ""				-- The green strip at the start/finish line (set by the race)
circuit_block_strips = 0			-- Red strips ZN-0.. in front of the blocked roads (set by the race)
circuit_original_cars = {}			-- Lower-case names of the original cars (set by the race)
circuit_watch = {}					-- Each opponent's {x, z, checks without moving}
circuit_trace_tick = 0				-- Seconds of position lines (circuitTrace)

-- Opponents race like the retail race 09: the top personality tier
-- (04_aggressive_*) and the best car of the player's group - an original car
-- only: retail driving does not handle the DLC or extra cars. The rival (the
-- first opponent) races the player's own car when that is an original one.
circuit_retail_place = placePlayerVehicle
circuit_retail_race = raceGetRace
circuit_retail_vehicle = getRandomVehicleFromGroup
circuit_retail_pick = randomCarInGroup

function circuitRaceCode()
	return "09"
end

function circuitOriginalCar(car)
	local i = 1
	car = strlower(car)
	if strsub(car, strlen(car) - 2, 1) == "." then
		car = strsub(car, 0, strlen(car) - 2)
	end
	while circuit_original_cars[i] ~= nil do
		if circuit_original_cars[i] == car then
			return 1
		end
		i = i + 1
	end
	return 0
end

function circuitVehicle(group, pick)
	local car
	local tries = 0
	if pick == nil then
		car = circuit_retail_vehicle(group)
	else
		car = circuit_retail_vehicle(group, pick)
	end
	while circuitOriginalCar(car) == 0 and tries < 32 do
		car = circuit_retail_vehicle(group)
		tries = tries + 1
	end
	if circuitOriginalCar(car) == 0 then
		car = "koenigsegg"				-- The fastest original car
	end
	return car
end

function circuitPick(player_id, player_group)
	local car
	if player_id == 1 then
		car = strlower(lobbyGetModel(1))
		if strsub(car, strlen(car) - 2, 1) == "." then
			car = strsub(car, 0, strlen(car) - 2)
		end
		if circuitOriginalCar(car) == 1 then
			return car
		end
	end
	return circuit_retail_pick(player_id, player_group)
end

function placePlayerVehicle()
	raceGetRace = circuitRaceCode
	getRandomVehicleFromGroup = circuitVehicle
	randomCarInGroup = circuitPick
	circuit_retail_place()
	randomCarInGroup = circuit_retail_pick
	getRandomVehicleFromGroup = circuit_retail_vehicle
	raceGetRace = circuit_retail_race
end

-- Catch-up: an opponent behind the player and out of sight races faster; close
-- by or ahead it races at its car's own speed, which its braking points are
-- planned for. Retail races switch the speed in steps every few seconds (1.0,
-- 1.25, 1.5), so an opponent coming into the near band brakes on a straight;
-- here the speed follows the gap smoothly and changes slowly (circuitPace).
-- Race by race the catch-up starts closer behind. No launch boost: it sent
-- opponents into the first corner too fast.
ai_speed_from_race = 99				-- The retail schedule (by race digit) stays off
ai_speed_delay = 3
circuit_pace_top = 1.3				-- The most an opponent's speed is raised
circuit_pace_step = 0.08			-- The most it changes, once a second
circuit_pace = {}					-- Each opponent's speed factor now

function circuitDifficulty()
	-- No catch-up up to the first gap; full catch-up from the second on.
	ai_speed_limit = {150 - 5 * circuit_tier, 400 - 10 * circuit_tier;}
end

-- Lines for the game log (src/mm3_lua_errors.c prints "circuit:" messages).
function circuitLog(text)
	_ERRORMESSAGE("circuit: " .. text)
end

-- A whole number for the log: the game's mod works on whole numbers (and its
-- Lua has no format or floor).
function circuitRound(v)
	if v == nil then
		return "-"
	end
	return mod(v, 1000000)
end

-- ********************************************************************************
-- * Opponents follow a path, as in Midtown Madness 1: one lap of targets in     *
-- * driving order (path points, and a point on every gate), from just after the *
-- * start/finish line to it. Each lap has its own waypoint objects, WP-0 ..      *
-- * WP-(laps * circuit_route_count - 1): an opponent never arms a target twice   *
-- * (one armed again for the next lap froze opponents at the line), and never  *
-- * arms a gate - the race counts its gates as it reaches their points. The     *
-- * game's AI heads for the target that suits it best among those armed, so an  *
-- * opponent has only its next circuit_window targets armed, never one behind.  *
-- * Waypoints have no call: four times a second circuitFollow moves an opponent *
-- * on past every target it reached or drove past (a missed one counts, rather  *
-- * than the car turning round for it).                                         *
-- ********************************************************************************
circuit_route_pos = {}				-- circuit_route_pos[k] = {x, z, reach}: one lap of targets (set by the race)
circuit_route_gate = {}				-- circuit_route_gate[k]: the gate at target k, or -1 (set by the race)
circuit_route_count = 1				-- Targets in one lap (set by the race)
circuit_window = 3					-- Targets an opponent has armed ahead (circuits.OPPONENT_WINDOW)
circuit_at = {}						-- Each opponent's next target, counted over all laps from 0
circuit_armed = {}					-- Each opponent's furthest armed target
circuit_lost_after = 20				-- Seconds on one target before an opponent gives it up ...
circuit_lost_speed = 8				-- ... plus a second for every this many metres to it
circuit_since = {}					-- Each opponent's {target, second it became the next one, seconds allowed}

function circuitTargetPos(k)
	return circuit_route_pos[mod(k + circuit_route_count, circuit_route_count) + 1]
end

-- Arm targets up to circuit_window ahead, in driving order.
function circuitFill(player, ply_id)
	while circuit_armed[ply_id] < circuit_at[ply_id] + circuit_window - 1 and
			circuit_armed[ply_id] < circuit_route_count * laps - 1 do
		circuit_armed[ply_id] = circuit_armed[ply_id] + 1
		playerEnableWayPoint(player, "WP-" .. circuit_armed[ply_id])
	end
end

-- An opponent is done with its targets up to `last`: each is disarmed (the
-- game has taken off the ones reached) and a gate's point counts the gate.
function circuitAdvance(player, ply_id, last, missed)
	local k, gate
	for k = circuit_at[ply_id], last, 1 do
		playerDisableWayPoint(player, "WP-" .. k)
		gate = circuit_route_gate[mod(k, circuit_route_count) + 1]
		if gate >= 0 then
			if missed == 1 then
				circuitLog("missed " .. player .. " CP-" .. gate)
			end
			circuitCountGate(player, ply_id, gate)
		end
	end
	circuit_at[ply_id] = last + 1
	circuitFill(player, ply_id)
end

-- The targets an opponent has reached, or driven past: beyond the target as
-- seen from the one before, outside its reach and closer to the one after.
function circuitFollow(player, ply_id, x, z)
	local k, a, p, q, dx, dz, reach
	local steps = 0
	if x == nil or z == nil then
		return
	end
	while steps < circuit_window and circuit_at[ply_id] < circuit_route_count * laps do
		k = circuit_at[ply_id]
		a = circuitTargetPos(k - 1)
		p = circuitTargetPos(k)
		q = circuitTargetPos(k + 1)
		dx = x - p[1]
		dz = z - p[2]
		reach = p[3]
		if dx * dx + dz * dz < (reach + 6) * (reach + 6) then
			circuitAdvance(player, ply_id, k, 0)
		elseif dx * (p[1] - a[1]) + dz * (p[2] - a[2]) > 0 and
				(x - q[1]) * (x - q[1]) + (z - q[2]) * (z - q[2]) < (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]) then
			circuitAdvance(player, ply_id, k, 1)
		else
			return
		end
		steps = steps + 1
	end
end

-- Four times a second, every opponent moves on along its path.
circuit_follow_delay = 0.25

-- An opponent's speed factor moves towards the one its gap behind the player
-- asks for, by at most circuit_pace_step (circuitTrace, once a second: the
-- game's speed setting is not changed more often).
function circuitPace(player, ply_id, vehicle)
	local want = 1.0
	local gap, now
	if playerGetScore(player) <= playerGetScore(ply_HUMAN_subname .. "0") then
		gap = objectDistance("VH-0", vehicle)
		if gap > ai_speed_limit[2] then
			want = circuit_pace_top
		elseif gap > ai_speed_limit[1] then
			want = 1.0 + (circuit_pace_top - 1.0) * (gap - ai_speed_limit[1]) / (ai_speed_limit[2] - ai_speed_limit[1])
		end
	end
	now = circuit_pace[ply_id]
	if now == nil then
		now = 1.0
	end
	if want > now + circuit_pace_step then
		want = now + circuit_pace_step
	elseif want < now - circuit_pace_step then
		want = now - circuit_pace_step
	end
	if want ~= now then
		circuit_pace[ply_id] = want
		playerSpeedCoeff(player, want)
	end
end

function circuitFollowAll()
	local i
	for i = 1, players - 1, 1 do
		circuitFollow(ply_AI_subname .. (i - 1), i + 1, objectGetCurrentPos("VH-" .. i, 1), objectGetCurrentPos("VH-" .. i, 3))
	end
	timerCreate("circuit_follow", circuit_follow_delay, circuitFollowAll)
end

-- The straight-line distance between two targets (the game's Lua has no
-- sqrt): Newton steps from |dx| + |dz|, never below the distance.
function circuitGap(a, p)
	local dx = p[1] - a[1]
	local dz = p[2] - a[2]
	local v = dx * dx + dz * dz
	local r, i
	if dx < 0 then
		dx = -dx
	end
	if dz < 0 then
		dz = -dz
	end
	r = dx + dz
	if r <= 0 then
		return 0
	end
	for i = 1, 5, 1 do
		r = (r + v / r) / 2
	end
	return r
end

-- An opponent that keeps the same next target far longer than the drive
-- to it takes (circuit_lost_after, plus a second every circuit_lost_speed
-- metres from the target before) has lost its way: it gives that target up
-- for the next one. (circuitWatch handles one that stopped.)
function circuitLostWatch(player, ply_id)
	local s = circuit_since[ply_id]
	local k = circuit_at[ply_id]
	local a, p
	if k >= circuit_route_count * laps then
		return
	end
	if s == nil or s[1] ~= k then
		a = circuitTargetPos(k - 1)
		p = circuitTargetPos(k)
		circuit_since[ply_id] = {k, circuit_trace_tick, circuit_lost_after + circuitGap(a, p) / circuit_lost_speed;}
	elseif circuit_trace_tick - s[2] >= s[3] then
		circuitLog("lost " .. player .. " before WP-" .. circuit_at[ply_id])
		circuitAdvance(player, ply_id, circuit_at[ply_id], 1)
	end
end

-- Once a second: "pos <second> <x y z of VH-0> <x y z of VH-1> ... at <each
-- opponent's next target>" for tools/races/trace.py (where opponents slow
-- down, leave the road or stop following their path). Opponents also move
-- on along their path here (should the quarter-second check ever stop) and
-- get their catch-up speed, changed at most once a second.
function circuitTrace()
	local i, x, z
	local line = "pos " .. circuit_trace_tick
	local targets = " at"
	for i = 0, players - 1, 1 do
		x = objectGetCurrentPos("VH-" .. i, 1)
		z = objectGetCurrentPos("VH-" .. i, 3)
		line = line .. " " .. circuitRound(x) .. " " .. circuitRound(objectGetCurrentPos("VH-" .. i, 2)) .. " " .. circuitRound(z)
		if i > 0 then
			circuitFollow(ply_AI_subname .. (i - 1), i + 1, x, z)
			circuitLostWatch(ply_AI_subname .. (i - 1), i + 1)
			circuitPace(ply_AI_subname .. (i - 1), i + 1, "VH-" .. i)
			targets = targets .. " " .. circuit_at[i + 1]
		end
	end
	circuitLog(line .. targets)
	circuit_trace_tick = circuit_trace_tick + 1
	timerCreate("circuit_trace", 1, circuitTrace)
end

-- An opponent that stops moving (three checks, about 9 s) gives up the target
-- it is heading for and goes on to the next one. The race never moves a car
-- (one put back with objectSetPos flickered and stayed put).
function circuitWatch(player, ply_id, vehicle)
	local x = objectGetCurrentPos(vehicle, 1)
	local z = objectGetCurrentPos(vehicle, 3)
	local w = circuit_watch[ply_id]
	local moved
	if x == nil or z == nil then
		return
	end
	if w == nil or current_target[ply_id] >= total_targets then
		circuit_watch[ply_id] = {x, z, 0;}
		return
	end
	moved = x - w[1]
	if moved < 0 then moved = -moved end
	if z - w[2] < 0 then moved = moved - (z - w[2]) else moved = moved + (z - w[2]) end
	w[1] = x
	w[2] = z
	if moved < 4 then
		w[3] = w[3] + 1
	else
		w[3] = 0
	end
	if w[3] >= 3 and circuit_at[ply_id] < circuit_route_count * laps then
		circuitLog("stuck " .. player .. " " .. circuitRound(x) .. " " .. circuitRound(z) .. " before WP-" .. circuit_at[ply_id])
		circuitAdvance(player, ply_id, circuit_at[ply_id], 1)
		w[3] = 0
	end
end

function checkAISpeed()
	local i
	for i = 0, players - 2, 1 do
		circuitWatch(ply_AI_subname .. i, i + 2, "VH-" .. i + 1)
	end
	timerCreate("check_ai_speed", ai_speed_delay, checkAISpeed)
end

-- An opponent's speed is set by circuitPace.
function checkSpecificAISpeed(player)
end

-- The start strip is a zone only to be seen; no player has it armed.
function zone(id, player)
end

-- The player's gates: all armed. The game disarms a gate right after its
-- checkPoint call, so each pass re-arms every other gate the game has
-- disarmed; only the gate just passed is missing until the next one.
function circuitShowGates(passed)
	local i
	for i = 0, total_gates - 1, 1 do
		if i ~= passed and circuit_shown[i + 1] ~= 1 then
			playerEnableCheckPoint(ply_HUMAN_subname .. "0", "CP-" .. i)
			circuit_shown[i + 1] = 1
		end
	end
	if passed >= 0 then
		circuit_shown[passed + 1] = 0
	end
end

function initLists()
	local i
	for i = 0, total_gates - 1, 1 do
		objectShow("CP-" .. i)
		objectEnable("CP-" .. i)
		circuit_shown[i + 1] = 0
	end
	for i = 1, players, 1 do
		current_target[i] = 0
		circuit_next[i] = 0
		circuit_lap[i] = 1
	end
	if circuit_start_zone ~= "" then
		objectEnable(circuit_start_zone)
		objectShow(circuit_start_zone)
	end
	for i = 0, circuit_block_strips - 1, 1 do
		objectEnable("ZN-" .. i)
		objectShow("ZN-" .. i)
	end
	circuitShowGates(-1)
end

function initAI()
	local i
	for i = 0, players - 2, 1 do
		circuit_at[i + 2] = 0
		circuit_armed[i + 2] = -1
		circuitFill(ply_AI_subname .. i, i + 2)
	end
	circuitDifficulty()
	timerCreate("check_ai_speed", ai_speed_delay, checkAISpeed)
	circuitLog("start " .. gameGetCity() .. " " .. (20 + circuit_tier) .. " gates " .. total_gates .. " laps " .. laps .. " players " .. players .. " path " .. circuit_route_count)
	circuitTrace()
	circuitFollowAll()
	if laps > 1 then
		sidShow("SID_MM3_LAP_1_" .. laps, -1, sid_default_time, -1, sid_default_size_big)
	end
end

function nextCheckPoint()
	return "CP-" .. circuit_next[1]
end

function futureCheckPoint()
	local gate = circuit_next[1] + 1
	if current_target[1] + 1 >= total_targets then
		return ""
	end
	if gate == total_gates then
		gate = 0
	end
	return "CP-" .. gate
end

-- An opponent's gate, in its turn: score, lap, and the finish.
function circuitCountGate(player, ply_id, gate)
	current_target[ply_id] = current_target[ply_id] + 1
	playerAddScore(player)
	circuitLog("gate " .. player .. " " .. gate .. " lap " .. circuit_lap[ply_id])
	circuit_next[ply_id] = gate + 1
	if circuit_next[ply_id] == total_gates then
		circuit_next[ply_id] = 0
		circuit_lap[ply_id] = circuit_lap[ply_id] + 1
	end
	if current_target[ply_id] == total_targets then
		if position > 0 then
			hudAddDisplayText("SID_MP_CHECK_FINISHED", 0, sid_default_time, sid_default_color, sid_default_size_small, ai_name[ply_id - 1])
			if position == 1 then
				soundPlayEffect("checkpoint_ai_finish")
				raceFinish(1, player)
				winner = player
			else
				raceFinish(0, player)
				adjustFinalTime(player)
			end
			position = position + 1
		else
			raceFinish(0, player)
			adjustFinalTime(player)
		end
	else
		checkSpecificAISpeed(player)
	end
end

function checkPoint(id, player)
	local ply_id = playerId(player)
	local gate = strtoint(strsub(id, 3))
	local next_gate

	if player ~= ply_HUMAN_subname .. "0" then
		return				-- Opponents' gates count along their path (circuitAdvance)
	end
	circuitShowGates(gate)
	if gate ~= circuit_next[ply_id] then
		return
	end
	current_target[ply_id] = current_target[ply_id] + 1
	playerAddScore(player)
	circuitLog("gate " .. player .. " " .. gate .. " lap " .. circuit_lap[ply_id])
	next_gate = gate + 1
	if next_gate == total_gates then
		next_gate = 0
		circuit_lap[ply_id] = circuit_lap[ply_id] + 1
	end
	circuit_next[ply_id] = next_gate
	if current_target[1] == total_targets then
		finish(id)
	else
		if next_gate == 0 then
			if circuit_lap[1] == laps then
				sidShow("SID_MM3_LAP_FINAL", -1, sid_default_time, -1, sid_default_size_big)
			else
				sidShow("SID_MM3_LAP_" .. circuit_lap[1] .. "_" .. laps, -1, sid_default_time, -1, sid_default_size_big)
			end
		else
			sidShow("SID_FLASH_CHECKPOINT", -1, sid_default_time_quick, -1, sid_default_size_quick)
		end
		soundPlayEffect("gate")
		arrowBindToObject("Human-0", nextCheckPoint(), futureCheckPoint())
		if current_target[1] == total_targets - 1 then
			stack(wait_after_effect, "duringRaceVO()")
			stackRun()
		end
	end
end

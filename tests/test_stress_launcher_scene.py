"""Exercise the production PowerShell scene controller without launching MM3."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "tools/bench/stress.ps1"
PWSH = ROOT / "tools/bin/powershell/pwsh.exe"


def quote(value):
    return "'" + str(value).replace("'", "''") + "'"


@unittest.skipUnless(os.name == "nt" and PWSH.exists(), "Checkout Windows PowerShell is required")
class StressLauncherSceneTests(unittest.TestCase):
    def run_fixture(self, body):
        with tempfile.TemporaryDirectory() as temporary:
            script = Path(temporary) / "fixture.ps1"
            script.write_text(f"""
$ErrorActionPreference = 'Stop'
$tokens = $null; $parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile({quote(LAUNCHER)}, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) {{ throw ($parseErrors | Out-String) }}
function Production-Function([string]$name) {{
    $nodes = @($ast.FindAll({{ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }}, $true))
    if ($nodes.Count -ne 1) {{ throw "Expected one production function $name" }}
    return $nodes[0].Extent.Text
}}
{body}
""", encoding="utf-8")
            result = subprocess.run(
                [str(PWSH), "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script)],
                capture_output=True, text=True, timeout=45,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_actual_city_selection_handles_dc_paris_and_failed_input(self):
        self.run_fixture(r"""
$assignment = @($ast.FindAll({ param($node)
    $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -eq '$cityPickers'
}, $true))[0]
$loop = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.ForStatementAst] -and $node.Extent.Text -match '\$cityPickers\.city'
}, $true)
$check = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.IfStatementAst] -and
    $node.Extent.Text -like '*Native city selector did not select*'
}, $true)
if (-not $assignment -or -not $loop -or -not $check) { throw 'Production city-selection statements missing' }
$selection = [scriptblock]::Create($assignment.Extent.Text + "`n" + $loop.Extent.Text + "`n" + $check.Extent.Text)
function Get-MenuPickers { return @{ city=$script:currentCity } }
function Press-Pad([string]$button, [double]$after) {
    if ($button -notin @('left', 'right')) { throw 'Unexpected city input' }
    $script:buttons += $button
    if (-not $script:stuck) {
        # The retail picker clamps at Paris/DC; it does not wrap left from Paris.
        $delta=if ($button -eq 'right') { 1 } else { -1 }
        $script:currentCity=[Math]::Clamp($script:currentCity+$delta, 0, 1)
    }
}
foreach ($wantedCity in @(0, 1)) {
    $cityName = if ($wantedCity -eq 1) { 'Washington DC' } else { 'Paris' }
    foreach ($initialCity in @(0, 1)) {
        $script:currentCity=$initialCity; $script:buttons=@(); $script:stuck=$false
        & $selection
        if ($currentCity -ne $wantedCity -or $buttons.Count -ne [int]($initialCity -ne $wantedCity)) {
            throw 'Native city selection did not preserve/select the requested city'
        }
        if ($initialCity -ne $wantedCity -and $buttons[0] -ne $(if ($wantedCity -eq 1) {'right'} else {'left'})) {
            throw 'Native city selection moved in the wrong direction'
        }
    }
    $script:currentCity=1-$wantedCity; $script:buttons=@(); $script:stuck=$true
    $rejected=$false
    try { & $selection } catch {
        if ($_.Exception.Message -ne "Native city selector did not select $cityName.") { throw }
        $rejected=$true
    }
    if (-not $rejected -or $buttons.Count -ne 2) { throw 'Failed input was accepted or retries were unbounded' }
}
""")

    def test_actual_menu_validation_requires_city_max_peds_traffic_and_cops(self):
        self.run_fixture(r"""
$check = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.IfStatementAst] -and
    $node.Extent.Text -like '*Native Cruise menu values are invalid.*'
}, $true)
if (-not $check) { throw 'Production menu validation missing' }
$validate = [scriptblock]::Create($check.Extent.Text)
foreach ($wantedCity in @(0, 1)) {
    $selected=@{ city=$wantedCity; traffic=5; peds=5; cops=4; time=2 }
    & $validate
    foreach ($key in @('city', 'traffic', 'peds', 'cops', 'time')) {
        $prior=$selected[$key]; $selected[$key]=99
        $rejected=$false
        try { & $validate } catch {
            if ($_.Exception.Message -ne 'Native Cruise menu values are invalid.') { throw }
            $rejected=$true
        }
        if (-not $rejected) { throw "Invalid native $key menu value accepted" }
        $selected[$key]=$prior
    }
}
""")

    def test_actual_committed_scene_reads_native_values_and_rejects_each_mismatch(self):
        self.run_fixture(r"""
Invoke-Expression (Production-Function 'Get-CommittedScenario')
Invoke-Expression (Production-Function 'Assert-CommittedScenario')
function Read-GuestWord([uint32]$address) {
    if (-not $script:words.ContainsKey($address)) { throw "Unexpected native word $address" }
    return $script:words[$address]
}
function Read-GuestFloat([uint32]$address) {
    if (-not $script:floats.ContainsKey($address)) { throw "Unexpected native float $address" }
    return $script:floats[$address]
}
foreach ($wantedCity in @(0, 1)) {
    $cityName = if ($wantedCity -eq 1) { 'Washington DC' } else { 'Paris' }
    $script:words=@{ [uint32]0x407C18=$wantedCity; [uint32]0x407C1C=1; [uint32]0x407C30=2 }
    $script:floats=@{
        [uint32]0x407C64=1.0; [uint32]0x407C60=1.0
        [uint32]0x3A85FC=0.12; [uint32]0x3A61EC=0.1; [uint32]0x3A7918=1.1
    }
    $state=Assert-CommittedScenario
    if ($state.city -ne $wantedCity -or $state.Count -ne 8) { throw 'Committed native state was not returned' }
    foreach ($dictionary in @($words, $floats)) {
        foreach ($address in @($dictionary.Keys)) {
            $prior=$dictionary[$address]; $dictionary[$address]=$prior+1
            $rejected=$false
            try { $null=Assert-CommittedScenario } catch {
                if ($_.Exception.Message -notlike "Exact $cityName/Cruise/evening/MAX traffic scene failed native validation:*") { throw }
                $rejected=$true
            }
            if (-not $rejected) { throw "Mismatched native scene value $address accepted" }
            $dictionary[$address]=$prior
        }
    }
    # The float comparison should tolerate ordinary guest serialization noise.
    $floats[[uint32]0x407C60]=1.000001
    $null=Assert-CommittedScenario
}
""")

    def test_full_launcher_rejects_malformed_or_unsupported_plan_before_side_effects(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            save = folder / "save"
            save.mkdir()
            marker = save / "original-save.txt"
            marker.write_text("must remain unchanged", encoding="utf-8")
            example = json.loads((ROOT / "tools/bench/stress-plan.example.json").read_text())
            cases = [("missing_coordinates", {"scene": example["scene"]}, "Stress plan validation failed before launch.")]
            for key, bad in (("city", 2), ("city", 0.5), ("mode", 0), ("time", 0), ("traffic", 0.5), ("peds", 0.5)):
                plan = json.loads(json.dumps(example))
                plan["scene"][key] = bad
                cases.append((f"{key}-{bad}", plan, "Autonomous acquisition supports*"))
            requests = []
            run_dirs = []
            for name, plan, expected in cases:
                path = folder / (name + ".json")
                path.write_text(json.dumps(plan), encoding="utf-8")
                tag = "scene-contract-" + uuid.uuid4().hex
                run_dirs.append(ROOT / "conformance_tmp/bench" / tag)
                requests.append(f"@{{ path={quote(path)}; tag={quote(tag)}; expected={quote(expected)} }}")
            self.run_fixture(f"""
function Start-Process {{ throw 'Game launch was reached' }}
function Get-CimInstance {{ throw 'Process acquisition was reached before scene rejection' }}
function New-Item {{ throw 'Directory creation was reached before scene rejection' }}
function Copy-Item {{ throw 'Save copying was reached before scene rejection' }}
$env:RECOMP_MSAA='scene-contract-sentinel'
$requests=@({', '.join(requests)})
foreach ($request in $requests) {{
    $rejected=$false
    try {{
        & {quote(LAUNCHER)} -Exe {quote(sys.executable)} -Save {quote(save)} -Plan $request.path -Tag $request.tag
    }} catch {{
        if ($_.Exception.Message -notlike $request.expected) {{ throw }}
        $rejected=$true
    }}
    if (-not $rejected) {{ throw 'Malformed/unsupported scene accepted' }}
    if ($env:RECOMP_MSAA -ne 'scene-contract-sentinel') {{ throw 'Early rejection changed process settings' }}
}}
""")
            self.assertEqual(marker.read_text(encoding="utf-8"), "must remain unchanged")
            self.assertEqual(list(save.iterdir()), [marker])
            self.assertTrue(all(not path.exists() for path in run_dirs))


if __name__ == "__main__":
    unittest.main()

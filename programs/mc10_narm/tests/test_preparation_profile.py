"""Build 24 identification, shared geometry protocol and safe play handoff."""
from pathlib import Path
from test_geometry_profile import run

def test_switch():
    project=Path(__file__).resolve().parents[1]
    source=(project/'build_mc10.ps1').read_text()
    assert '[switch]$PreparationProfile' in source
    assert 'if ($PreparationProfile) { $GeometryProfile = $true }' in source
    assert 'mc10_narm_build24_preparation_fixes' in source
    assert 'mc10_narm_build23_geometry_optimization' in source
    assert "if ($PreparationProfile) { $profileCompilerFlags += '-DMC10_PREPARATION_PROFILE=1' }" in source
    assert 'mc10_geometry_report.py' in source
    run(preparation=True)
    print('Build 24 identification PASS: unique folder and b24 log prefix/title; unchanged geometry schema/flags/cases; common fixes disclosed; old-firmware gates, cancellation/error restoration and optimized verified Enter-to-play handoff retained')

if __name__=='__main__':
    test_switch()

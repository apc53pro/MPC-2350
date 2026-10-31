"""Test the production wall-clock profiler without device-dependent timings."""
import subprocess
import tempfile
import ctypes
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]

def run():
    with tempfile.TemporaryDirectory(prefix="mc10-profile-") as tmp:
        executable = Path(tmp) / "profile.exe"
        result = subprocess.run(
            ["gcc", "-O2", str(PROJECT / "tests/profile_fixture.c"), "-o", str(executable)],
            capture_output=True, text=True,
        )
        assert result.returncode == 0, result.stderr
        subprocess.run([str(executable)], check=True)
    # Inject known durations into the real renderer's NARM drawing/clear/
    # present stubs, verifying the timers are around the intended calls.
    from test_render_cache import build, load, trace, integer
    class Frame(ctypes.Structure):
        _fields_ = [(name, ctypes.c_uint32) for name in (
            "total_us", "geometry_us", "draw_us", "input_us", "tick_us",
            "yield_us", "setup_us", "ui_us", "other_us", "hud_us", "present_us",
        )] + [(name, ctypes.c_int32) for name in ("faces", "batches", "pending", "ticks")]
    with tempfile.TemporaryDirectory(prefix="mc10-profile-render-") as tmp:
        lib = build(Path(tmp), PROJECT / "src/mc10_render.tc")
        load(lib, PROJECT / "MC10/saves/New World")
        assert lib.mc_render_build_cache() == 1
        integer(lib, "fixture_draw_us").value = 500
        integer(lib, "fixture_clear_us").value = 100
        integer(lib, "fixture_present_us").value = 200
        lib.mc_profile_cancel()
        for frame_index in range(2):
            stream, *_ = trace(lib, 0, 0, 0, 0)
            frame = Frame.in_dll(lib, "mc_profile_last")
            faces = len(stream) // 88
            assert frame.faces == faces and frame.draw_us == faces * 500
            assert frame.setup_us == 100 and frame.ui_us == 200
            assert frame.hud_us == 0 and frame.present_us == 200
            assert frame.total_us == frame.draw_us + 300
            assert frame.geometry_us == frame.input_us == frame.tick_us == 0
            assert frame.yield_us == frame.other_us == 0
        integer(lib,"fixture_hud_us").value=50
        on_stream,*_=trace(lib,0,0,0,0)
        assert Frame.in_dll(lib,"mc_profile_last").hud_us>0
        assert integer(lib,"fixture_hud_calls").value>0
        integer(lib,"mc_render_show_hud").value=0
        off_stream,*_=trace(lib,0,0,0,0)
        frame=Frame.in_dll(lib,"mc_profile_last")
        assert off_stream==on_stream and integer(lib,"fixture_hud_calls").value==0
        assert frame.hud_us==0 and frame.ui_us==frame.present_us==200
        assert frame.total_us==frame.draw_us+300
        import _ctypes
        _ctypes.FreeLibrary(lib._handle)
        print("Frame profiling: PASS real renderer call boundaries and two consecutive complete frames")

    # A profiler binary must show normal controls after the benchmark hands
    # off to play, without changing its terrain stream or HUD visibility.
    with tempfile.TemporaryDirectory(prefix="mc10-profile-footer-") as tmp:
        lib = build(Path(tmp), PROJECT / "src/mc10_render.tc", autoprofile=True)
        load(lib, PROJECT / "MC10/saves/New World")
        assert lib.mc_render_build_cache() == 1
        reference = None
        for active in (1, 0, 1, 0):
            integer(lib, "mc_render_benchmark_running").value = active
            stream, *_ = trace(lib, 0, 0, 0, 0)
            footer = ctypes.c_char_p.in_dll(lib, "fixture_footer").value
            assert footer == (b"Benchmark running - ESC: cancel" if active else b"Arrows: look ESC: pause F3: profiler")
            if reference is not None:
                assert stream == reference
            reference = stream
        integer(lib, "mc_render_show_hud").value = 0
        trace(lib, 0, 0, 0, 0)
        assert ctypes.c_char_p.in_dll(lib, "fixture_footer").value is None
        integer(lib, "mc_profile_enabled").value = 0
        integer(lib, "mc_render_show_hud").value = 1
        lib.mc_profile_cancel()
        lib.mc_render_fps_reset()
        off_stream, *_ = trace(lib, 0, 0, 0, 0)
        assert off_stream == reference
        assert integer(lib, "fixture_micros_calls").value == 0
        assert integer(lib, "fixture_hud_calls").value == 1
        assert ctypes.c_char_p.in_dll(lib, "fixture_footer").value is None
        assert integer(lib, "mc_profile_active").value == 0
        _ctypes.FreeLibrary(lib._handle)
        print("Profiler HUD: PASS benchmark/play controls switch at runtime; terrain and HUD-off unchanged")

if __name__ == "__main__":
    run()

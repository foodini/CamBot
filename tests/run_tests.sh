#!/bin/bash
# Builds and runs CamBot's unit tests. These cover the parts of the program that don't need a
# window, a GPU or FFmpeg: the climb filters, map zoom, flight statistics, altitude references,
# user settings, the edit model, file ordering, the project file handling, and the
# multi-file decoder (against a fake FFmpeg).
#
# Run it from Windows through WSL (needs g++):    wsl ./tests/run_tests.sh
# or from any Linux shell:                        ./tests/run_tests.sh
#
# Each test is one .cpp file with a main() that prints "all ok" (or a line starting with FAIL)
# and exits non-zero on failure.

cd "$(dirname "$0")" || exit 1
OUT=$(mktemp -d)
failed=0

for test in *_test.cpp; do
    name="${test%.cpp}"
    extra=""
    # Tests that touch the disk use the file helpers.
    if [ "$name" = "project_test" ]; then extra="../src/file_io.cpp ../src/edit_model.cpp ../src/project_model.cpp"; fi
    if [ "$name" = "edit_model_test" ]; then extra="../src/edit_model.cpp"; fi
    if [ "$name" = "edit_playhead_test" ]; then extra="../src/edit_model.cpp"; fi
    if [ "$name" = "timecode_test" ]; then extra="../src/timecode.cpp"; fi
    if [ "$name" = "window_layout_test" ]; then extra="../src/window_layout.cpp"; fi
    if [ "$name" = "timeline_edit_test" ]; then extra="../src/timeline_edit.cpp ../src/edit_history.cpp ../src/edit_model.cpp"; fi
    if [ "$name" = "position_smoothing_test" ]; then extra="../src/position_smoothing.cpp"; fi
    if [ "$name" = "map_view_test" ]; then extra="../src/map_view.cpp"; fi
    if [ "$name" = "tile_fetcher_test" ]; then extra="../src/tile_fetcher.cpp ../src/map_tiles.cpp ../src/map_view.cpp ../src/file_io.cpp -pthread"; fi
    if [ "$name" = "map_tiles_test" ]; then extra="../src/map_tiles.cpp ../src/map_view.cpp"; fi
    if [ "$name" = "thumbnail_layout_test" ]; then extra="../src/thumbnail_layout.cpp"; fi
    if [ "$name" = "timeline_scale_test" ]; then extra="../src/timeline_scale.cpp ../src/scan_gesture.cpp"; fi
    # The decoder test runs the real MediaDecoder against a fake FFmpeg (fake_ffmpeg.cpp) that
    # serves small synthetic files, so no FFmpeg libraries or video files are needed.
    if [ "$name" = "decoder_test" ]; then extra="fake_ffmpeg.cpp ../src/media_decoder.cpp ../src/file_io.cpp ../src/edit_model.cpp"; fi
    if ! g++ -std=c++14 -Wall -Wextra -I ../src -I ../third_party/ffmpeg/include "$test" $extra -o "$OUT/$name"; then
        echo "BUILD FAILED: $name"
        failed=1
        continue
    fi
    if "$OUT/$name" "$OUT" > "$OUT/$name.log" 2>&1; then
        echo "ok      $name"
    else
        echo "FAILED  $name"
        cat "$OUT/$name.log"
        failed=1
    fi
done

rm -rf "$OUT"
exit $failed

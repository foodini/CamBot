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

# The project file's JSON is tested with jsoncpp's own source, found where vcpkg leaves it; without it that test is skipped.
JSONCPP_DIR=$(ls -d ../vcpkg_installed/x64-windows/vcpkg/blds/jsoncpp/src/*/ 2>/dev/null | head -n 1)

for test in *_test.cpp; do
    name="${test%.cpp}"
    extra=""
    if [ "$name" = "project_json_test" ]; then
        if [ -z "$JSONCPP_DIR" ] || [ ! -f "${JSONCPP_DIR}src/lib_json/json_reader.cpp" ]; then
            echo "skipped $name (no jsoncpp source found)"
            continue
        fi
        extra="../src/project_data.cpp ../src/project_model.cpp ../src/file_io.cpp ../src/edit_model.cpp ../src/tracking_path.cpp ../src/text_box.cpp ${JSONCPP_DIR}src/lib_json/json_reader.cpp ${JSONCPP_DIR}src/lib_json/json_value.cpp ${JSONCPP_DIR}src/lib_json/json_writer.cpp -I ${JSONCPP_DIR}include -DJSON_USE_EXCEPTION=0"
    fi
    # Tests that touch the disk use the file helpers.
    if [ "$name" = "project_test" ]; then extra="../src/file_io.cpp ../src/edit_model.cpp ../src/tracking_path.cpp ../src/text_box.cpp ../src/project_model.cpp"; fi
    if [ "$name" = "edit_model_test" ]; then extra="../src/edit_model.cpp ../src/tracking_path.cpp ../src/text_box.cpp"; fi
    if [ "$name" = "transition_test" ]; then extra="../src/edit_model.cpp ../src/tracking_path.cpp ../src/text_box.cpp"; fi
    if [ "$name" = "edit_playhead_test" ]; then extra="../src/edit_model.cpp ../src/tracking_path.cpp ../src/text_box.cpp"; fi
    if [ "$name" = "timecode_test" ]; then extra="../src/timecode.cpp"; fi
    if [ "$name" = "window_layout_test" ]; then extra="../src/window_layout.cpp"; fi
    if [ "$name" = "timeline_edit_test" ]; then extra="../src/timeline_edit.cpp ../src/edit_history.cpp ../src/edit_model.cpp ../src/tracking_path.cpp ../src/text_box.cpp"; fi
    if [ "$name" = "position_smoothing_test" ]; then extra="../src/position_smoothing.cpp"; fi
    if [ "$name" = "map_view_test" ]; then extra="../src/map_view.cpp"; fi
    if [ "$name" = "tile_fetcher_test" ]; then extra="../src/tile_fetcher.cpp ../src/map_tiles.cpp ../src/map_view.cpp ../src/file_io.cpp -pthread"; fi
    if [ "$name" = "map_tiles_test" ]; then extra="../src/map_tiles.cpp ../src/map_view.cpp"; fi
    if [ "$name" = "text_box_test" ]; then extra="../src/edit_model.cpp ../src/tracking_path.cpp ../src/text_box.cpp"; fi
    if [ "$name" = "text_layout_test" ]; then extra="../src/text_layout.cpp ../src/text_box.cpp"; fi
    if [ "$name" = "user_settings_test" ] || [ "$name" = "recent_projects_test" ]; then extra="../src/text_box.cpp ../src/text_box_geometry.cpp"; fi
    if [ "$name" = "text_box_geometry_test" ]; then extra="../src/text_box_geometry.cpp"; fi
    if [ "$name" = "audio_plan_test" ]; then extra="../src/audio_plan.cpp ../src/edit_model.cpp ../src/tracking_path.cpp ../src/text_box.cpp"; fi
    if [ "$name" = "audio_player_test" ]; then extra="fake_audio_output.cpp fake_audio_file_reader.cpp ../src/audio_player.cpp ../src/audio_timeline.cpp ../src/audio_stretch.cpp ../src/audio_plan.cpp ../src/edit_model.cpp ../src/tracking_path.cpp ../src/text_box.cpp"; fi
    if [ "$name" = "audio_timeline_test" ]; then extra="fake_audio_file_reader.cpp ../src/audio_timeline.cpp ../src/audio_stretch.cpp ../src/audio_plan.cpp ../src/edit_model.cpp ../src/tracking_path.cpp ../src/text_box.cpp"; fi
    if [ "$name" = "text_lanes_test" ]; then extra="../src/text_lanes.cpp"; fi
    if [ "$name" = "text_fonts_test" ]; then extra="../src/text_fonts.cpp"; fi
    if [ "$name" = "timeline_selection_test" ]; then extra="../src/timeline_selection.cpp ../src/edit_model.cpp ../src/tracking_path.cpp ../src/text_box.cpp"; fi
    if [ "$name" = "seam_icon_test" ]; then extra="../src/seam_icon.cpp"; fi
    if [ "$name" = "proto_wire_test" ]; then extra="../src/proto_wire.cpp"; fi
    if [ "$name" = "thumbnail_layout_test" ]; then extra="../src/thumbnail_layout.cpp"; fi
    if [ "$name" = "timeline_scale_test" ]; then extra="../src/timeline_scale.cpp ../src/scan_gesture.cpp"; fi
    if [ "$name" = "tracking_path_test" ]; then extra="../src/tracking_path.cpp"; fi
    if [ "$name" = "frame_mapping_test" ]; then extra="../src/frame_mapping.cpp"; fi
    if [ "$name" = "dji_orientation_test" ]; then extra="../src/dji_orientation.cpp ../src/tracking_path.cpp ../src/proto_wire.cpp"; fi
    # The decoder test runs the real MediaDecoder against a fake FFmpeg (fake_ffmpeg.cpp) that
    # serves small synthetic files, so no FFmpeg libraries or video files are needed.
    if [ "$name" = "speed_test" ]; then extra="fake_ffmpeg.cpp ../src/media_decoder.cpp ../src/file_io.cpp ../src/edit_model.cpp ../src/tracking_path.cpp ../src/text_box.cpp ../src/timeline_edit.cpp ../src/audio_plan.cpp"; fi
    if [ "$name" = "audio_stretch_test" ]; then extra="fake_audio_file_reader.cpp ../src/audio_stretch.cpp"; fi
    if [ "$name" = "decoder_test" ]; then extra="fake_ffmpeg.cpp ../src/media_decoder.cpp ../src/file_io.cpp ../src/edit_model.cpp ../src/tracking_path.cpp ../src/text_box.cpp"; fi
    if ! g++ -std=c++14 -Wall -Wextra -I ../src -I ../third_party/ffmpeg/include "$test" $extra -o "$OUT/$name"; then
        echo "BUILD FAILED: $name"
        failed=1
        continue
    fi
    # Run from the scratch folder, so what tests write (the user settings tests save a settings file in the current folder)
    # never lands in the source tree, and a file left there by an earlier run cannot change the next one.
    if (cd "$OUT" && "$OUT/$name" "$OUT" > "$OUT/$name.log" 2>&1); then
        echo "ok      $name"
    else
        echo "FAILED  $name"
        cat "$OUT/$name.log"
        failed=1
    fi
done

rm -rf "$OUT"
exit $failed

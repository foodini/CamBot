#!/bin/bash
# Builds and runs CamBot's unit tests. These cover the parts of the program that don't need a
# window, a GPU or FFmpeg: the climb filters, map zoom, flight statistics, altitude references,
# user settings, the source timeline and file ordering, the project file handling, and the
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
    if [ "$name" = "project_test" ]; then extra="../src/file_io.cpp"; fi
    # The decoder test runs the real MediaDecoder against a fake FFmpeg (fake_ffmpeg.cpp) that
    # serves small synthetic files, so no FFmpeg libraries or video files are needed.
    if [ "$name" = "decoder_test" ]; then extra="fake_ffmpeg.cpp ../src/media_decoder.cpp ../src/file_io.cpp"; fi
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

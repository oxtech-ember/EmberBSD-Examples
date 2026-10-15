#!/bin/sh
# SPDX-License-Identifier: MIT
# Origin: EmberBSD contributors (AI-assisted).
# Run inside one network namespace, on the target or native control host.
set -eu
umask 022
[ "$#" = 4 ] || {
    echo 'Usage: run-api.sh FIXTURE.i420 NEW_RESULTS APP_BINDIR MEDIA_PREFIX' >&2
    exit 2
}
fixture=$1 out=$2 apps=$3 prefix=$4
[ ! -e "$out" ] && [ ! -L "$out" ] || exit 2
mkdir -p "$out"
PATH=$prefix/bin:/bin:/usr/bin:/sbin:/usr/sbin; export PATH
LC_ALL=C; export LC_ALL
GST_PLUGIN_SYSTEM_PATH_1_0=$prefix/lib/gstreamer-1.0
GST_REGISTRY_1_0=$out/gstreamer-registry.bin
export GST_PLUGIN_SYSTEM_PATH_1_0 GST_REGISTRY_1_0
max_ms=${E35_MAX_MS:-90000}
idle_ms=${E35_IDLE_MS:-2500}
relay_pid= recv_pid= sender_pid=
cleanup() {
    for pid in "$sender_pid" "$relay_pid" "$recv_pid"; do
        if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || :; fi
    done
    for pid in "$sender_pid" "$relay_pid" "$recv_pid"; do
        if [ -n "$pid" ]; then wait "$pid" 2>/dev/null || :; fi
    done
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' HUP TERM
wait_ready() {
    ready_path=$1 ready_pid=$2
    attempt=0
    while [ ! -s "$ready_path" ]; do
        kill -0 "$ready_pid"
        attempt=$((attempt + 1))
        [ "$attempt" -le 600 ] || return 1
        sleep 0.1
    done
}
"$apps/check-frames" "$fixture" --require-count 40 > "$out/fixture-check.json"
ffmpeg9 -hide_banner -version > "$out/ffmpeg-version.txt"
gst-inspect-1.0 --version > "$out/gstreamer-version.txt"
ffmpeg9 -hide_banner -v warning -nostdin -threads 1 \
    -f rawvideo -pixel_format yuv420p -video_size 96x64 -framerate 10 \
    -i "$fixture" -an -c:v libx265 -preset ultrafast \
    -x265-params 'lossless=1:keyint=4:min-keyint=4:scenecut=0:repeat-headers=1:aud=1:bframes=0:open-gop=0:pools=none:frame-threads=1:wpp=0:log-level=error' \
    -frames:v 40 -fps_mode passthrough "$out/fixture.mkv" \
    > "$out/encode.log" 2>&1
run_case() {
    name=$1 sender=$2 receiver=$3 oracle=$4
    shift 4
    case_out=$out/$name
    mkdir "$case_out"
    cat > "$case_out/stream.sdp" <<'SDP'
v=0
o=- 0 0 IN IP4 127.0.0.1
s=EmberBSD E35 HEVC RTP
c=IN IP4 127.0.0.1
t=0 0
m=video 5006 RTP/AVP 96
a=rtpmap:96 H265/90000
SDP
    if [ "$receiver" = gst ]; then
        if [ "$name" = receiver-restart-ff-gst ]; then
            "$apps/gst-h265-rtp" receive 5006 "$case_out/decoded.i420" \
                "$case_out/frames.csv" "$case_out/receiver.json" \
                "$max_ms" "$idle_ms" "$case_out/receiver.ready" 1400 600 \
                > "$case_out/receiver.log" 2>&1 &
        else
            "$apps/gst-h265-rtp" receive 5006 "$case_out/decoded.i420" \
                "$case_out/frames.csv" "$case_out/receiver.json" \
                "$max_ms" "$idle_ms" "$case_out/receiver.ready" \
                > "$case_out/receiver.log" 2>&1 &
        fi
    else
        "$apps/ffmpeg-h265-receive" "$case_out/stream.sdp" \
            "$case_out/decoded.i420" "$case_out/frames.csv" \
            "$case_out/receiver.json" "$max_ms" "$idle_ms" \
            "$case_out/receiver.ready" > "$case_out/receiver.log" 2>&1 &
    fi
    recv_pid=$!
    wait_ready "$case_out/receiver.ready" "$recv_pid"
    "$apps/rtp-fault-relay" --listen 5004 --forward 5006 \
        --capture "$case_out/received.rtp" --forward-capture "$case_out/forwarded.rtp" \
        --stats "$case_out/relay.json" --ready-file "$case_out/relay.ready" \
        --max-ms "$max_ms" --idle-ms "$idle_ms" "$@" \
        > "$case_out/relay.log" 2>&1 &
    relay_pid=$!
    wait_ready "$case_out/relay.ready" "$relay_pid"
    if [ "$sender" = ff ]; then
        ffmpeg9 -hide_banner -v warning -nostdin -re -i "$out/fixture.mkv" \
            -map 0:v:0 -c:v copy -an -payload_type 96 -ssrc 123456 -seq 1000 \
            -f rtp 'rtp://127.0.0.1:5004?pkt_size=400' \
            > "$case_out/sender.log" 2>&1 &
    else
        "$apps/gst-h265-rtp" send "$fixture" 96 64 10 127.0.0.1 5004 \
            "$case_out/sender.json" "$max_ms" > "$case_out/sender.log" 2>&1 &
    fi
    sender_pid=$!
    wait "$sender_pid"; sender_pid=
    wait "$relay_pid"; relay_pid=
    wait "$recv_pid"; recv_pid=
    case "$oracle" in
        clean) "$apps/check-frames" "$case_out/decoded.i420" --require-count 40 ;;
        loss) "$apps/check-frames" "$case_out/decoded.i420" --allow-damaged \
            --require-tail-start 8 --require-tail-count 32 ;;
        combined) "$apps/check-frames" "$case_out/decoded.i420" --allow-damaged \
            --require-tail-start 20 --require-tail-count 20 ;;
        restart) "$apps/check-frames" "$case_out/decoded.i420" --allow-damaged \
            --require-tail-start 28 --require-tail-count 12 ;;
        *) return 2 ;;
    esac > "$case_out/pixels.json"
    echo "PASS $name"
}
run_case clean-ff-gst ff gst clean
run_case clean-gst-ff gst ff clean
if [ "${E35_CLEAN_ONLY:-no}" = yes ]; then
    echo PASS > "$out/API.PASS"
    exit 0
fi
run_case loss-ff-gst ff gst loss --drop-vcl-fu 1
run_case loss-gst-ff gst ff loss --drop-vcl-fu 1
run_case combined-ff-gst ff gst combined --drop-vcl-fu 1 --duplicate 20 \
    --reorder 40 --blackout-ms 800:1200
run_case combined-gst-ff gst ff combined --drop-vcl-fu 1 --duplicate 20 \
    --reorder 40 --blackout-ms 800:1200
run_case receiver-restart-ff-gst ff gst restart
echo PASS > "$out/API.PASS"

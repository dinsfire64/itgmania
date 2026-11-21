#!/bin/sh

knob_path=/sys/devices/platform/soc@3000000/2009000.gpadc0/iio:device0/in_voltage2_raw
jack_path=/sys/module/snd_soc_sunxi_common/parameters/jack_state
status_file=/tmp/ddrmini-audio-status
previous_headphones=-1
previous_volume=-1

log() { echo "monitor_audio: $*"; }
set_control() {
  if ! amixer -q set "$1" "$2"; then
    log "failed to set $1 to $2"
  fi
}

cleanup() { rm -f "$status_file"; }
trap 'cleanup; exit 0' INT TERM
trap cleanup EXIT

set_control HPOUT on

while :; do
  if ! knob=$(cat "$knob_path" 2>/dev/null) ||
     ! headphones=$(cat "$jack_path" 2>/dev/null); then
    echo "UNAVAILABLE 0" > "$status_file"
    sleep 1
    continue
  fi

  volume=$((knob * 64 / 1234))
  [ "$volume" -gt 63 ] && volume=63
  [ "$volume" -lt 0 ] && volume=0

  if [ "$headphones" -ne "$previous_headphones" ]; then
    if [ "$headphones" -eq 1 ]; then
      set_control SPK off
      route=HEADPHONES
    else
      set_control SPK on
      route=SPEAKER
    fi
    previous_headphones=$headphones
    log "route changed to $route"
  elif [ "$headphones" -eq 1 ]; then
    route=HEADPHONES
  else
    route=SPEAKER
  fi

  if [ "$volume" -ne "$previous_volume" ]; then
    set_control DAC "$volume"
    previous_volume=$volume
    log "volume changed: raw=$knob DAC=$volume"
  fi

  echo "$route $volume" > "$status_file"
  sleep 0.1
done


#!/bin/bash
set -euo pipefail

readonly helper_module=gen2kr
readonly nvidia_vendor=0x10de
readonly retrain_wait_seconds=3
readonly helper_path=/sys/module/$helper_module

if (( EUID != 0 )); then
    echo 'Run as root from a console with GPU applications stopped.' >&2
    exit 1
fi
if (( $# != 1 )) || [[ ! $1 =~ ^[[:xdigit:]]{4}:[[:xdigit:]]{2}:[[:xdigit:]]{2}\.[0-7]$ ]]; then
    echo "Usage: ${0##*/} <P1xx/CMP PCI-address>" >&2
    exit 1
fi

readonly target_bdf=${1,,}
readonly device_path=/sys/bus/pci/devices/$target_bdf
readonly driver_path=/sys/bus/pci/drivers/nvidia

if [[ ! -r $device_path/vendor ]] ||
   [[ $(< "$device_path/vendor") != "$nvidia_vendor" ]]; then
    echo "$target_bdf is likely not a NVIDIA P1xx/CMP GPU." >&2
    exit 1
fi

modprobe nvidia
if [[ $(readlink -f "$device_path/driver") != "$driver_path" ]]; then
    echo "$target_bdf must be bound to the NVIDIA driver." >&2
    exit 1
fi
helper_loaded=0
if [[ -d $helper_path ]]; then
    if [[ ! -r $helper_path/parameters/target_bdf ]] ||
       [[ $(< "$helper_path/parameters/target_bdf") != "$target_bdf" ]]; then
        echo "$helper_module is loaded for another PCI address. Unload it before selecting $target_bdf." >&2
        exit 1
    fi
    if [[ ! -r $helper_path/parameters/probe_triggered ]]; then
        echo "$helper_module lacks probe status. Reload it using the updated helper package." >&2
        exit 1
    fi
    if [[ $(< "$helper_path/parameters/probe_triggered") != 0 ]]; then
        echo "$helper_module has already handled a probe. Unload it before another activation." >&2
        exit 1
    fi
    helper_loaded=1
fi

shopt -s nullglob
device_nodes=()
for node in /dev/nvidia* /dev/nvidia-caps/*; do
    [[ ! -c $node ]] || device_nodes+=("$node")
done
if (( ${#device_nodes[@]} )); then
    busy_status=0
    fuser -v "${device_nodes[@]}" && busy_status=0 || busy_status=$?
    if (( busy_status != 1 )); then
        echo 'NVIDIA device nodes are busy or their users could not be checked.' >&2
        exit 1
    fi
fi

needs_bind=0
restore_binding() {
    if (( needs_bind )); then
        printf '%s\n' "$target_bdf" > "$driver_path/bind"
    fi
}
trap restore_binding EXIT

if (( ! helper_loaded )); then
    modprobe "$helper_module" "target_bdf=$target_bdf"
fi
needs_bind=1
printf '%s\n' "$target_bdf" > "$driver_path/unbind"
printf '%s\n' "$target_bdf" > "$driver_path/bind"
needs_bind=0
sleep "$retrain_wait_seconds"
speed=$(< "$device_path/current_link_speed")
printf '%s: %s\n' "$target_bdf" "$speed"
if [[ $speed != '5.0 GT/s PCIe' ]]; then
    echo 'Expected a 5.0 GT/s link. Check the gen2kr messages in the kernel log.' >&2
    exit 1
fi

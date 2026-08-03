#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/m1n1-host-tests.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
cc=${CC:-cc}

all_tests="
hv_autonomous_manifest_test
hv_autonomous_stage_test
hv_diag_test
hv_fb_stream_test
hv_fb_stream_usb_limit_test
hv_irq_routes_test
hv_nvme_queue_test
hv_sgi_diag_test
hv_vgic_diag_test
hv_vgic_redist_test
hv_xhci_handoff_test
iodev_console_backpressure_test
ringbuffer_test
uartproxy_event_test
usb_dwc3_bulk_state_test
"

if [ "$#" -eq 0 ]; then
    # shellcheck disable=SC2086
    set -- $all_tests
fi

for name in "$@"; do
    definitions=""
    sources="tests/$name.c"
    case "$name" in
        hv_autonomous_manifest_test)
            sources="$sources src/hv_autonomous_manifest.c"
            ;;
        hv_autonomous_stage_test)
            sources="$sources src/hv_autonomous.c"
            ;;
        hv_diag_test)
            definitions="-DHV_DIAG_HOST_TEST"
            sources="$sources src/hv_diag.c"
            ;;
        hv_fb_stream_test)
            definitions="-DHV_FB_STREAM_HOST_TEST -DHV_FB_STREAM_PAYLOAD_SIZE=4"
            sources="$sources src/hv_fb_stream.c"
            ;;
        hv_fb_stream_usb_limit_test)
            definitions="-DHV_FB_STREAM_HOST_TEST -DUARTPROXY_EVENT_HOST_TEST"
            sources="$sources src/hv_fb_stream.c src/uartproxy_event.c"
            ;;
        hv_irq_routes_test)
            definitions="-DHV_IRQ_ROUTES_HOST_TEST"
            sources="$sources src/hv_irq_routes.c"
            ;;
        hv_nvme_queue_test)
            definitions="-DVNVME_HOST_TEST"
            sources="$sources src/hv_nvme_queue.c"
            ;;
        hv_sgi_diag_test)
            definitions="-DHV_SGI_DIAG_HOST_TEST"
            sources="$sources src/hv_sgi_diag.c"
            ;;
        hv_vgic_diag_test)
            definitions="-DHV_VGIC_DIAG_HOST_TEST"
            sources="$sources src/hv_vgic_diag.c"
            ;;
        hv_vgic_redist_test)
            definitions="-DHV_VGIC_REDIST_HOST_TEST"
            sources="$sources src/hv_vgic_redist.c"
            ;;
        hv_xhci_handoff_test)
            definitions="-DHV_XHCI_HANDOFF_HOST_TEST"
            sources="$sources src/hv_xhci_handoff.c"
            ;;
        iodev_console_backpressure_test)
            ;;
        ringbuffer_test)
            definitions="-DRINGBUFFER_HOST_TEST"
            sources="$sources src/ringbuffer.c"
            ;;
        uartproxy_event_test)
            definitions="-DUARTPROXY_EVENT_HOST_TEST"
            sources="$sources src/uartproxy_event.c"
            ;;
        usb_dwc3_bulk_state_test)
            definitions="-DUSB_DWC3_BULK_STATE_HOST_TEST"
            sources="$sources src/usb_dwc3_bulk_state.c"
            ;;
        *)
            echo "unknown host test: $name" >&2
            exit 2
            ;;
    esac

    echo "  HOSTCC  $name"
    # The lists above are fixed repository paths, not user-provided shell input.
    # shellcheck disable=SC2086
    "$cc" -std=c11 -Wall -Wextra -Werror $definitions $sources -o "$build_dir/$name"
    "$build_dir/$name"
done

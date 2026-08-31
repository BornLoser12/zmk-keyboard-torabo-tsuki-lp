"""Host regression checks for the pinned Studio USB transport patch."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "patches/studio-usb-independent.patch"
RPC_PATH = Path("app/src/studio/rpc.c")


def run(*args, cwd=None, check=True):
    result = subprocess.run(args, cwd=cwd, text=True, capture_output=True)
    if check and result.returncode:
        raise RuntimeError(f"{args!r} failed:\n{result.stdout}\n{result.stderr}")
    return result


class StudioUsbTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="torabo-studio-test-")
        self.addCleanup(self.temp.cleanup)
        self.tree = Path(self.temp.name)
        self.rpc = self.tree / RPC_PATH
        self.rpc.parent.mkdir(parents=True)
        original = run("git", "-C", str(ZMK), "show", "HEAD:" + RPC_PATH.as_posix()).stdout
        self.rpc.write_text(original, encoding="utf-8", newline="\n")

    def apply_patch(self):
        run("git", "apply", "--check", str(PATCH), cwd=self.tree)
        run("git", "apply", str(PATCH), cwd=self.tree)
        return self.rpc.read_text(encoding="utf-8")

    def configure_patch(self, check=True):
        return run(
            CMAKE,
            "-DAPPLICATION_SOURCE_DIR=" + (self.tree / "app").as_posix(),
            "-P",
            str(ROOT / "cmake/studio_usb.cmake"),
            cwd=self.tree,
            check=check,
        )

    def test_patch_only_changes_studio_rpc(self):
        files = run("git", "apply", "--numstat", str(PATCH), cwd=self.tree).stdout
        self.assertEqual([line.split("\t")[-1] for line in files.splitlines()],
                         [RPC_PATH.as_posix()])
        self.apply_patch()
        run("git", "apply", "--reverse", "--check", str(PATCH), cwd=self.tree)

    def test_usb_event_is_subscribed(self):
        source = self.apply_patch()
        self.assertIn(
            "#if IS_ENABLED(CONFIG_TORABO_STUDIO_USB_INDEPENDENT)\n"
            "ZMK_SUBSCRIPTION(studio_rpc, zmk_usb_conn_state_changed);\n#endif",
            source,
        )
        self.assertIn("enum zmk_transport transport = get_rpc_transport();", source)

    def test_cmake_patch_is_idempotent(self):
        if not CMAKE:
            self.skipTest("cmake is not installed; CI requires it")
        self.configure_patch()
        first = self.rpc.read_bytes()
        self.configure_patch()
        self.assertEqual(self.rpc.read_bytes(), first)
        run("git", "apply", "--reverse", "--check", str(PATCH), cwd=self.tree)

    def test_cmake_rejects_incompatible_source_without_changes(self):
        if not CMAKE:
            self.skipTest("cmake is not installed; CI requires it")
        self.rpc.write_text("/* incompatible upstream */\n", encoding="utf-8")
        before = self.rpc.read_bytes()
        result = self.configure_patch(check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Studio USB patch does not match", result.stderr)
        self.assertEqual(self.rpc.read_bytes(), before)

    def test_transport_selection_and_usb_events(self):
        if not CC:
            self.skipTest("C compiler is not installed; CI requires it")
        source = self.apply_patch()
        selector = re.search(
            r"static enum zmk_transport get_rpc_transport\(void\) \{.*?^\}",
            source, re.MULTILINE | re.DOTALL,
        ).group(0)
        listener_start = source.index("static int studio_rpc_listener_cb(")
        listener_end = source.index("    struct zmk_studio_rpc_notification *rpc_notify",
                                    listener_start)
        # Compile the real transport-event branches, stopping before unrelated RPC dispatch.
        listener = source[listener_start:listener_end] + "    return ZMK_EV_EVENT_BUBBLE;\n}\n"
        fixture = self.tree / "test.c"
        fixture.write_text(HARNESS_PREFIX + selector + HARNESS_REFRESH + listener + HARNESS_MAIN,
                           encoding="utf-8")
        for enabled in (0, 1):
            with self.subTest(usb_independent=enabled):
                executable = self.tree / ("test-" + str(enabled) + (".exe" if os.name == "nt" else ""))
                run(CC, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-DCONFIG_TORABO_STUDIO_USB_INDEPENDENT=" + str(enabled),
                    "-I", str(ZMK / "app/include"), str(fixture), "-o", str(executable))
                run(str(executable))


HARNESS_PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <zmk/endpoints_types.h>
#define IS_ENABLED(x) (x)
#define ZMK_EV_EVENT_BUBBLE 0
typedef struct { int kind; } zmk_event_t;
struct zmk_endpoint_changed { int unused; } endpoint_event;
static struct zmk_endpoint_instance hid;
static bool usb_ready;
static int refresh_count;
static enum zmk_transport studio;
struct zmk_endpoint_instance zmk_endpoint_get_selected(void) { return hid; }
bool zmk_usb_is_hid_ready(void) { return usb_ready; }
const void *as_zmk_usb_conn_state_changed(const zmk_event_t *event) {
    return event->kind == 1 ? event : 0;
}
struct zmk_endpoint_changed *as_zmk_endpoint_changed(const zmk_event_t *event) {
    return event->kind == 2 ? &endpoint_event : 0;
}
"""

HARNESS_REFRESH = r"""
static void refresh_selected_transport(void) {
    refresh_count++;
    studio = get_rpc_transport();
}
"""

HARNESS_MAIN = r"""
int main(void) {
    for (int connected = 0; connected <= 1; connected++) {
        usb_ready = connected;
        for (int output = ZMK_TRANSPORT_NONE; output <= ZMK_TRANSPORT_BLE; output++) {
            hid.transport = output;
            hid.ble.profile_index = 3;
            enum zmk_transport expected =
                CONFIG_TORABO_STUDIO_USB_INDEPENDENT && connected ? ZMK_TRANSPORT_USB : output;
            assert(get_rpc_transport() == expected);
            assert(hid.transport == (enum zmk_transport)output);
            assert(hid.ble.profile_index == 3);
        }
    }
    /* USB connect/disconnect must refresh Studio even if HID stays on BLE. */
    hid.transport = ZMK_TRANSPORT_BLE;
    zmk_event_t usb_event = { .kind = 1 };
    for (int connected = 1; connected >= 0; connected--) {
        usb_ready = connected;
        refresh_count = 0;
        studio = ZMK_TRANSPORT_BLE;
        assert(studio_rpc_listener_cb(&usb_event) == ZMK_EV_EVENT_BUBBLE);
        assert(refresh_count == CONFIG_TORABO_STUDIO_USB_INDEPENDENT);
        assert(studio == (CONFIG_TORABO_STUDIO_USB_INDEPENDENT && connected ?
                          ZMK_TRANSPORT_USB : ZMK_TRANSPORT_BLE));
        assert(hid.transport == ZMK_TRANSPORT_BLE);
        assert(hid.ble.profile_index == 3);
    }
    zmk_event_t endpoint = { .kind = 2 };
    refresh_count = 0;
    studio_rpc_listener_cb(&endpoint);
    assert(refresh_count == 1);
    zmk_event_t unrelated = { .kind = 3 };
    studio_rpc_listener_cb(&unrelated);
    assert(refresh_count == 1);
    return 0;
}
"""


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--zmk", required=True, type=Path)
    parser.add_argument("--require-toolchain", action="store_true")
    args = parser.parse_args()
    ZMK = args.zmk.resolve()
    CMAKE = shutil.which("cmake")
    CC = shutil.which(os.environ.get("CC", "cc")) or shutil.which("gcc") or shutil.which("clang")
    if args.require_toolchain and (not CMAKE or not CC):
        parser.error("cmake and a host C compiler are required")
    unittest.main(argv=[__file__], verbosity=2)

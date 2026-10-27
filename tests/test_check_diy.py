"""The breadboard layout (hardware/breadboard) connects what the firmware
expects: checked with tools/check_diy.py."""
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LAYOUT = ROOT / "hardware" / "breadboard" / "dtmf-signer-breadboard.diy"
CHECKER = ROOT / "tools" / "check_diy.py"

GND = "pico:GND+tb:2"
V33 = "oled:VCC+pico:3V3"

# Part -> the two nets its leads must reach (in either order). "~" is a node
# that only joins parts to each other.
EXPECTED = {
    "R1": ("~", "pico:GP28"), "R2": ("~", "pico:GP28"), "R3": ("~", "pico:GP28"),
    "R4": ("~", "pico:GP28"), "R5": ("~", "pico:GP28"),
    "R6": (V33, "pico:GP28"),
    "C1": ("~B", "tb:3"),
    "R7": ("~B", V33), "R8": ("~B", GND), "R9": ("~B", "~C"),
    "R10": ("~C", "pico:GP27"), "C2": (GND, "pico:GP27"),
    "R11": ("pico:GP15", "~O"), "C3": ("~O", "tb:1"),
}
VALUES = {"R1": "1.0K", "R2": "2.2K", "R3": "4.7K", "R4": "10.0K", "R5": "22.0K",
          "R6": "10.0K", "R7": "10.0K", "R8": "10.0K", "R9": "1.0K", "R10": "4.7K",
          "R11": "1.0K", "C1": "1.0uF", "C2": "10.0nF", "C3": "10.0uF"}


class CheckDiyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        run = subprocess.run([sys.executable, str(CHECKER), str(LAYOUT)],
                             capture_output=True, text=True)
        cls.rc, cls.out = run.returncode, run.stdout
        cls.parts = {}
        for line in cls.out.splitlines():
            f = line.split()
            if len(f) == 5 and f[3] == "<->":
                cls.parts[f[0]] = (f[1], f[2], f[4])

    def test_no_problems(self):
        self.assertEqual(self.rc, 0, self.out)
        self.assertIn("no problems", self.out)

    def test_screen_reaches_the_pico(self):
        for sig in ("GND", "CLK", "MOSI", "RES", "DC", "CS"):
            self.assertIn(f"oled:{sig} + pico:{sig}", self.out)
        self.assertIn("oled:VCC + pico:3V3", self.out)

    def test_parts_and_values(self):
        def other(name, net):
            _, a, b = self.parts[name]
            self.assertIn(net, (a, b), name)
            return b if a == net else a

        # The local nodes, found from one part each, then checked everywhere.
        node = {"~B": other("C1", "tb:3"), "~O": other("C3", "tb:1")}
        node["~C"] = other("R9", node["~B"])
        for name, (want_a, want_b) in EXPECTED.items():
            value, a, b = self.parts[name]
            self.assertEqual(value, VALUES[name], name)
            want = [node.get(w, w) for w in (want_a, want_b)]
            if "~" in want:  # an anonymous local node: one lead somewhere local
                known = [w for w in want if w != "~"][0]
                self.assertIn(known, (a, b), name)
                self.assertTrue(other(name, known).startswith("~"), name)
            else:
                self.assertEqual(sorted(want), sorted((a, b)), name)
        self.assertEqual(len({node["~B"], node["~C"], node["~O"]}), 3)

    def test_buttons_to_gnd_through_their_resistor(self):
        for i in range(1, 6):
            value, a, b = self.parts[f"SW{i}"]
            self.assertIn(GND, (a, b), f"SW{i}")
            other = b if a == GND else a
            self.assertIn(other, self.parts[f"R{i}"][1:], f"SW{i} and R{i}")

    def test_no_author_in_the_file(self):
        text = LAYOUT.read_text()
        self.assertIn("<author></author>", text)


if __name__ == "__main__":
    unittest.main()

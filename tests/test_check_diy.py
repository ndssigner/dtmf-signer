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
VALUES = {"R1": "22.0K", "R2": "10.0K", "R3": "4.7K", "R4": "2.2K", "R5": "1.0K",
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
        # The firmware tells the buttons apart by their resistor (ds_btn).
        want = {"LEFT": "1.0K", "UP": "2.2K", "OK": "4.7K", "DOWN": "10.0K", "RIGHT": "22.0K"}
        ladder = {n: v for n, v in self.parts.items() if n in ("R1", "R2", "R3", "R4", "R5")}
        for i in range(1, 6):
            label, a, b = self.parts[f"SW{i}"]
            self.assertIn(GND, (a, b), f"SW{i}")
            node = b if a == GND else a
            values = [v[0] for v in ladder.values() if node in v[1:]]
            self.assertEqual(values, [want[label]], f"SW{i} {label}")

    def test_project_credits(self):
        text = LAYOUT.read_text()
        self.assertIn("<author>NDS-Signer</author>", text)
        self.assertIn("https://github.com/ndssigner/dtmf-signer", text)


PEX = ROOT / "hardware" / "phone" / "dtmf-signer-pex.diy"
LINE_A = "U2.1+lead:RJ11_line_A"            # the LM317's ADJ and the socket's line A
V12 = "MOD1:VOUT++U2.3"                     # the boost module's output and the LM317's IN
PEX_GND = "MOD1:VIN-+MOD1:VOUT-+lead:RJ11_line_B+pico:GND"
TOP_GND = "pico:AGND+pico:GND"

# The PEX (hardware/phone), with the references of its KiCad schematic.
PEX_EXPECTED = {
    "R1": ("U2.2", LINE_A), "C1": (V12, PEX_GND), "C2": (V12, PEX_GND),
    "C3": (LINE_A, "~B"), "R2": ("~B", "pico:3V3"), "R3": ("~B", TOP_GND),
    "R4": ("~B", "~C"), "R5": ("~C", "pico:GP27"), "C4": ("pico:GP27", TOP_GND),
    "R6": ("pico:GP15", "~O"), "C5": ("~O", PEX_GND), "C6": (LINE_A, "~O"),
    "R7": (LINE_A, "pico:GP28"), "R8": ("pico:GP28", TOP_GND), "C7": ("pico:GP28", TOP_GND),
}
PEX_VALUES = {"R1": "51.0R", "R2": "10.0K", "R3": "10.0K", "R4": "1.0K", "R5": "4.7K",
              "R6": "1.0K", "R7": "47.0K", "R8": "10.0K", "C1": "100.0uF", "C2": "100.0nF",
              "C3": "100.0nF", "C4": "10.0nF", "C5": "10.0nF", "C6": "10.0uF", "C7": "100.0nF"}


class CheckDiyPexTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        run = subprocess.run([sys.executable, str(CHECKER), str(PEX)], capture_output=True, text=True)
        cls.rc, cls.out = run.returncode, run.stdout
        cls.parts = {}
        for line in cls.out.splitlines():
            f = line.split()
            if len(f) == 5 and f[3] == "<->":
                cls.parts[f[0]] = (f[1], f[2], f[4])

    def test_no_problems(self):
        self.assertEqual(self.rc, 0, self.out)
        self.assertIn("no problems", self.out)

    def test_parts_values_and_nets(self):
        def other(name, net):
            _, a, b = self.parts[name]
            self.assertIn(net, (a, b), name)
            return b if a == net else a

        node = {"~B": other("C3", LINE_A), "~O": other("R6", "pico:GP15")}
        node["~C"] = other("R5", "pico:GP27")
        for name, want in PEX_EXPECTED.items():
            value, a, b = self.parts[name]
            self.assertEqual(value, PEX_VALUES[name], name)
            self.assertEqual(sorted(node.get(w, w) for w in want), sorted((a, b)), name)
        self.assertEqual(len(set(node.values())), 3)
        self.assertTrue(all(n.startswith("~") for n in node.values()))

    def test_supply_and_line(self):
        self.assertIn("net: MOD1:VIN+ + pico:VBUS", self.out)
        self.assertIn("net: MOD1:VOUT+ + U2.3", self.out)
        self.assertIn("net: U2.1 + lead:RJ11_line_A", self.out)
        self.assertIn("net: pico:AGND + pico:GND", self.out)

    def test_project_credits(self):
        text = PEX.read_text()
        self.assertIn("<author>NDS-Signer</author>", text)
        self.assertIn("https://github.com/ndssigner/dtmf-signer", text)


if __name__ == "__main__":
    unittest.main()

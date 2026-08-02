import ast
from pathlib import Path
import re
import struct
import sys
import unittest


ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "proxyclient"))

from m1n1.proxy import M1N1Proxy


class FakeHeap:
    def __init__(self):
        self.allocations = []
        self.frees = []

    def malloc(self, size):
        address = 0x1000 + len(self.allocations) * 0x1000
        self.allocations.append((address, size))
        return address

    def free(self, address):
        self.frees.append(address)


class FakeInterface:
    def __init__(self, retval, payload):
        self.retval = retval
        self.payload = payload
        self.requests = []
        self.reads = []

    def proxyreq(self, request, reboot=False, no_reply=False, pre_reply=None):
        del reboot, no_reply, pre_reply
        opcode, *args = struct.unpack("<7Q", request)
        self.requests.append((opcode, args))
        return struct.pack("<QqQ", opcode, 0, self.retval)

    def readmem(self, address, size):
        self.reads.append((address, size))
        return self.payload[:size]


class HvDiagProxyContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.python_source = (ROOT / "proxyclient/m1n1/proxy.py").read_text()
        cls.python_tree = ast.parse(cls.python_source)
        cls.c_header = (ROOT / "src/proxy.h").read_text()

    def proxy(self, retval, payload):
        interface = FakeInterface(retval, payload)
        proxy = M1N1Proxy(interface)
        proxy.heap = FakeHeap()
        return proxy, interface

    def test_opcodes_are_explicit_and_match_c(self):
        for name, expected in (("P_HV_DIAG_STATUS", 0xC1B), ("P_HV_DIAG_SAMPLE", 0xC1C)):
            match = re.search(rf"{name}\s*=\s*(0x[0-9a-fA-F]+)", self.c_header)
            self.assertIsNotNone(match)
            self.assertEqual(int(match.group(1), 16), expected)

        proxy_class = next(
            node
            for node in self.python_tree.body
            if isinstance(node, ast.ClassDef) and node.name == "M1N1Proxy"
        )
        assignments = {
            item.targets[0].id: ast.literal_eval(item.value)
            for item in proxy_class.body
            if isinstance(item, ast.Assign) and isinstance(item.targets[0], ast.Name)
        }
        self.assertEqual(assignments["P_HV_DIAG_STATUS"], 0xC1B)
        self.assertEqual(assignments["P_HV_DIAG_SAMPLE"], 0xC1C)

    def test_status_allocates_reads_and_frees_exact_abi_size(self):
        payload = bytes(range(32))
        proxy, interface = self.proxy(1, payload)

        self.assertEqual(proxy.hv_diag_status(), payload)
        self.assertEqual(proxy.heap.allocations, [(0x1000, 32)])
        self.assertEqual(interface.requests, [(0xC1B, [0x1000, 32, 0, 0, 0, 0])])
        self.assertEqual(interface.reads, [(0x1000, 32)])
        self.assertEqual(proxy.heap.frees, [0x1000])

    def test_sample_passes_sequence_and_returns_exact_sample(self):
        payload = bytes(index & 0xFF for index in range(176))
        proxy, interface = self.proxy(1, payload)

        self.assertEqual(proxy.hv_diag_sample(37), payload)
        self.assertEqual(proxy.heap.allocations, [(0x1000, 176)])
        self.assertEqual(interface.requests, [(0xC1C, [37, 0x1000, 176, 0, 0, 0])])
        self.assertEqual(interface.reads, [(0x1000, 176)])
        self.assertEqual(proxy.heap.frees, [0x1000])

    def test_stale_sample_returns_none_without_reading_and_still_frees(self):
        proxy, interface = self.proxy(0, bytes(176))

        self.assertIsNone(proxy.hv_diag_sample(2))
        self.assertEqual(interface.reads, [])
        self.assertEqual(proxy.heap.frees, [0x1000])


if __name__ == "__main__":
    unittest.main()

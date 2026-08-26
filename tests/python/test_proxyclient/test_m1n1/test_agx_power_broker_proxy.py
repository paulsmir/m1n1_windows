import ast
import re
from pathlib import Path
import unittest


class TestAgxPowerBrokerProxy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[4]
        cls.c_header = (cls.root / "src/proxy.h").read_text()
        cls.c_proxy = (cls.root / "src/proxy.c").read_text()
        cls.python_source = (cls.root / "proxyclient/m1n1/proxy.py").read_text()
        cls.python_tree = ast.parse(cls.python_source)
        cls.hv_source = (cls.root / "proxyclient/m1n1/hv/__init__.py").read_text()

    def test_opcode_is_new_and_identical_in_c_and_python(self):
        match = re.search(r"P_HV_MAP_AGX_POWER\s*=\s*(0x[0-9a-fA-F]+)", self.c_header)
        self.assertIsNotNone(match)
        self.assertEqual(int(match.group(1), 16), 0xC1F)

        proxy = next(
            node
            for node in self.python_tree.body
            if isinstance(node, ast.ClassDef) and node.name == "M1N1Proxy"
        )
        assignment = next(
            item
            for item in proxy.body
            if isinstance(item, ast.Assign)
            and isinstance(item.targets[0], ast.Name)
            and item.targets[0].id == "P_HV_MAP_AGX_POWER"
        )
        self.assertEqual(ast.literal_eval(assignment.value), 0xC1F)

    def test_proxy_surface_takes_no_guest_address_or_platform_path(self):
        proxy = next(
            node
            for node in self.python_tree.body
            if isinstance(node, ast.ClassDef) and node.name == "M1N1Proxy"
        )
        method = next(
            item
            for item in proxy.body
            if isinstance(item, ast.FunctionDef) and item.name == "hv_map_agx_power_broker"
        )
        self.assertEqual([arg.arg for arg in method.args.args], ["self"])
        call = next(node for node in ast.walk(method) if isinstance(node, ast.Call))
        self.assertEqual(len(call.args), 1)
        self.assertEqual(call.args[0].attr, "P_HV_MAP_AGX_POWER")

    def test_mapping_is_explicitly_opt_in_and_reserved(self):
        self.assertIn('WOM1_AGX_G2_POWER_BROKER', self.hv_source)
        self.assertRegex(
            self.hv_source,
            r'os\.environ\.get\("WOM1_AGX_G2_POWER_BROKER",\s*"0"\)\s*!=\s*"1"',
        )
        self.assertIn('"AGX-POWER-BROKER", TraceMode.RESERVED', self.hv_source)

    def test_c_dispatch_has_a_fixed_no_argument_mapper(self):
        case = re.search(
            r"case\s+P_HV_MAP_AGX_POWER:(.*?)(?:\n\s*case\s+|\n\s*default:)",
            self.c_proxy,
            re.S,
        )
        self.assertIsNotNone(case)
        body = case.group(1)
        self.assertIn("hv_agx_power_broker_map()", body)
        self.assertNotIn("request->args", body)


if __name__ == "__main__":
    unittest.main()

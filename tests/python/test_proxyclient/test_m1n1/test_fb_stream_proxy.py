import ast
from pathlib import Path
import re
import unittest


class TestFramebufferStreamProxy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[4]
        cls.python_source = (cls.root / "proxyclient/m1n1/proxy.py").read_text()
        cls.python_tree = ast.parse(cls.python_source)
        cls.c_header = (cls.root / "src/proxy.h").read_text()

    def test_event_type_is_stable(self):
        event = next(
            node
            for node in self.python_tree.body
            if isinstance(node, ast.ClassDef) and node.name == "EVENT"
        )
        values = {
            item.targets[0].id: ast.literal_eval(item.value)
            for item in event.body
            if isinstance(item, ast.Assign) and isinstance(item.targets[0], ast.Name)
        }
        self.assertEqual(values["FRAMEBUFFER"], 3)

    def test_opcode_matches_c_and_python(self):
        match = re.search(r"P_HV_FB_STREAM_CONFIG\s*=\s*(0x[0-9a-fA-F]+)", self.c_header)
        self.assertIsNotNone(match)
        self.assertEqual(int(match.group(1), 16), 0xC1A)

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
            and item.targets[0].id == "P_HV_FB_STREAM_CONFIG"
        )
        self.assertEqual(ast.literal_eval(assignment.value), 0xC1A)

    def test_config_method_forwards_geometry(self):
        proxy = next(
            node
            for node in self.python_tree.body
            if isinstance(node, ast.ClassDef) and node.name == "M1N1Proxy"
        )
        method = next(
            item
            for item in proxy.body
            if isinstance(item, ast.FunctionDef) and item.name == "hv_fb_stream_config"
        )
        call = next(node for node in ast.walk(method) if isinstance(node, ast.Call))
        self.assertIsInstance(call.func, ast.Attribute)
        self.assertEqual(call.func.attr, "request")
        self.assertIsInstance(call.args[0], ast.Attribute)
        self.assertEqual(call.args[0].attr, "P_HV_FB_STREAM_CONFIG")
        args = [arg.id for arg in call.args[1:] if isinstance(arg, ast.Name)]
        self.assertEqual(args, ["ipa", "size", "width", "height", "stride"])


if __name__ == "__main__":
    unittest.main()

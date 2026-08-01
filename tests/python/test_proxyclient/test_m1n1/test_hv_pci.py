import ast
from pathlib import Path
import unittest


class TestHVPci(unittest.TestCase):
    def test_map_pci_preserves_the_c_ecam_hook(self):
        root = Path(__file__).resolve().parents[4]
        source = (root / "proxyclient/m1n1/hv/__init__.py").read_text()
        tree = ast.parse(source)
        map_pci = next(
            node
            for node in ast.walk(tree)
            if isinstance(node, ast.FunctionDef) and node.name == "map_pci"
        )
        calls = [node for node in ast.walk(map_pci) if isinstance(node, ast.Call)]
        init_line = next(
            node.lineno
            for node in calls
            if isinstance(node.func, ast.Attribute) and node.func.attr == "hv_pci_init"
        )
        reserve = next(
            node
            for node in calls
            if isinstance(node.func, ast.Attribute) and node.func.attr == "add_tracer"
        )
        self.assertGreater(reserve.lineno, init_line)
        self.assertEqual(ast.literal_eval(reserve.args[1]), "PCI-ECAM")
        self.assertIsInstance(reserve.args[2], ast.Attribute)
        self.assertEqual(reserve.args[2].attr, "RESERVED")


if __name__ == "__main__":
    unittest.main()

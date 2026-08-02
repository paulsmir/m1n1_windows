import pathlib
import unittest


HV_C = pathlib.Path(__file__).resolve().parents[4] / "src" / "hv.c"


def function_body(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : pos]
    raise AssertionError(f"unterminated function {signature}")


class SecondaryLaunchContractTest(unittest.TestCase):
    def test_secondary_launch_uses_persistent_per_cpu_context(self):
        source = HV_C.read_text()
        start = function_body(source, "void hv_start_secondary(int cpu")
        enter = function_body(source, "static void hv_enter_secondary(")
        self.assertIn("secondary_launch[cpu]", start)
        self.assertIn("memcpy(launch->regs, regs, sizeof(launch->regs))", start)
        self.assertIn("smp_call1(cpu, hv_enter_secondary, (u64)launch)", start)
        self.assertNotIn("(u64)regs", start)
        self.assertIn("launch->regs", enter)

    def test_secondary_membership_uses_target_cpu(self):
        source = HV_C.read_text()
        start = function_body(source, "void hv_start_secondary(int cpu")
        self.assertIn("BIT(cpu)", start)
        self.assertNotIn("BIT(smp_id())", start)


if __name__ == "__main__":
    unittest.main()

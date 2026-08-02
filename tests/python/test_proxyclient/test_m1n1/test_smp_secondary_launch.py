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

    def test_percpu_diagnostics_capture_bounded_x18_transitions(self):
        source = HV_C.read_text()
        diag = function_body(source, "void hv_percpu_diag_tick(struct exc_info *ctx)")
        self.assertIn("ctx->regs[18]", diag)
        self.assertIn("HV DIAG X18:", diag)
        self.assertIn("HV_DIAG_X18_REPORT_LIMIT", source)
        self.assertIn("d->x18_reports < HV_DIAG_X18_REPORT_LIMIT", diag)

    def test_guest_wfi_keeps_architectural_register_context(self):
        source = HV_C.read_text()
        configure = function_body(source, "static void hv_configure_guest_wfi(void)")
        primary = function_body(source, "void hv_init(void)")
        secondary = function_body(source, "static void hv_init_secondary(")

        self.assertIn("CYC_OVRD_WFI_MODE(2)", configure)
        self.assertIn("hv_configure_guest_wfi()", primary)
        self.assertIn("hv_configure_guest_wfi()", secondary)
        self.assertNotIn("CYC_OVRD_WFI_MODE(0)", source)


if __name__ == "__main__":
    unittest.main()

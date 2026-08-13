import pathlib
import unittest


HV_C = pathlib.Path(__file__).resolve().parents[4] / "src" / "hv.c"
SMP_C = pathlib.Path(__file__).resolve().parents[4] / "src" / "smp.c"


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
    def test_mailbox_publication_and_completion_use_explicit_barriers(self):
        source = SMP_C.read_text()
        call = function_body(source, "void smp_call4(int cpu")
        wait = function_body(source, "u64 smp_wait(int cpu)")
        secondary = function_body(source, "void smp_secondary_entry(void)")

        self.assertIn('sysop("dsb sy")', call)
        self.assertIn("target->target = (u64)func", call)
        self.assertIn("while (target->flag == flag)", call)
        self.assertIn("while (!(target = me->target))", secondary)
        self.assertIn("me->target = 0", secondary)
        self.assertIn("while (target->target)", wait)

    def test_secondary_completion_does_not_require_lse_rmw(self):
        source = SMP_C.read_text()
        secondary = function_body(source, "void smp_secondary_entry(void)")

        # This path executes before the secondary has completed its first MMU
        # setup callback.  Keep it on ordinary load/store plus the existing
        # barrier protocol; an atomic RMW lets Clang emit LDADD here.
        self.assertNotIn("__atomic_add_fetch(&me->flag", secondary)
        self.assertIn("me->flag++", secondary)

    def test_wfe_transition_wakes_existing_wfi_waiters(self):
        source = SMP_C.read_text()
        transition = function_body(source, "void smp_set_wfe_mode(bool new_mode)")

        self.assertIn("wfe_mode = new_mode", transition)
        self.assertIn('sysop("dsb sy")', transition)
        self.assertIn("smp_send_ipi(cpu)", transition)
        self.assertLess(transition.index("wfe_mode = new_mode"),
                        transition.index("smp_send_ipi(cpu)"))

    def test_every_mailbox_wakes_both_wfi_and_wfe_waiters(self):
        source = SMP_C.read_text()
        call = function_body(source, "void smp_call4(int cpu")
        secondary = function_body(source, "void smp_secondary_entry(void)")

        self.assertIn("smp_send_ipi(cpu)", call)
        self.assertIn('sysop("sev")', call)
        self.assertLess(call.index("smp_send_ipi(cpu)"), call.index('sysop("sev")'))
        # A target may see the mailbox before sleeping at all.  The physical
        # IPI therefore has to be acknowledged after the wait loop, not only
        # inside its WFI branch.
        wait_end = secondary.index('sysop("dmb sy")')
        ack = secondary.index("SYS_IMP_APL_IPI_SR_EL1")
        self.assertLess(wait_end, ack)

    def test_hv_init_returns_secondary_cpu_readiness(self):
        source = HV_C.read_text()
        primary = function_body(source, "bool hv_init(void)")

        self.assertIn("secondaries_ready = smp_start_secondaries()", primary)
        self.assertIn("return secondaries_ready", primary)

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
        primary = function_body(source, "bool hv_init(void)")
        secondary = function_body(source, "static void hv_init_secondary(")

        self.assertIn("CYC_OVRD_WFI_MODE(2)", configure)
        self.assertIn("hv_configure_guest_wfi()", primary)
        self.assertIn("hv_configure_guest_wfi()", secondary)
        self.assertNotIn("CYC_OVRD_WFI_MODE(0)", source)

    def test_every_secondary_enables_both_guest_timer_fiq_routes(self):
        source = HV_C.read_text()
        secondary = function_body(source, "static void hv_init_secondary(")
        self.assertIn("SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2", secondary)
        self.assertIn("VM_TMR_FIQ_ENA_ENA_P | VM_TMR_FIQ_ENA_ENA_V", secondary)
        self.assertLess(secondary.index("SYS_IMP_APL_VM_TMR_FIQ_ENA_EL2"),
                        secondary.index("hv_arm_tick(true)"))


if __name__ == "__main__":
    unittest.main()

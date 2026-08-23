from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[4]


class ProxyHVInitContractTest(unittest.TestCase):
    def test_proxy_returns_hv_and_pci_readiness(self):
        source = (ROOT / "src/proxy.c").read_text()

        self.assertIn("reply->retval = hv_init();", source)
        self.assertIn("reply->retval = hv_pci_init(", source)

    def test_autonomous_stage_rejects_failed_hv_init(self):
        source = (ROOT / "src/hv_autonomous_runtime.c").read_text()

        self.assertIn("if (!hv_init())", source)

    def test_host_requires_explicit_opt_in_for_legacy_zero_result(self):
        source = (ROOT / "proxyclient/m1n1/hv/__init__.py").read_text()

        self.assertIn("WOM1_ALLOW_LEGACY_HV_INIT_ZERO", source)
        self.assertIn('legacy_zero != "1"', source)
        self.assertIn('raise RuntimeError("secondary CPU startup failed")', source)
        self.assertIn("pci_init_result = self.p.hv_pci_init", source)
        self.assertIn("if not pci_init_result and legacy_zero != \"1\"", source)


if __name__ == "__main__":
    unittest.main()

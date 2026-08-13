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


if __name__ == "__main__":
    unittest.main()

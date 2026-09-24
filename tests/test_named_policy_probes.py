import copy
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts/agent"))
from probe_named_policy import cases
from single_token import model_messages,format_messages,ACTIONS


class PolicyProbeTests(unittest.TestCase):
    def test_cases_are_paired_bounded_and_never_send_expected_choices(self):
        for reserved in (False,True):
            rows=cases(reserved)
            self.assertGreater(len(rows),30)
            self.assertEqual(len({n for n,_,_ in rows}),len(rows))
            for name,history,allowed in rows:
                original=copy.deepcopy(history)
                rendered=format_messages(model_messages(history,"policy"),"named")
                self.assertEqual(history,original)
                self.assertTrue(set(allowed)<=set(ACTIONS))
                encoded=json.dumps(rendered)
                for excluded in (name,"expected_choices","source_preassembled","offline-body","completed_clock_seconds"):
                    self.assertNotIn(excluded,encoded)
                self.assertLess(len(rendered),40)

    def test_faults_cannot_pass_as_normal_progress(self):
        rows={name:(history,allowed) for name,history,allowed in cases()}
        self.assertEqual(rows["failed-sealant"][1],("B","M"))
        self.assertEqual(rows["premature-thermal-retries"][1],("C",))
        self.assertEqual(rows["must-reinspect-after-placement"][1],("D",))
        self.assertEqual(rows["placed-without-sealant"][1],("M",))
        for name,(history,allowed) in rows.items():
            if "stale_reply" in name:
                result=json.loads(history[-1]["content"])
                self.assertFalse(result["valid"])
                self.assertNotIn("E",allowed)
                self.assertNotIn("I",allowed)


if __name__=="__main__":unittest.main()

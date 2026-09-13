import math
import unittest
from dark_factory.gates_scores import topk_label_bounds, decision_from_bounds

POLICY={"minimum_label_mass":.5,"minimum_conditional_probability":.8}
def candidates(probabilities):
    return [{"token": token, "logprob":math.log(p)} for token,p in probabilities]

class TopKBoundsTests(unittest.TestCase):
    def test_complete_labels_match_original_softmax(self):
        b=topk_label_bounds(candidates([("G",.72),("A",.05),("R",.03),("other",.2)]),4)
        self.assertAlmostEqual(b["label_mass_lower_bound"],.8)
        self.assertAlmostEqual(b["conditional_label_lower_bounds"]["G"],.9)
        self.assertEqual(b["conditional_label_lower_bounds"],b["conditional_label_upper_bounds"])
        self.assertEqual(decision_from_bounds(b,POLICY)[0],"G")

    def test_tiny_omitted_classes_do_not_force_a_hold(self):
        b=topk_label_bounds(candidates([("G",.99),("other",.001)]),2)
        self.assertEqual(b["missing_labels"],["A","R"])
        self.assertAlmostEqual(b["label_mass_upper_bound"],.992)
        self.assertAlmostEqual(b["conditional_label_lower_bounds"]["G"],.99/.992)
        self.assertEqual(decision_from_bounds(b,POLICY)[0],"G")

    def test_insufficient_total_mass_or_ambiguous_bounds_hold(self):
        b=topk_label_bounds(candidates([("other",.6),("G",.2)]),2)
        self.assertEqual(decision_from_bounds(b,POLICY)[1],"insufficient_label_support")
        b=topk_label_bounds(candidates([("G",.5),("other",.15)]),2)
        self.assertEqual(decision_from_bounds(b,POLICY)[0],"A")

    def test_every_feasible_missing_probability_lies_in_bounds(self):
        b=topk_label_bounds(candidates([("G",.61),("A",.11),("other",.09)]),3)
        for missing in [i*.0009 for i in range(101)]:
            exact={"G":.61/(.72+missing),"A":.11/(.72+missing),"R":missing/(.72+missing)}
            for label in "GAR":
                self.assertLessEqual(b["conditional_label_lower_bounds"][label],exact[label]+1e-12)
                self.assertGreaterEqual(b["conditional_label_upper_bounds"][label]+1e-12,exact[label])
        self.assertEqual(decision_from_bounds(b,POLICY)[0],"A")

    def test_invalid_topk_rejected(self):
        for data,n in ((candidates([("G",.9)]),2),(candidates([("G",.7),("G",.2)]),2),
                       ([{"token":"G","logprob":float("nan")}],1),(candidates([("G",.9),("A",.9)]),2)):
            with self.assertRaises(ValueError): topk_label_bounds(data,n)

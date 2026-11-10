"""Routing checks using known probe outcomes and simulated teacher replies."""

from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "resources" / "training"))
import boundary_lab as lab


class BoundaryRoutingTest(unittest.TestCase):
    def setUp(self):
        self.case = lab.make_split(True)[0]
        self.zero_weights = (0.0,) * lab.DIM
        self.tiers = [["http://127.0.0.1:10001", "small"], ["http://127.0.0.1:10002", "large"]]

    @patch.object(lab, "teacher_available")
    def test_verified_local_repair_stops_before_network(self, available):
        result = lab.route_case(self.case, self.zero_weights, [0, 1, 1, 1, 1], self.tiers)
        self.assertTrue(result["verified"])
        self.assertEqual(result["selected_lane"], "boundary-ranker")
        available.assert_not_called()

    @patch.object(lab, "teacher_available", return_value=True)
    @patch.object(lab, "query_teacher")
    def test_failed_small_choice_escalates_to_large(self, query, _available):
        query.side_effect = [{"choice": "B", "finish": "stop"}, {"choice": "D", "finish": "stop"}]
        result = lab.route_case(self.case, self.zero_weights, [1, 2, 1, 0, 1], self.tiers)
        self.assertEqual(result["selected_lane"], "large")
        self.assertEqual([row["status"] for row in result["attempts"]], ["failed-probes", "failed-probes", "passed"])
        self.assertEqual([call.args[2] for call in query.call_args_list], ["small", "large"])

    @patch.object(lab, "teacher_available", side_effect=[False, True])
    @patch.object(lab, "query_teacher", return_value={"choice": None, "finish": "length"})
    def test_unavailable_and_truncated_do_not_admit_a_repair(self, query, _available):
        result = lab.route_case(self.case, self.zero_weights, [1, 2, 1, 0, 1], self.tiers)
        self.assertFalse(result["verified"])
        self.assertNotIn("repair", result)
        self.assertEqual([row["status"] for row in result["attempts"]],
                         ["failed-probes", "unavailable", "no-usable-choice"])
        query.assert_called_once_with(self.case, *self.tiers[1], constrained=False)


if __name__ == "__main__":
    unittest.main()

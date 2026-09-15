"""Panel camera QA distinguishes modality from physical scan family."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location("panel_scan_qa",Path(__file__).resolve().parents[1]/"scripts/verify_inspection_arm.py")
QA=importlib.util.module_from_spec(spec)
spec.loader.exec_module(QA)


def fixture(stage):
    family="nir" if stage in ("panel_presence","panel_seated") else stage
    value={"nir":0.,"adhesive":10.,"weld":20.}[family]
    poses=[{"spot_index":i,"joint_angles_deg":[value]*6,"lens_location_cm":[i*50.,value,100.],"lens_rotation_deg":[0.,0.,0.]} for i in range(3)]
    arm={"schema":"authored-r4-inspection-scan-v1","configured":True,"settled":True,"requested_spot":0,
         "transition_seconds":.8,"elapsed_seconds":.8,"progress":1.,"inspection_extension":1.,
         "joint_ids":["robot_nx_ny_"+r for r in ("base","shoulder","elbow","forearm_roll","wrist","tool_roll")],
         "current_joint_angles_deg":[value]*6,"target_joint_angles_deg":[value]*6,
         "fixed_mount_location_cm":[0.,0.,-24.],"fixed_mount_quaternion_xyzw":[0.,0.,0.,1.],
         "optical_offset_cm":[21.,0.,0.],"poses":poses,"lens_location_cm":poses[0]["lens_location_cm"],
         "lens_rotation_deg":[0.,0.,0.],"scan_family":family,"scan_modality":"nir" if family=="nir" else "thermal"}
    view={"stage":stage,"spot":0,"sensor_warmup_captures":3,"lens_location_cm":arm["lens_location_cm"],"lens_rotation_deg":arm["lens_rotation_deg"]}
    return arm,view


class PanelScanFamilyTests(unittest.TestCase):
    def test_distinct_adhesive_and_weld_tables_are_stable(self):
        audit=QA.PanelAudit("unused","runtime")
        for i,stage in enumerate(("panel_presence","adhesive","panel_seated","weld","adhesive","weld")):
            arm,view=fixture(stage);audit.check_arm(arm,view,str(i))
        self.assertTrue(all(audit.result["checks"].values()),audit.result["checks"])
        self.assertTrue(audit.pose_tables_unchanged())
        self.assertEqual({key:len(value) for key,value in audit.modality_pose_tables.items()},{"nir":2,"adhesive":2,"weld":2})

    def test_wrong_family_or_modality_is_rejected(self):
        for key,bad in (("scan_family","weld"),("scan_family",None),("scan_modality","nir")):
            with self.subTest(key=key,bad=bad):
                audit=QA.PanelAudit("unused","runtime");arm,view=fixture("adhesive");arm[key]=bad
                audit.check_arm(arm,view,"bad")
                self.assertFalse(all(audit.result["checks"].values()))

    def test_unprimed_published_capture_is_rejected(self):
        for count in (None,0,1,2,True):
            with self.subTest(count=count):
                audit=QA.PanelAudit("unused","runtime");arm,view=fixture("panel_presence")
                view["sensor_warmup_captures"]=count;audit.check_arm(arm,view,"bad")
                self.assertFalse(audit.result["checks"]["bad_actual_sensor_prime_captures"])

    def test_within_family_physical_drift_and_authored_edits_fail(self):
        for kind,delta in (("lens_location_cm",.001),("joint_angles_deg",1e-10)):
            with self.subTest(kind=kind):
                audit=QA.PanelAudit("unused","runtime");arm,view=fixture("adhesive")
                audit.check_arm(arm,view,"first")
                changed=copy.deepcopy(arm);changed["poses"][2][kind][0]+=delta
                audit.check_arm(changed,view,"second")
                self.assertFalse(audit.pose_tables_unchanged())
        audit=QA.PanelAudit("unused","runtime");arm,view=fixture("adhesive");audit.check_arm(arm,view,"first")
        changed=copy.deepcopy(arm);changed["poses"][2]["lens_location_cm"][0]+=2e-10
        audit.check_arm(changed,view,"second")
        self.assertTrue(audit.pose_tables_unchanged())

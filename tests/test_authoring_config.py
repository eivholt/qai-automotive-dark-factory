"""Reject malformed editor recipes without damaging the last valid recipe."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / 'unreal/Plugins/DarkFactoryAuthoring/Content/Python/df_authoring_config.py'
spec = importlib.util.spec_from_file_location('authoring_config_test', MODULE)
config = importlib.util.module_from_spec(spec)
spec.loader.exec_module(config)


class AuthoringConfigTests(unittest.TestCase):
    def test_checked_in_recipe(self):
        self.assertEqual(config.load(ROOT / 'unreal/Config/Authoring/preview.json')['schema_version'], 1)

    def test_all_poses_roundtrip(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'preview.json'
            for pose in config.POSES:
                value = dict(schema_version=1, pose=pose, viewport=None)
                config.save(path, value)
                self.assertEqual(config.load(path), value)

    def test_invalid_input_preserves_file(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'preview.json'
            value = dict(schema_version=1, pose='AwaitingPanel', viewport=None)
            config.save(path, value)
            before = path.read_bytes()
            for change in ({'pose': 'Welding'}, {'schema_version': True}, {'schema_version': 2},
                           {'model_threshold': 0.1}, {'viewport': {'location_cm': [0, 0, 0]}}):
                with self.assertRaises(ValueError):
                    config.save(path, dict(value, **change))
                self.assertEqual(path.read_bytes(), before)

    def test_viewport_validation(self):
        value = dict(schema_version=1, pose='PanelPlaced', viewport={
            'location_cm': [1, 2, 3], 'rotation_deg': [-20, 55, 0]})
        self.assertEqual(config.validate(value), value)
        for bad in (float('nan'), float('inf'), 100001, True, '0'):
            value['viewport']['location_cm'][0] = bad
            with self.assertRaises(ValueError):
                config.validate(value)

    def test_reject_unknown_fields(self):
        with self.assertRaises(ValueError):
            config.validate({'schema_version': 1, 'pose': 'PanelPlaced', 'viewport': None, 'actors': []})


if __name__ == '__main__':
    unittest.main()

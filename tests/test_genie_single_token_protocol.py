"""Exercise the real worker against a fake Genie ABI; never performs model inference.

Set DF_GENIE_TEST_SDK to headers plus SDK examples' nlohmann include tree. Tests
require native g++, OpenSSL development headers, and Linux. The fake library and
test executable live only in a temporary directory.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class GenieProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = os.environ.get("DF_GENIE_TEST_SDK")
        if not sdk or not shutil.which("g++") or not sys.platform.startswith("linux"):
            raise unittest.SkipTest("Set DF_GENIE_TEST_SDK on Linux with g++ and OpenSSL headers")
        sdk = Path(sdk)
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        source = Path(__file__).resolve().parents[1]
        include = Path(os.environ.get("DF_GENIE_JSON_INCLUDE", sdk / "examples/Genie/Genie/src/qualla/include"))
        flags = ["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-I" + str(sdk / "include")]
        subprocess.run(flags + ["-shared", "-fPIC", str(source / "tests/genie_single_token_fake_runtime.cpp"),
                                "-o", str(cls.directory / "libGenie.so")], check=True, timeout=60)
        cls.worker = cls.directory / "worker"
        subprocess.run(flags + ["-I" + str(include), str(source / "scripts/agent/genie_single_token_worker.cpp"),
                                "-L" + str(cls.directory), "-Wl,-rpath," + str(cls.directory),
                                "-lGenie", "-lcrypto", "-ldl", "-o", str(cls.worker)], check=True, timeout=120)
        (cls.directory / "tokenizer.json").write_text('{}')
        (cls.directory / "context.bin").write_bytes(b"TEST SHIM ONLY - NO MODEL")
        config = {"dialog": {"type": "basic", "context": {"size": 32, "n-vocab": 2048,
                                                       "eos-token": [2, 3], "eot-token": 4},
                  "tokenizer": {"path": "tokenizer.json"}, "sampler": {"version": 1},
                  "engine": {"backend": {"type": "QnnHtp"},
                             "model": {"type": "binary", "binary": {"ctx-bins": ["context.bin"]}}}}}
        (cls.directory / "config.json").write_text(json.dumps(config))

    def run_requests(self, *requests, prefix="", config_path=None, minor=None):
        env = dict(os.environ)
        if minor is not None:
            env["DF_FAKE_GENIE_MINOR"] = str(minor)
        result = subprocess.run([str(self.worker), str(config_path or self.directory / "config.json"),
                                 str(self.directory / "tokenizer.json"), str(self.directory)],
                                input=prefix + "".join(json.dumps({"request_id": str(i), **request}) + "\n"
                                                      for i, request in enumerate(requests)),
                                capture_output=True, text=True, timeout=15, check=True, env=env)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertTrue(rows[0]["ready"])
        self.assertEqual(rows[0]["action_token_ids"], {chr(65+i): 1065+i for i in range(13)})
        return rows[1:]

    def test_scores_and_exact_token_protocol(self):
        tokenized, decoded, first, repeated = self.run_requests(
            {"op": "tokenize", "content": "A", "add_special": False},
            {"op": "decode", "tokens": [1065]},
            {"op": "completion", "tokens": [1, 2, 3]},
            {"op": "completion", "tokens": [1, 2, 3]})
        self.assertEqual(tokenized["tokens"], [1065])
        self.assertEqual(decoded["content"], "A")
        self.assertEqual(first["tokens"], [1065])
        self.assertEqual(first["raw_winner_token_id"], 1065)
        self.assertEqual(first["native_emitted_token_count"], 1)
        self.assertFalse(first["native_eos_suppressed"])
        self.assertEqual(len(first["completion_probabilities"][0]["top_logprobs"]), 128)
        self.assertEqual(first["prompt_tokens_sha256"], hashlib.sha256(b'[1,2,3]').hexdigest())
        self.assertEqual(repeated["timings"]["cache_mode"], "reset")
        self.assertEqual(repeated["timings"]["retained_context_tokens"], 0)
        self.assertEqual(first["tokens"], repeated["tokens"])

    def test_append_submits_actual_action_once_and_matches_reset(self):
        initial, appended, reset, wrong_action, changed, repeated = self.run_requests(
            {"op": "completion", "tokens": [1, 2, 3]},
            {"op": "completion", "tokens": [1, 2, 3, 1065, 4, 5], "cache_mode": "append"},
            {"op": "completion", "tokens": [1, 2, 3, 1065, 4, 5]},
            {"op": "completion", "tokens": [1, 2, 3, 1065, 4, 5, 1066, 6], "cache_mode": "append"},
            {"op": "completion", "tokens": [9, 2, 3], "cache_mode": "append"},
            {"op": "completion", "tokens": [9, 2, 3], "cache_mode": "append"})
        self.assertEqual(initial["timings"]["cache_mode"], "reset")
        self.assertEqual(appended["timings"]["cache_mode"], "append")
        self.assertEqual(appended["submitted_tokens"], 3)
        self.assertEqual(appended["submitted_tokens_sha256"], hashlib.sha256(b'[1065,4,5]').hexdigest())
        self.assertEqual(appended["timings"]["context_occupancy"], 6)
        self.assertEqual(appended["timings"]["retained_context_tokens"], 3)
        self.assertIsNone(appended["timings"]["reused_tokens_measured"])
        self.assertEqual(appended["completion_probabilities"], reset["completion_probabilities"])
        for row in (wrong_action, changed, repeated):
            self.assertEqual(row["timings"]["cache_mode"], "reset")
            self.assertTrue(row["timings"]["cache_fallback"])

    def test_global_winner_is_never_masked(self):
        result, = self.run_requests({"op": "completion", "tokens": [1, 666]})
        self.assertEqual(result["tokens"], [230])
        self.assertEqual(result["completion_probabilities"][0]["top_logprobs"][0]["id"], 230)
        self.assertEqual(len(result["action_probabilities_full_vocab"]), 13)

    def test_opt_in_interior_rewind_submits_full_prompt_and_gets_fresh_scores(self):
        first, changed, reference, appended = self.run_requests(
            {"op": "completion", "tokens": [1, 2, 3, 4]},
            {"op": "completion", "tokens": [1, 2, 7, 8], "cache_mode": "append_rewind"},
            {"op": "completion", "tokens": [1, 2, 7, 8]},
            {"op": "completion", "tokens": [1, 2, 7, 8, 1065, 9], "cache_mode": "append_rewind"})
        self.assertEqual(first["timings"]["cache_mode"], "reset")
        self.assertEqual(changed["timings"]["cache_mode"], "rewind")
        self.assertEqual(changed["submitted_tokens"], 4)
        self.assertEqual(changed["submitted_tokens_sha256"], hashlib.sha256(b'[1,2,7,8]').hexdigest())
        self.assertEqual(changed["timings"]["rewind_expected_prefix_tokens"], 2)
        self.assertEqual(changed["timings"]["expected_fresh_prompt_n"], 2)
        self.assertEqual(changed["native_sampler_callback_count"], 1)
        self.assertEqual(changed["native_emitted_token_count"], 1)
        self.assertEqual(changed["completion_probabilities"], reference["completion_probabilities"])
        self.assertEqual(changed["action_probabilities_full_vocab"], reference["action_probabilities_full_vocab"])
        self.assertEqual(appended["timings"]["cache_mode"], "append")

    def test_rewind_boundaries_without_interior_divergence_reset(self):
        for candidate in ([1, 2, 3], [1, 2], [1, 2, 3, 99], [9, 2, 3]):
            with self.subTest(candidate=candidate):
                _, result = self.run_requests(
                    {"op": "completion", "tokens": [1, 2, 3]},
                    {"op": "completion", "tokens": candidate, "cache_mode": "append_rewind"})
                self.assertEqual(result["timings"]["cache_mode"], "reset")
                self.assertEqual(result["timings"]["retained_context_tokens"], 0)
                self.assertTrue(result["timings"]["cache_fallback"])

    def test_rewind_refuses_old_or_unreviewed_api(self):
        for minor in (18, 19, 21):
            with self.subTest(minor=minor):
                result, = self.run_requests(
                    {"op": "completion", "tokens": [1, 2], "cache_mode": "append_rewind"}, minor=minor)
                self.assertIn("requires reviewed Genie API 1.20.0", result["error"])
                self.assertNotIn("tokens", result)

    def test_rewind_saved_output_without_fresh_sampler_is_rejected(self):
        _, unsafe, recovered = self.run_requests(
            {"op": "completion", "tokens": [1, 2, 3]},
            {"op": "completion", "tokens": [1, 2, 676], "cache_mode": "append_rewind"},
            {"op": "completion", "tokens": [1, 2, 5], "cache_mode": "append_rewind"})
        self.assertIn("error", unsafe)
        self.assertNotIn("tokens", unsafe)
        self.assertEqual(recovered["timings"]["cache_mode"], "reset")
        self.assertEqual(recovered["native_sampler_callback_count"], 1)

    def test_matching_fresh_token_without_end_is_rejected_and_resets(self):
        for mode in ("reset", "append_rewind"):
            with self.subTest(mode=mode):
                _, incomplete, recovered = self.run_requests(
                    {"op": "completion", "tokens": [1, 2, 3]},
                    {"op": "completion", "tokens": [1, 2, 677], "cache_mode": mode},
                    {"op": "completion", "tokens": [1, 2, 5], "cache_mode": "append_rewind"})
                self.assertIn("error", incomplete)
                self.assertNotIn("tokens", incomplete)
                self.assertEqual(recovered["timings"]["cache_mode"], "reset")
                self.assertEqual(recovered["native_sampler_callback_count"], 1)

    def test_configured_eos_winners_preserve_scores_despite_native_suppression(self):
        results = self.run_requests(*({"op": "completion", "tokens": [1, token]}
                                      for token in (670, 674, 675)))
        for result, winner in zip(results, (2, 3, 4)):
            self.assertEqual(result["raw_winner_token_id"], winner)
            self.assertEqual(result["tokens"], [winner])
            self.assertEqual(result["native_emitted_token_count"], 0)
            self.assertTrue(result["native_eos_suppressed"])
            self.assertEqual(result["native_sampler_callback_count"], 1)
            self.assertEqual(result["completion_probabilities"][0]["top_logprobs"][0]["id"], winner)
            self.assertEqual(len(result["action_probabilities_full_vocab"]), 13)

    def test_unexplained_missing_output_and_abort_are_not_eos(self):
        results = self.run_requests(*({"op": "completion", "tokens": [1, token]}
                                      for token in (671, 672, 673, 5)))
        for result in results[:3]:
            self.assertIn("error", result)
            self.assertNotIn("tokens", result)
        self.assertEqual(results[3]["tokens"], [1065])
        self.assertEqual(results[3]["timings"]["cache_mode"], "reset")

    def test_only_configured_terminal_ids_allow_empty_native_end(self):
        config = json.loads((self.directory / "config.json").read_text())
        config["dialog"]["context"]["eos-token"] = 2
        config["dialog"]["context"].pop("eot-token")
        config_path = self.directory / "scalar-eos.json"
        config_path.write_text(json.dumps(config))
        accepted, unconfigured = self.run_requests(
            {"op": "completion", "tokens": [1, 670]},
            {"op": "completion", "tokens": [1, 674]}, config_path=config_path)
        self.assertEqual(accepted["raw_winner_token_id"], 2)
        self.assertTrue(accepted["native_eos_suppressed"])
        self.assertIn("error", unconfigured)
        self.assertNotIn("tokens", unconfigured)

    def test_rejects_invalid_scores_then_recovers(self):
        results = self.run_requests(*({"op": "completion", "tokens": [1, token]}
                                      for token in (667, 668, 669, 5)))
        for result in results[:3]:
            self.assertIn("error", result)
            self.assertNotIn("tokens", result)
        self.assertEqual(results[3]["tokens"], [1065])
        self.assertEqual(results[3]["timings"]["cache_mode"], "reset")

    def test_rejects_modifiers_overflow_and_bad_tokens(self):
        bad = [{"grammar": "root ::= A"}, {"logit_bias": [[230, -100]]},
               {"n_predict": 2}, {"samplers": ["top_k"]}, {"temperature": .5},
               {"tokens": [1] * 32}, {"tokens": [-1]}, {"tokens": [2048]}, {"tokens": []},
               {"cache_mode": "rewind"}]
        results = self.run_requests(*({"op": "completion", "tokens": [1, 2], **case} for case in bad))
        self.assertTrue(all("error" in result for result in results))

    def test_oversize_line_does_not_break_next_request(self):
        error, good = self.run_requests({"op": "health"}, prefix="x" * (1024*1024 + 1) + "\n")
        self.assertIn("error", error)
        self.assertTrue(good["ready"])

    def test_startup_rejects_configured_model_or_vocabulary_modifiers(self):
        for key, value in (("grammar", {"root": "A"}), ("lora", {"adapter": "trained.bin"})):
            config = json.loads((self.directory / "config.json").read_text())
            config["dialog"][key] = value
            path = self.directory / ("bad-" + key + ".json")
            path.write_text(json.dumps(config))
            result = subprocess.run([str(self.worker), str(path), str(self.directory / "tokenizer.json"),
                                     str(self.directory)], capture_output=True, text=True, timeout=15)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertIn("LoRA or grammar", result.stderr)

    def test_startup_rejects_eos_overlapping_an_action(self):
        config = json.loads((self.directory / "config.json").read_text())
        config["dialog"]["context"]["eos-token"] = 1065
        config_path = self.directory / "action-eos.json"
        config_path.write_text(json.dumps(config))
        result = subprocess.run([str(self.worker), str(config_path),
                                 str(self.directory / "tokenizer.json"), str(self.directory)],
                                capture_output=True, text=True, timeout=15)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIn("EOS token overlaps an action", result.stderr)


if __name__ == "__main__":
    unittest.main()

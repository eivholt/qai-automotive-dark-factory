"""Optional CPU tests for the guarded patch projection; no CUDA/model downloads."""
import copy
from importlib.metadata import PackageNotFoundError, version
import unittest
from dark_factory.gates_patch_embed import UnsupportedPatchLayout, linear_qwen3_vl_patch_embed

try:
    import torch
except ImportError:
    torch = None
try:
    compatible_transformers = version("transformers") == "4.57.3"
except PackageNotFoundError:
    compatible_transformers = False


@unittest.skipUnless(torch is not None and compatible_transformers,
                     "Optional Torch and Transformers 4.57.3 are required")
class LinearPatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from transformers.models.qwen3_vl.configuration_qwen3_vl import Qwen3VLVisionConfig
        from transformers.models.qwen3_vl.modeling_qwen3_vl import Qwen3VLVisionPatchEmbed, Qwen3VLVisionModel
        cls.Config, cls.Patch, cls.Vision = Qwen3VLVisionConfig, Qwen3VLVisionPatchEmbed, Qwen3VLVisionModel
        cls.old_threads = torch.get_num_threads()
        torch.set_num_threads(1)

    @classmethod
    def tearDownClass(cls):
        torch.set_num_threads(cls.old_threads)

    def fixture(self):
        torch.manual_seed(8601)
        return self.Patch(self.Config(hidden_size=12)).double()

    def test_output_input_weight_and_bias_gradient_parity(self):
        for count in (1, 2, 7):
            with self.subTest(patches=count):
                original = self.fixture()
                candidate = copy.deepcopy(original)
                x = torch.randn(count, 1536, dtype=torch.float64, requires_grad=True)
                x2 = x.detach().clone().requires_grad_()
                expected = original(x)
                with linear_qwen3_vl_patch_embed(candidate, unsupported="raise") as patched:
                    self.assertEqual(patched, 1)
                    actual = candidate(x2)
                    weight = torch.randn_like(actual)
                    (actual * weight).sum().backward()
                (expected * weight).sum().backward()
                torch.testing.assert_close(actual, expected, rtol=1e-11, atol=1e-12)
                torch.testing.assert_close(x2.grad, x.grad, rtol=1e-11, atol=1e-12)
                for (name, parameter), (_, reference) in zip(candidate.named_parameters(), original.named_parameters()):
                    with self.subTest(parameter=name):
                        torch.testing.assert_close(parameter.grad, reference.grad, rtol=1e-11, atol=1e-12)

    def test_independent_patches_and_channel_temporal_spatial_order(self):
        module = self.fixture()
        # Nonseparable input and weights detect changes in flattening order.
        x = torch.arange(4 * 1536, dtype=torch.float64).reshape(4, 1536) / 997
        x.requires_grad_()
        expected = module(x)
        with linear_qwen3_vl_patch_embed(module, unsupported="raise"):
            actual = module(x)
            changed = module(torch.cat((x[:1], x[1:2].flip(1) + 3, x[2:])))
            actual[0].sum().backward()
        torch.testing.assert_close(actual, expected, rtol=1e-11, atol=1e-12)
        torch.testing.assert_close(actual[:1], changed[:1], rtol=0, atol=0)
        torch.testing.assert_close(actual[2:], changed[2:], rtol=0, atol=0)
        self.assertGreater((actual[1] - changed[1]).abs().max().item(), 0.1)
        self.assertEqual(torch.count_nonzero(x.grad[1:]).item(), 0)

    def test_surrounding_input_dtype_cast_is_preserved(self):
        module = self.fixture()
        x = torch.randn(2, 1536, dtype=torch.float32, requires_grad=True)
        expected = module(x)
        with linear_qwen3_vl_patch_embed(module, unsupported="raise"):
            actual = module(x)
            actual.sum().backward()
        self.assertEqual(actual.dtype, torch.float64)
        self.assertEqual(x.grad.dtype, torch.float32)
        torch.testing.assert_close(actual, expected, rtol=1e-11, atol=1e-12)

    def test_nonstandard_input_layout_falls_back_or_raises(self):
        module = self.fixture()
        x = torch.randn(2, 3, 4, 16, 16, dtype=torch.float64)
        expected = module.proj(x)
        with linear_qwen3_vl_patch_embed(module):
            torch.testing.assert_close(module.proj(x), expected, rtol=0, atol=0)
        with linear_qwen3_vl_patch_embed(module, unsupported="raise"):
            with self.assertRaises(UnsupportedPatchLayout):
                module.proj(x)

    def test_unsupported_properties_never_use_linear_path(self):
        for field, value in (("stride", (1,16,16)), ("padding", (1,0,0)),
                             ("dilation", (2,1,1)), ("padding_mode", "reflect")):
            with self.subTest(field=field):
                module = self.fixture()
                setattr(module.proj, field, value)
                with linear_qwen3_vl_patch_embed(module) as count:
                    self.assertEqual(count, 0)
                with self.assertRaises(UnsupportedPatchLayout):
                    with linear_qwen3_vl_patch_embed(module, unsupported="raise"):
                        pass
        module = self.fixture()
        with linear_qwen3_vl_patch_embed(module, unsupported="raise"):
            module.proj.stride = (1,16,16)
            with self.assertRaises(UnsupportedPatchLayout):
                module(torch.randn(2,1536,dtype=torch.float64))

    def test_weights_keys_hooks_and_exception_restoration(self):
        module = self.fixture()
        module.requires_grad_(False)
        before = {k:v.clone() for k,v in module.state_dict().items()}
        parameters = list(module.parameters())
        original_forward = module.proj.forward
        calls = []
        hook = module.proj.register_forward_hook(lambda m, args, result: calls.append(result.shape))
        try:
            with self.assertRaisesRegex(RuntimeError, "intentional"):
                with linear_qwen3_vl_patch_embed(module, unsupported="raise"):
                    module(torch.zeros(2,1536,dtype=torch.float64))
                    with self.assertRaisesRegex(ValueError, "already wrapped"):
                        with linear_qwen3_vl_patch_embed(module):
                            pass
                    raise RuntimeError("intentional")
        finally:
            hook.remove()
        self.assertEqual(calls, [torch.Size([2,12,1,1,1])])
        self.assertEqual(module.proj.forward, original_forward)
        self.assertEqual(list(before), list(module.state_dict()))
        for parameter, same_parameter in zip(parameters, module.parameters()):
            self.assertIs(parameter, same_parameter)
            self.assertFalse(parameter.requires_grad)
        for key, value in module.state_dict().items():
            torch.testing.assert_close(value, before[key], rtol=0, atol=0)

    def test_bfloat16_cpu_autocast(self):
        original = self.fixture().float()
        candidate = copy.deepcopy(original)
        x = torch.randn(2,1536,requires_grad=True)
        x2 = x.detach().clone().requires_grad_()
        # This host oneDNN lacks BF16 Conv3d backward; use its native CPU reference.
        with torch.backends.mkldnn.flags(enabled=False), torch.autocast(device_type="cpu", dtype=torch.bfloat16):
            expected = original(x)
            with linear_qwen3_vl_patch_embed(candidate, unsupported="raise"):
                actual = candidate(x2)
                actual.float().square().sum().backward()
            expected.float().square().sum().backward()
        self.assertEqual(actual.dtype, torch.bfloat16)
        torch.testing.assert_close(actual, expected, rtol=0.02, atol=0.01)
        torch.testing.assert_close(x2.grad, x.grad, rtol=0.02, atol=0.001)
        for parameter, reference in zip(candidate.parameters(), original.parameters()):
            torch.testing.assert_close(parameter.grad, reference.grad, rtol=0.02, atol=0.02)

    def test_downstream_peft_lora_gradients_and_dropout_rng(self):
        try:
            from peft import LoraConfig, get_peft_model
        except ImportError:
            self.skipTest("Optional PEFT dependency is not installed")
        torch.manual_seed(193)
        config = self.Config(depth=1, hidden_size=32, intermediate_size=64, num_heads=4,
                             out_hidden_size=32, deepstack_visual_indexes=[0])
        config._attn_implementation = "sdpa"
        original = get_peft_model(self.Vision(config).float(),
            LoraConfig(r=2, lora_alpha=4, lora_dropout=0.25, bias="none", target_modules=["qkv"]))
        candidate = copy.deepcopy(original)
        x = torch.randn(16,1536)
        grid = torch.tensor([[1,4,4]])
        torch.manual_seed(211)
        expected, expected_deep = original(x,grid)
        (expected.square().sum() + sum(v.square().sum() for v in expected_deep)).backward()
        rng = torch.get_rng_state()
        torch.manual_seed(211)
        with linear_qwen3_vl_patch_embed(candidate, unsupported="raise"):
            actual, actual_deep = candidate(x,grid)
            (actual.square().sum() + sum(v.square().sum() for v in actual_deep)).backward()
        torch.testing.assert_close(actual, expected, rtol=1e-4,atol=1e-5)
        self.assertTrue(torch.equal(rng,torch.get_rng_state()))
        nonzero_trainable = 0
        for (name,parameter),(_,reference) in zip(candidate.named_parameters(),original.named_parameters()):
            with self.subTest(parameter=name):
                if parameter.requires_grad:
                    torch.testing.assert_close(parameter.grad,reference.grad,rtol=2e-4,atol=1e-5)
                    nonzero_trainable += bool(torch.count_nonzero(parameter.grad))
                else:
                    self.assertIsNone(parameter.grad)
        self.assertGreater(nonzero_trainable,0)

    def test_full_small_vision_tower_with_checkpoint_backward(self):
        torch.manual_seed(660)
        config = self.Config(depth=2, hidden_size=32, intermediate_size=64, num_heads=4,
                             out_hidden_size=32, deepstack_visual_indexes=[0,1])
        config._attn_implementation = "sdpa"
        original = self.Vision(config).float()
        original.gradient_checkpointing_enable(gradient_checkpointing_kwargs={"use_reentrant":False})
        candidate = copy.deepcopy(original)
        x = torch.randn(32,1536,requires_grad=True)
        x2 = x.detach().clone().requires_grad_()
        grid = torch.tensor([[1,4,4],[1,4,4]])
        expected, expected_deep = original(x,grid)
        with linear_qwen3_vl_patch_embed(candidate, unsupported="raise"):
            actual, actual_deep = candidate(x2,grid)
            (actual.square().sum() + sum(v.square().sum() for v in actual_deep)).backward()
        (expected.square().sum() + sum(v.square().sum() for v in expected_deep)).backward()
        for output, reference in zip([actual]+actual_deep,[expected]+expected_deep):
            torch.testing.assert_close(output,reference,rtol=1e-4,atol=1e-5)
        torch.testing.assert_close(x2.grad,x.grad,rtol=1e-4,atol=1e-5)
        for (name,parameter),(_,reference) in zip(candidate.named_parameters(),original.named_parameters()):
            with self.subTest(parameter=name):
                torch.testing.assert_close(parameter.grad,reference.grad,rtol=2e-4,atol=1e-4)


if __name__ == "__main__":
    unittest.main()

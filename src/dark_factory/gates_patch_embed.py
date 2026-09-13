"""Opt-in linear implementation of the inspected Qwen3-VL patch convolution.

Use ``with linear_qwen3_vl_patch_embed(model):`` around forward and backward.
No behavior changes on import. This is pinned to Transformers 4.57.3 and the
stock RGB (2,16,16) patch Conv3d; unsupported projections/layouts use the original
path unless ``unsupported="raise"``. Models must not be used concurrently or
patched by another caller while inside the context.

The processor already supplies independent flattened patches. Each Conv3d input
has exactly one kernel volume, so linear(input.flatten(1), weight.flatten(1),
bias) computes the same algebraic map. Floating-point kernels may still differ;
full-model GPU parity must be established before enabling this experimentally.

Only the existing Conv3d instance's forward is temporarily replaced. Its module
hooks, Parameter objects, five-dimensional weights, state-dict keys and gradient
flags remain intact. The surrounding Qwen patch module retains its input cast
and reshape; autocast and all later LoRA modules retain their existing behavior.
Torch/Transformers are optional and loaded only on explicit use.
"""
from contextlib import contextmanager
from importlib.metadata import version
from types import MethodType


class UnsupportedPatchLayout(ValueError):
    """A patch projection falls outside the inspected linear equivalence."""


def _supported_projection(projection, conv_type):
    return (type(projection) is conv_type and projection.in_channels == 3
            and projection.kernel_size == (2, 16, 16)
            and projection.stride == (2, 16, 16)
            and projection.padding == (0, 0, 0)
            and projection.dilation == (1, 1, 1)
            and projection.groups == 1 and projection.padding_mode == "zeros"
            and tuple(projection.weight.shape) == (projection.out_channels, 3, 2, 16, 16)
            and projection.bias is not None
            and tuple(projection.bias.shape) == (projection.out_channels,))


@contextmanager
def linear_qwen3_vl_patch_embed(model, *, unsupported="fallback"):
    """Yield patched projection count; restore original forwards even on error.

    Keep the context open through checkpoint recomputation/backward. Existing
    forward overrides are rejected; unsupported wrapped/quantized projections
    remain untouched in fallback mode. No weights are copied or reformatted.
    """
    if unsupported not in ("fallback", "raise"):
        raise ValueError("unsupported must be 'fallback' or 'raise'")
    if version("transformers") != "4.57.3":
        raise RuntimeError("Patch projection experiment requires Transformers 4.57.3")
    import torch
    from torch.nn import functional as F
    from transformers.models.qwen3_vl.modeling_qwen3_vl import Qwen3VLVisionPatchEmbed

    patches = [m for m in model.modules() if type(m) is Qwen3VLVisionPatchEmbed]
    if not patches:
        raise ValueError("Model contains no stock Qwen3-VL patch embedding")
    projections = []
    for patch in patches:
        projection = patch.proj
        if not _supported_projection(projection, torch.nn.Conv3d):
            if unsupported == "raise":
                raise UnsupportedPatchLayout("Expected stock RGB nonoverlapping (2,16,16) Conv3d with bias")
            continue
        if ("forward" in projection.__dict__
                or getattr(projection.forward, "__func__", None) is not torch.nn.Conv3d.forward):
            raise ValueError("Patch convolution forward is already wrapped or patched")
        projections.append(projection)

    def make_forward(original):
        def forward(projection, input):
            if (not _supported_projection(projection, torch.nn.Conv3d)
                    or input.ndim != 5 or input.shape[0] == 0
                    or tuple(input.shape[1:]) != (3, 2, 16, 16)):
                if unsupported == "raise":
                    raise UnsupportedPatchLayout("Each convolution input must contain exactly one RGB patch volume")
                return original(input)
            output = F.linear(input.flatten(1), projection.weight.flatten(1), projection.bias)
            return output.view(input.shape[0], projection.out_channels, 1, 1, 1)
        return forward

    installed = []
    try:
        for projection in projections:
            projection.forward = MethodType(make_forward(projection.forward), projection)
            installed.append(projection)
        yield len(installed)
    finally:
        for projection in installed:
            del projection.forward

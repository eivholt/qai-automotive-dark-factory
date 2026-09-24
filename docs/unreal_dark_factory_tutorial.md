# Physics-aware agentic Edge AI for robotics: Simulated dark factory

**Author:** [Eivind Holt](https://www.linkedin.com/in/eivholt/), September 2026  

**Repository:** [github.com/eivholt/qai-physics-reasoning](https://github.com/eivholt/qai-automotive-dark-factory)  

**Target:** [Qualcomm Dragonwing IQ-9075 EVK / QCS9075 / Hexagon v73](https://www.qualcomm.com/developer/hardware/qualcomm-iq-9075-evaluation-kit-evk). Hardware generously sponsored by Qualcomm

**VLM Model:** [nvidia/Cosmos-Reason2-2B](https://huggingface.co/nvidia/Cosmos-Reason2-2B) Q4_0 GGUF running on device NPU

This tutorial simulates a fully automated factory with robotic arms controlled by an LLM-backed AI agent. The agent uses physics-aware VLM tools to determine current state and next steps. It all runs on a single Qualcomm Dragonwing IQ-9075 EVK, thanks to active concurrent model inference and some neat speed and accuracy tricks.

## Dark factory
[Dark factories, or lights-out manufacturing](https://en.wikipedia.org/wiki/Lights_out_(manufacturing)) denotes fully automated manufacturing, where lights and air-condition are redundant as a human workforce is not present. Think of the ending scenes of The Terminator (1984) where Sarah Connor fights for her life in an unattended, 24/7 assembly line. While literal dark factories do exist, this is more of a metaphor than about saving electricity bill.

Often the production process needs to keep humans in the loop due to visual quality assurance and other tasks that are hard to automate with traditional sensor technology. Keeping workers around is also often needed to handle unpredicted events, such as parts falling off, perhaps having to reset a rigid mechanical process. Advances in physics-aware visual language models (VLM) move visual computing beyond bounded object recognition, to being able to reason and answer questions about an image or short video clip. LLM-backed agentic AI allows for more dynamic process execution where policy and desired outcomes steer behavior, rather than rigid control flow instructions. This demo shows how all of this can be combined in a virtual factory, useful for smoking out problems before deployment in a real facility.

## Demo setup
A virtual vehicle assembly station was constructed by OpenAI Codex with GPT-6 Astra in Unreal Engine based on model assets purchased from [TurboSquid](https://www.turbosquid.com/FullPreview/1370870). The assets of this demo is not included in the project repo due to licensing restrictions. As an alternative, coding assistants are becoming capable of [creating 3D assets using e.g. Blender](https://x.com/tomkrcha/status/2095756085890310311). Prepare for asset store inflation.

The simulation contains different virtual tools on robotic arms: inspection camera, sealant dispenser, vacuum placement tool and a spot welder.

A physical IQ9 EVK is connected to the computer running the simulation, serving a LLM, a VLM and an agentic harness. The agent controls the virtual arms, requesting inspection camera images, asking VLM to interpret the images, optionally waiting and recapturing and finally initializing the next assembly step.

This tutorial mostly keeps a high-level view of the different parts of the project, with some novel exceptions. Find details on subjects such as model deployment, fine-tuning and agentic harnesses in the tutorial section of the [Dragonwing developer documentation portal](https://dragonwingdocs.qualcomm.com/tutorial).

## Physics-aware VLM
As detailed in previous tutorials, this demo uses NVIDIA Cosmos Reason2-2B for answering questions about visual inspection. The agent has tools that can answer whether panels are in place, if sealant has been sufficiently distributed, if the sealant has cooled after application and if spot welds have cooled and panels have not drifted (unlike Tesla).

### VLM speed and accuracy optimizations

## Agent

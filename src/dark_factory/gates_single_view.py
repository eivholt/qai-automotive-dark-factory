"""Offline single-view experiment; existing three-view adapters are unvalidated here.

Never import this experiment into the live router until its changed image/prompt
contract has compatible adapter provenance and has passed rendered integration.
"""
import hashlib

VERSION = "automotive-panel-single-view-experiment-v1"
CAMERAS = ("spot1", "spot2", "spot3")
SUFFIX = " Ignore unrelated body panels, robot tools and background. Output exactly one letter G, A or R. Do not explain or think."
PROMPTS = {
    "panel_presence": "Inspect this single NIR camera view of the inner trunk panel installation area. Determine whether the inner trunk panel is installed. G = the panel is visibly present in its intended installation area. R = the installation area is clearly visible and the panel is missing. A = the area is hidden, a panel is only partly present, or the evidence is uncertain.",
    "panel_seated": "Inspect this single NIR camera view of the inner trunk panel after installation and welding. Check the panel and its contacts visible in this image. G = the panel is present, correctly positioned and flush at the visible contacts, with no lifted edge or displaced corner. R = the panel is missing, visibly displaced, tilted, or has a lifted edge or corner. A = a contact needed to judge this view is hidden or the seating evidence is uncertain. Other camera views are inspected separately.",
    "adhesive": "Inspect this single monochrome thermal camera view of the inner trunk panel-to-body contact. Hot sealant underneath warms the opaque metal panel surface. Inspect the heated metal contact band visible in this image, not the hidden sealant. The contact follows half of the front edge and one side; the rear edge is free. Other contact views are inspected separately. Sensor grayscale is linear: black is 20 C, white is 120 C. G = the contact band is visible and its hottest panel area is below 42 C (grayscale 56 of 255). R = any area of this contact band is above 48 C (grayscale 71); wait for cooling. A = the hottest contact area is between 42 C and 48 C inclusive, the contact is hidden, or the evidence is uncertain. Inspect the band even when cool contact areas have little contrast.",
    "weld": "Inspect this single monochrome thermal camera closeup of the inner trunk panel-to-body weld contact regions visible in this image. Other camera views are inspected separately. Sensor grayscale is linear: black is 20 C, white is 120 C. G = the weld contact regions in this view are visible and their hottest area is below 52 C (grayscale 82 of 255). R = any weld contact area in this view is above 58 C (grayscale 97); wait for cooling. A = the hottest weld contact area is between 52 C and 58 C inclusive, a required region in this view is hidden, or the evidence is uncertain. Inspect the contact regions even when cool welds have little contrast.",
}


def prompt_for(stage):
    return PROMPTS[stage] + SUFFIX


def prompt_hashes():
    return {stage: hashlib.sha256(prompt_for(stage).encode()).hexdigest() for stage in PROMPTS}


def aggregate(stage, views):
    """Require complete evidence, unanimous presence and no per-view green veto.

    These are experimental batch labels, not live process permissions. Never copy
    a whole-batch ground-truth label onto an individual camera image.
    """
    if stage not in PROMPTS:
        raise ValueError("Unknown single-view stage")
    if len(views) != 3 or [v.get("camera_id") for v in views] != list(CAMERAS):
        return "A", "incomplete_or_misordered_views"
    if any(v.get("error") or v.get("decision") not in ("G", "A", "R") for v in views):
        return "A", "view_transport_or_scoring_failure"
    labels = [v["decision"] for v in views]
    if all(label == "G" for label in labels):
        return "G", "all_views_green"
    if stage == "panel_presence":
        return ("R", "all_views_missing") if all(label == "R" for label in labels) else ("A", "presence_disagreement_or_uncertainty")
    if "R" in labels:
        return "R", "at_least_one_view_red"
    return "A", "view_uncertainty"

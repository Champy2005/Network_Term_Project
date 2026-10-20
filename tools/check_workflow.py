"""Validate the repository's deliberately small skill/config/reference contract."""

import json
from pathlib import Path
import re
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
NAME = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*\Z")
LINK = re.compile(r"(?<!!)\[[^\]\n]+\]\(([^)\n]+)\)")


def skill_metadata(path: Path) -> dict[str, str]:
    """Read this repo's flat YAML subset: plain names and JSON-quoted descriptions."""
    text = path.read_text(encoding="utf-8")
    if not text.startswith("---\n") or "\n---\n" not in text[4:]:
        raise ValueError("missing YAML frontmatter")
    metadata = {}
    for line in text.split("\n---\n", 1)[0][4:].splitlines():
        key, separator, value = line.partition(":")
        if not separator or key not in {"name", "description"} or key in metadata:
            raise ValueError("expected unique name and description fields")
        value = value.strip()
        metadata[key] = json.loads(value) if value.startswith('"') else value
    if set(metadata) != {"name", "description"}:
        raise ValueError("name and description are required")
    name, description = metadata["name"], metadata["description"]
    if not isinstance(name, str) or len(name) > 64 or not NAME.fullmatch(name):
        raise ValueError("invalid skill name")
    if not isinstance(description, str) or not description.strip() or len(description) > 1024:
        raise ValueError("description must be a nonempty string of at most 1024 characters")
    if name != path.parent.name:
        raise ValueError("skill name must match its directory")
    return metadata


def validate(root: Path) -> list[str]:
    """Return actionable artifact errors; this does not claim native runtime discovery."""
    root = root.resolve()
    errors = []
    skills = sorted((root / ".agents/skills").glob("*/SKILL.md"))
    if not skills:
        errors.append("No repository skills found")
    names = set()
    for path in skills:
        try:
            metadata = skill_metadata(path)
            if metadata["name"] in names:
                raise ValueError("duplicate skill name")
            names.add(metadata["name"])
        except (OSError, ValueError) as error:
            errors.append(f"{path.relative_to(root)}: {error}")
    # Scan supporting playbooks as well as entrypoints: dead routing is a real failure.
    for path in sorted((root / ".agents/skills").rglob("*.md")):
        for match in LINK.finditer(path.read_text(encoding="utf-8")):
            link = match.group(1).strip().strip("<>")
            if re.match(r"[a-zA-Z][a-zA-Z0-9+.-]*:", link) or link.startswith("#"):
                continue
            target = (path.parent / link.split("#", 1)[0]).resolve()
            if not target.is_relative_to(root) or not target.is_file():
                errors.append(f"{path.relative_to(root)}: missing/outside reference {link}")
    try:
        config = tomllib.loads((root / ".codex/config.toml").read_text(encoding="utf-8"))
        # This local policy deliberately cannot override the primary model or permissions.
        if set(config) != {"agents"}:
            raise ValueError("project config may only set agents defaults")
        agents = config["agents"]
        supported = {"enabled", "max_concurrent_threads_per_session",
                     "default_subagent_model", "default_subagent_reasoning_effort"}
        if set(agents) != supported or agents["enabled"] is not True:
            raise ValueError("unsupported/missing agent defaults or delegation disabled")
        cap = agents["max_concurrent_threads_per_session"]
        if type(cap) is not int or not 1 <= cap <= 3:
            raise ValueError("child concurrency must be an integer in 1..3")
        if (agents["default_subagent_model"] != "gpt-6-luna"
                or agents["default_subagent_reasoning_effort"] != "high"):
            raise ValueError("default workers must use Luna/high")
    except (OSError, ValueError, TypeError) as error:
        errors.append(f".codex/config.toml: {error}")
    profiles = sorted((root / ".codex/agents").glob("*.toml"))
    if not profiles:
        errors.append("No custom agent profiles found")
    agent_names = set()
    for path in profiles:
        try:
            profile = tomllib.loads(path.read_text(encoding="utf-8"))
            required = {"name", "description", "model", "model_reasoning_effort",
                        "sandbox_mode", "developer_instructions"}
            if set(profile) != required:
                raise ValueError("unsupported/missing custom agent fields")
            for key in required:
                if not isinstance(profile[key], str) or not profile[key].strip():
                    raise ValueError(f"{key} must be a nonempty string")
            if profile["name"] in agent_names or profile["name"] != path.stem:
                raise ValueError("agent name must be unique and match its filename")
            agent_names.add(profile["name"])
            if profile["model"] != "gpt-6-luna" or profile["model_reasoning_effort"] not in {"high", "xhigh"}:
                raise ValueError("custom workers must use Luna/high or Luna/xhigh")
            if profile["sandbox_mode"] not in {"read-only", "workspace-write"}:
                raise ValueError("unexpected agent sandbox default")
        except (OSError, ValueError, TypeError) as error:
            errors.append(f"{path.relative_to(root)}: {error}")
    return errors


def main() -> int:
    """Fail closed on invalid local artifacts; native loading is checked separately."""
    errors = validate(ROOT)
    for error in errors:
        print(f"FAIL {error}")
    if not errors:
        count = len(list((ROOT / ".agents/skills").glob("*/SKILL.md")))
        print(f"PASS {count} skills, custom agent TOML, local references, and project configuration")
    return int(bool(errors))


if __name__ == "__main__":
    sys.exit(main())

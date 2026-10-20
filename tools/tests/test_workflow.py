"""Reject unusable skill manifests, dead references, and unsafe local config changes."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_workflow


class ArtifactTests(unittest.TestCase):
    """Use an isolated minimal project to exercise the validator's public result."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.skill = self.root / ".agents/skills/example/SKILL.md"
        self.skill.parent.mkdir(parents=True)
        self.skill.write_text('---\nname: example\ndescription: "A useful task"\n---\n\nDo the task.\n', encoding="utf-8")
        self.config = self.root / ".codex/config.toml"
        self.config.parent.mkdir()
        self.config.write_text('[agents]\nenabled = true\nmax_concurrent_threads_per_session = 3\n'
                               'default_subagent_model = "gpt-6-luna"\n'
                               'default_subagent_reasoning_effort = "high"\n', encoding="utf-8")
        self.agent = self.root / ".codex/agents/mapper.toml"
        self.agent.parent.mkdir()
        self.agent.write_text('name = "mapper"\ndescription = "Read the source"\n'
                              'model = "gpt-6-luna"\nmodel_reasoning_effort = "high"\n'
                              'sandbox_mode = "read-only"\ndeveloper_instructions = "Stay read-only."\n', encoding="utf-8")

    def test_valid_minimal_project(self):
        self.assertEqual(check_workflow.validate(self.root), [])

    def test_broken_playbook_link_is_rejected(self):
        reference = self.skill.parent / "playbooks/task.md"
        reference.parent.mkdir()
        reference.write_text("[Proof](missing.md)\n", encoding="utf-8")
        self.assertTrue(any("missing.md" in e for e in check_workflow.validate(self.root)))

    def test_reference_outside_repository_is_rejected(self):
        with self.skill.open("a", encoding="utf-8") as output:
            output.write("[Outside](../../../../outside.md)\n")
        self.assertTrue(any("outside reference" in e for e in check_workflow.validate(self.root)))

    def test_empty_description_and_missing_manifest_fail(self):
        self.skill.write_text('---\nname: example\ndescription: ""\n---\n', encoding="utf-8")
        self.assertTrue(check_workflow.validate(self.root))
        self.skill.unlink()
        self.assertIn("No repository skills found", check_workflow.validate(self.root))

    def test_manifest_name_must_match_directory(self):
        self.skill.write_text('---\nname: another\ndescription: "Task"\n---\n', encoding="utf-8")
        self.assertTrue(any("match its directory" in e for e in check_workflow.validate(self.root)))

    def test_malformed_toml_and_unknown_schema_field_fail(self):
        self.config.write_text("[agents\n", encoding="utf-8")
        self.assertTrue(check_workflow.validate(self.root))
        self.config.write_text('[agents]\nenabled = true\nmax_threads = 100\n', encoding="utf-8")
        self.assertTrue(check_workflow.validate(self.root))

    def test_primary_model_override_is_rejected(self):
        self.config.write_text('model = "other"\n' + self.config.read_text(encoding="utf-8"), encoding="utf-8")
        self.assertTrue(any("only set agents" in e for e in check_workflow.validate(self.root)))

    def test_higher_cost_worker_and_unbounded_cap_are_rejected(self):
        text = self.config.read_text(encoding="utf-8")
        self.config.write_text(text.replace('"gpt-6-luna"', '"other-model"'), encoding="utf-8")
        self.assertTrue(check_workflow.validate(self.root))
        self.config.write_text(text.replace("= 3", "= 30"), encoding="utf-8")
        self.assertTrue(check_workflow.validate(self.root))

    def test_missing_agent_instructions_and_unrestricted_sandbox_fail(self):
        text = self.agent.read_text(encoding="utf-8")
        self.agent.write_text(text.replace('sandbox_mode = "read-only"', 'sandbox_mode = "danger-full-access"'), encoding="utf-8")
        self.assertTrue(check_workflow.validate(self.root))
        self.agent.write_text(text.replace('developer_instructions = "Stay read-only."', ''), encoding="utf-8")
        self.assertTrue(check_workflow.validate(self.root))


if __name__ == "__main__":
    unittest.main()

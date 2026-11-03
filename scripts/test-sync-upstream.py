#!/usr/bin/env python3
"""Exercise the upstream merge using temporary local repositories."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("sync-upstream.sh").resolve()


class SyncUpstreamTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.env = os.environ.copy()
        self.env.update({
            "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_CONFIG_GLOBAL": str(self.root / "gitconfig"),
            "GIT_AUTHOR_NAME": "Sync Test",
            "GIT_AUTHOR_EMAIL": "sync@example.invalid",
            "GIT_COMMITTER_NAME": "Sync Test",
            "GIT_COMMITTER_EMAIL": "sync@example.invalid",
        })
        self.upstream = self.root / "upstream.git"
        self.fork = self.root / "fork.git"
        self.seed = self.root / "seed"
        self.checkout = self.root / "checkout"
        self.git(self.root, "init", "--bare", "--initial-branch=main", str(self.upstream))
        self.git(self.root, "init", "--bare", "--initial-branch=main", str(self.fork))
        self.git(self.root, "init", "--initial-branch=main", str(self.seed))
        self.commit(self.seed, "shared.txt", "base\n", "base")
        self.git(self.seed, "remote", "add", "upstream", str(self.upstream))
        self.git(self.seed, "remote", "add", "origin", str(self.fork))
        self.git(self.seed, "push", "upstream", "main")
        self.git(self.seed, "push", "origin", "main")
        self.git(self.root, "clone", str(self.fork), str(self.checkout))
        self.git(self.checkout, "remote", "add", "upstream", str(self.upstream))

    def git(self, cwd, *args, check=True):
        return subprocess.run(["git", *args], cwd=cwd, env=self.env, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=check)

    def commit(self, repo, name, content, message):
        (repo / name).write_text(content, encoding="utf-8")
        self.git(repo, "add", name)
        self.git(repo, "commit", "-m", message)
        return self.git(repo, "rev-parse", "HEAD").stdout.strip()

    def sync(self, *args, env=None):
        return subprocess.run([str(SCRIPT), *args], cwd=self.checkout, env=env or self.env, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)

    def head(self, repo, ref="HEAD"):
        return self.git(repo, "rev-parse", ref).stdout.strip()

    def update_upstream(self, name="upstream.txt", content="upstream\n"):
        head = self.commit(self.seed, name, content, "upstream change")
        self.git(self.seed, "push", "upstream", "main")
        return head

    def test_up_to_date_does_not_create_a_commit(self):
        before = self.head(self.checkout)
        result = self.sync("--push")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.head(self.checkout), before)
        self.assertEqual(self.head(self.fork, "main"), before)

    def test_fast_forward_and_push(self):
        upstream = self.update_upstream()
        result = self.sync("--push")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.head(self.checkout), upstream)
        self.assertEqual(self.head(self.fork, "main"), upstream)

    def test_divergent_changes_are_merged_and_pushed(self):
        local = self.commit(self.checkout, "kde.txt", "port\n", "KDE change")
        self.git(self.checkout, "push", "origin", "main")
        upstream = self.update_upstream()
        result = self.sync("--push")
        self.assertEqual(result.returncode, 0, result.stderr)
        merged = self.head(self.checkout)
        self.assertEqual(self.head(self.fork, "main"), merged)
        self.assertEqual(self.head(self.checkout, "HEAD^1"), local)
        self.assertEqual(self.head(self.checkout, "HEAD^2"), upstream)
        self.assertEqual((self.checkout / "kde.txt").read_text(), "port\n")
        self.assertEqual((self.checkout / "upstream.txt").read_text(), "upstream\n")

    def test_conflict_aborts_without_changing_fork_or_checkout(self):
        local = self.commit(self.checkout, "shared.txt", "fork\n", "fork change")
        self.git(self.checkout, "push", "origin", "main")
        self.update_upstream("shared.txt", "upstream\n")
        result = self.sync("--push")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("merge failed", result.stderr)
        self.assertEqual(self.head(self.checkout), local)
        self.assertEqual(self.head(self.fork, "main"), local)
        self.assertEqual(self.git(self.checkout, "status", "--porcelain").stdout, "")
        self.assertEqual((self.checkout / "shared.txt").read_text(), "fork\n")

    def test_dirty_checkout_is_never_changed(self):
        upstream = self.update_upstream()
        before = self.head(self.checkout)
        (self.checkout / "untracked.txt").write_text("keep\n", encoding="utf-8")
        result = self.sync("--push")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not clean", result.stderr)
        self.assertEqual(self.head(self.checkout), before)
        self.assertEqual(self.head(self.fork, "main"), before)
        self.assertNotEqual(before, upstream)
        self.assertEqual((self.checkout / "untracked.txt").read_text(), "keep\n")

    def test_outdated_checkout_is_never_changed(self):
        before = self.head(self.checkout)
        self.commit(self.seed, "fork.txt", "fork\n", "fork update")
        self.git(self.seed, "push", "origin", "main")
        result = self.sync("--push")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("differs", result.stderr)
        self.assertEqual(self.head(self.checkout), before)
        self.assertNotEqual(self.head(self.fork, "main"), before)

    def test_local_mode_does_not_push(self):
        before = self.head(self.fork, "main")
        upstream = self.update_upstream()
        result = self.sync()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.head(self.checkout), upstream)
        self.assertEqual(self.head(self.fork, "main"), before)

    def test_concurrent_remote_update_is_not_overwritten(self):
        upstream = self.update_upstream()
        concurrent = self.commit(self.seed, "other.txt", "concurrent\n", "concurrent update")
        wrapper_dir = self.root / "wrappers"
        wrapper_dir.mkdir()
        wrapper = wrapper_dir / "git"
        wrapper.write_text(
            '#!/bin/sh\n'
            'if [ "$1" = push ]; then\n'
            '  "$SYNC_REAL_GIT" -C "$SYNC_RACE_REPO" push origin main >/dev/null 2>&1 || exit 1\n'
            'fi\n'
            'exec "$SYNC_REAL_GIT" "$@"\n', encoding="utf-8")
        wrapper.chmod(0o755)
        env = self.env.copy()
        env.update({"PATH": f"{wrapper_dir}{os.pathsep}{env['PATH']}",
                    "SYNC_REAL_GIT": shutil.which("git"), "SYNC_RACE_REPO": str(self.seed)})
        result = self.sync("--push", env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.head(self.fork, "main"), concurrent)
        self.assertEqual(self.head(self.checkout), upstream)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env bash
set -euo pipefail

# Run from a clean checkout of the fork. Without --push, the result stays
# local so that it can be inspected before publishing.
push_result=false
case "${1-}" in
    "") ;;
    --push) push_result=true ;;
    *) printf 'Usage: %s [--push]\n' "$0" >&2; exit 2 ;;
esac
if (( $# > 1 )); then
    printf 'Usage: %s [--push]\n' "$0" >&2
    exit 2
fi

fork_remote=${SNOW_SYNC_FORK_REMOTE:-origin}
fork_branch=${SNOW_SYNC_FORK_BRANCH:-main}
upstream_remote=${SNOW_SYNC_UPSTREAM_REMOTE:-upstream}
upstream_branch=${SNOW_SYNC_UPSTREAM_BRANCH:-main}

git check-ref-format --branch "$fork_branch" >/dev/null
git check-ref-format --branch "$upstream_branch" >/dev/null
if [[ $(git branch --show-current) != "$fork_branch" ]]; then
    printf 'Check out %s before syncing.\n' "$fork_branch" >&2
    exit 1
fi
if [[ -n $(git status --porcelain --untracked-files=normal) ]] ||
   git rev-parse --verify --quiet MERGE_HEAD >/dev/null ||
   git rev-parse --verify --quiet CHERRY_PICK_HEAD >/dev/null ||
   git rev-parse --verify --quiet REVERT_HEAD >/dev/null ||
   [[ -d $(git rev-parse --git-path rebase-merge) ]] ||
   [[ -d $(git rev-parse --git-path rebase-apply) ]]; then
    printf 'The working tree or index is not clean; sync stopped.\n' >&2
    exit 1
fi

# Do not merge against an out-of-date checkout or push unrelated local commits.
git fetch --no-tags "$fork_remote" "refs/heads/$fork_branch"
fork_head=$(git rev-parse 'FETCH_HEAD^{commit}')
if [[ $(git rev-parse HEAD) != "$fork_head" ]]; then
    printf 'Local %s differs from %s/%s; sync stopped.\n' \
        "$fork_branch" "$fork_remote" "$fork_branch" >&2
    exit 1
fi
git fetch --no-tags "$upstream_remote" "refs/heads/$upstream_branch"
upstream_head=$(git rev-parse 'FETCH_HEAD^{commit}')

if git merge-base --is-ancestor "$upstream_head" HEAD; then
    printf 'Upstream is already included.\n'
    exit 0
fi

if git merge-base --is-ancestor HEAD "$upstream_head"; then
    git merge --ff-only "$upstream_head"
elif ! git merge --no-ff --no-edit "$upstream_head"; then
    printf 'Upstream merge failed; aborting it for manual resolution.\n' >&2
    if git rev-parse --verify --quiet MERGE_HEAD >/dev/null; then
        git merge --abort
    fi
    exit 1
fi

if "$push_result"; then
    # A normal push rejects a concurrent divergent update; never force-push.
    git push "$fork_remote" "HEAD:refs/heads/$fork_branch"
else
    printf 'Upstream merged locally; review and push the result when ready.\n'
fi

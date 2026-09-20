#!/usr/bin/env bash
# Update the current branch of github.com/ArtemShamaev/Barnix.
set -euo pipefail

usage() {
    cat <<'HELP'
Usage: scripts/update-repo.sh ["commit message"]

Fetch origin, check history, stage all non-ignored changes, commit, then push
only the current branch to github.com/ArtemShamaev/Barnix.
Default commit message: Update Barnix.

Requires GitHub authentication through Git's credential helper or SSH agent.
No force-push, automatic stash, history rewrite, or conflict resolution.
HELP
}
fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
esac
[[ $# -le 1 ]] || { usage; exit 2; }
message=${1:-Update Barnix}
[[ -n "$message" ]] || fail 'Commit message cannot be empty.'

# Work relative to this script, even when invoked from another directory.
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd -- "$script_dir/.."
root=$(git rev-parse --show-toplevel) || fail 'Not a Git checkout.'
[[ "$PWD" == "$root" ]] || fail 'The script must be in the repository scripts/ directory.'

check_url() {
    case "$1" in
        https://github.com/ArtemShamaev/Barnix|https://github.com/ArtemShamaev/Barnix.git|\
        git@github.com:ArtemShamaev/Barnix|git@github.com:ArtemShamaev/Barnix.git|\
        ssh://git@github.com/ArtemShamaev/Barnix|ssh://git@github.com/ArtemShamaev/Barnix.git) ;;
        *) fail 'origin must point only to github.com/ArtemShamaev/Barnix.' ;;
    esac
}
# --all also rejects multiple push URLs rather than publishing elsewhere.
check_url "$(git remote get-url --all origin)"
check_url "$(git remote get-url --push --all origin)"
branch=$(git symbolic-ref --quiet --short HEAD) || fail 'Detached HEAD; switch to a branch first.'
git check-ref-format "refs/heads/$branch" >/dev/null
for operation in MERGE_HEAD CHERRY_PICK_HEAD REVERT_HEAD rebase-merge rebase-apply; do
    [[ ! -e "$(git rev-parse --git-path "$operation")" ]] || fail 'Finish or abort the current Git operation first.'
done
[[ -z "$(git ls-files --unmerged)" ]] || fail 'Resolve merge conflicts first.'

git fetch origin
remote_ref="refs/remotes/origin/$branch"
if git show-ref --verify --quiet "$remote_ref"; then
    git merge-base --is-ancestor "$remote_ref" HEAD ||
        fail 'Remote branch is ahead or diverged. Commit/stash your changes, then integrate it before pushing.'
fi
git add -A -- .
if ! git diff --cached --quiet; then
    git commit -m "$message"
fi
# Explicit refspec overrides configured push.default and matching refspecs.
git push --set-upstream origin "HEAD:refs/heads/$branch"

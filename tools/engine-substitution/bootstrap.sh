#!/usr/bin/env bash
# Provision isolated pinned Tkrzw/model20 libraries with upstream-owned tooling.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
source "$ROOT/tools/engine-substitution/pins.env"
WORK=${1:?Usage: bootstrap.sh absolute-work-directory}
mkdir -p "$WORK"
WORK=$(realpath "$WORK")
if [[ ! -d $WORK/source/.git ]]; then
    git clone https://github.com/shenghaoc/oxpinyin.git "$WORK/source"
fi
git -C "$WORK/source" checkout --detach "$OXPINYIN_COMMIT"
test "$(git -C "$WORK/source" rev-parse HEAD)" = "$OXPINYIN_COMMIT"
cp "$WORK/source/tools/oracle/oracle-pin.txt" "$WORK/reference-pin.txt"
"$WORK/source/tools/model/fetch-model.sh" --cache-dir "$WORK/model20"
"$WORK/source/tools/oracle/build-oracle.sh" --work-dir "$WORK/oracle-work" \
    --prefix "$WORK/oracle" --dbm tkrzw --jobs "${JOBS:-4}" \
    --model-dir "$WORK/model20/extracted"
(
    cd "$WORK/source"
    # Repository rust-toolchain.toml selects the required toolchain locally.
    tools/packaging/install.sh libpinyin --prefix="$WORK/engine" --libdir="$WORK/engine/lib" \
        -- --release --no-default-features --features tkrzw
    cargo run --release -p oxpinyin-datagen --no-default-features --features tkrzw -- \
        compile --backend tkrzw --model-dir "$WORK/model20/extracted" \
        --out-dir "$WORK/engine/lib/libpinyin/data"
)
printf '%s\n' "$OXPINYIN_COMMIT" > "$WORK/engine-commit.txt"
sha256sum "$WORK/engine/lib/libpinyin.so.15" > "$WORK/engine-library.sha256"

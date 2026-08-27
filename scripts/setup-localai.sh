#!/bin/sh
# setup-localai.sh - download + run LocalAI (prebuilt binary, no root/Docker) so
# jichi's generate_image / generate_audio / transcribe_audio tools have a local
# OpenAI-compatible backend. Tested target: an NVIDIA GPU box (RTX 4070 Ti SUPER,
# 16 GB) with no container runtime. See docs/MEDIA_GEN.md.
#
# LocalAI ships as a single ~150 MB binary that pulls its inference *backends*
# and *models* from a gallery at runtime (GPU backends included when a CUDA
# runtime is present). This script fetches the binary, starts the server on
# 127.0.0.1:8080, and installs a TTS voice and an image model from the gallery.
#
# Usage:
#   scripts/setup-localai.sh                 # install + run (foreground)
#   scripts/setup-localai.sh --dry-run       # print what it would do
#   scripts/setup-localai.sh --no-models     # binary + server only
#   LOCALAI_VERSION=v4.6.2 \
#   LOCALAI_TTS=voice-en-us-amy-low \
#   LOCALAI_IMAGE=stablediffusion \
#     scripts/setup-localai.sh
#
# Idempotent: re-running reuses an existing binary/models. Ctrl-C stops the
# server. This script is intentionally dependency-light (sh + curl).
set -eu

VERSION="${LOCALAI_VERSION:-v4.6.2}"
PREFIX="${LOCALAI_PREFIX:-$HOME/.local/opt/localai}"
ADDR="${LOCALAI_ADDR:-127.0.0.1:8080}"
# Gallery names (override via env). TTS = piper voice (small, CPU-fine).
# Image = a Stable Diffusion model runnable by the stablediffusion.cpp backend;
# swap for a FLUX gallery entry if you have the diffusers backend + VRAM.
TTS_MODEL="${LOCALAI_TTS:-vits-piper-en_US-amy-sherpa}"
IMAGE_MODEL="${LOCALAI_IMAGE:-sd-1.5-ggml}"

DRY=0
INSTALL_MODELS=1
for arg in "$@"; do
    case "$arg" in
        --dry-run)    DRY=1 ;;
        --no-models)  INSTALL_MODELS=0 ;;
        -h|--help)
            sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

BIN="$PREFIX/local-ai"
ASSET="local-ai-$VERSION-linux-amd64"
URL="https://github.com/mudler/LocalAI/releases/download/$VERSION/$ASSET"

run() { echo "+ $*"; [ "$DRY" -eq 1 ] || "$@"; }

echo "LocalAI setup"
echo "  version : $VERSION"
echo "  prefix  : $PREFIX"
echo "  address : $ADDR"
echo "  models  : $([ "$INSTALL_MODELS" -eq 1 ] && echo "$TTS_MODEL, $IMAGE_MODEL" || echo "(none)")"
echo

# 1. Fetch the binary (skip if present).
run mkdir -p "$PREFIX" "$PREFIX/models"
if [ ! -x "$BIN" ]; then
    echo "Downloading $ASSET (~150 MB)..."
    run curl -fL --progress-bar -o "$BIN" "$URL"
    run chmod +x "$BIN"
else
    echo "Binary already present: $BIN"
fi

if [ "$DRY" -eq 1 ]; then
    echo
    echo "[dry run] would start: $BIN run --address $ADDR --models-path $PREFIX/models"
    [ "$INSTALL_MODELS" -eq 1 ] && \
        echo "[dry run] would install gallery models: $TTS_MODEL $IMAGE_MODEL"
    exit 0
fi

# 2. Report GPU (informational; LocalAI auto-detects CUDA).
if command -v nvidia-smi >/dev/null 2>&1; then
    echo "GPU: $(nvidia-smi --query-gpu=name,memory.total --format=csv,noheader | head -1)"
fi

# 3. Start the server in the background so we can install models, then hand the
#    foreground back to it (so Ctrl-C stops it).
echo "Starting LocalAI on $ADDR ..."
# Keep models AND backends under $PREFIX -- LocalAI otherwise drops backends/ and
# data/ in the *current directory* (a multi-GB surprise inside your project).
"$BIN" run --address "$ADDR" \
    --models-path "$PREFIX/models" \
    --backends-path "$PREFIX/backends" \
    --localai-config-dir "$PREFIX/config" &
SRV=$!
trap 'kill "$SRV" 2>/dev/null || true' INT TERM EXIT

# Wait for the API to answer.
i=0
until curl -fsS "http://$ADDR/readyz" >/dev/null 2>&1 || \
      curl -fsS "http://$ADDR/v1/models" >/dev/null 2>&1; do
    i=$((i + 1))
    [ "$i" -gt 120 ] && { echo "server did not come up in 120s" >&2; exit 1; }
    sleep 1
done
echo "LocalAI is up."

# 4. Install gallery models via the API (backends download on first use).
if [ "$INSTALL_MODELS" -eq 1 ]; then
    for M in "$TTS_MODEL" "$IMAGE_MODEL"; do
        echo "Installing gallery model: $M (this can take a while) ..."
        curl -fsS "http://$ADDR/models/apply" \
            -H 'Content-Type: application/json' \
            -d "{\"id\":\"$M\"}" || \
            echo "  ! could not queue $M (check \`$BIN models list\` for the exact gallery id)"
    done
    echo "Model installs queued. Track progress in the server log above."
fi

echo
echo "Ready. Point jichi at it with examples/config.local-media.json:"
echo "  jichi --config examples/config.local-media.json \\"
echo "    -p 'generate an image of a red bicycle'"
echo
echo "Leaving the server in the foreground (Ctrl-C to stop)."
wait "$SRV"

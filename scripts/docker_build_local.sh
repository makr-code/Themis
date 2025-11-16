#!/usr/bin/env bash
set -euo pipefail

# Local Docker build helper for ThemisDB
# Usage:
#   ./scripts/docker_build_local.sh            # build local image with tag 'themis:local'
#   ./scripts/docker_build_local.sh --tag myrepo/themis:dev --push
#   ./scripts/docker_build_local.sh --no-cache
#
# Environment:
#   DOCKER_REGISTRY (optional) - registry to push to (default: docker hub if --push)
#   DOCKER_USER / DOCKER_PASSWORD (optional) - credentials for docker login (or use `docker login` beforehand)
#

TAG="themis:local"
PUSH=0
NO_CACHE=0
DOCKERFILE="Dockerfile"
BUILD_CONTEXT="."

usage() {
  cat <<EOF
Usage: $0 [--tag <name>] [--push] [--no-cache] [--dockerfile <path>] [--context <path>]

Options:
  --tag <name>         Image tag to build (default: themis:local)
  --push               Push the built image to the registry (requires docker login or DOCKER_USER/DOCKER_PASSWORD)
  --no-cache           Pass --no-cache to docker build
  --dockerfile <path>  Path to Dockerfile to use (default: ./Dockerfile)
  --context <path>     Docker build context (default: .)
  -h, --help           Show this help
EOF
}

# parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag)
      TAG="$2"; shift 2;;
    --push)
      PUSH=1; shift;;
    --no-cache)
      NO_CACHE=1; shift;;
    --dockerfile)
      DOCKERFILE="$2"; shift 2;;
    --context)
      BUILD_CONTEXT="$2"; shift 2;;
    -h|--help)
      usage; exit 0;;
    *)
      echo "Unknown arg: $1"; usage; exit 2;;
  esac
done

# Ensure docker present
if ! command -v docker >/dev/null 2>&1; then
  echo "docker not found. Install Docker or run on a machine with docker available." >&2
  exit 1
fi

# Build args
BUILD_ARGS=("-f" "$DOCKERFILE" "-t" "$TAG")
if [ "$NO_CACHE" -eq 1 ]; then
  BUILD_ARGS+=("--no-cache")
fi

# Start build
echo "Building Docker image tag=$TAG using Dockerfile=$DOCKERFILE context=$BUILD_CONTEXT"
set -x
docker build "${BUILD_ARGS[@]}" "$BUILD_CONTEXT"
set +x

if [ "$PUSH" -eq 1 ]; then
  # push image
  echo "PUSH requested. Ensure you're logged into docker or set DOCKER_USER/DOCKER_PASSWORD env vars."
  if [ -n "${DOCKER_USER:-}" ] && [ -n "${DOCKER_PASSWORD:-}" ]; then
    echo "Logging into registry via DOCKER_USER/DOCKER_PASSWORD"
    echo "$DOCKER_PASSWORD" | docker login --username "$DOCKER_USER" --password-stdin || { echo "docker login failed"; exit 1; }
  fi

  echo "Pushing $TAG"
  docker push "$TAG"
fi

# Final message
echo "Docker build completed: $TAG"
[ "$PUSH" -eq 1 ] && echo "Image pushed." || true

exit 0

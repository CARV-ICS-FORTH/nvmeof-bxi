#!/bin/bash
set -e

# --- Default Credentials ---
HOST_UID=$(id -u)
HOST_GID=$(id -g)
HOST_USER=$(whoami)

# Initialize empty mandatory variables
REPO_DIR=""
WORKSPACE=""

# --- Help Annotation ---
show_help() {
    cat << EOF
Usage: ${0##*/} --repo-dir <PATH> --workspace <PATH> [OPTIONS]

Launch the Docker build environment for compiling the custom KASAN kernel.

Mandatory Arguments:
  -r, --repo-dir PATH      Path to the Git repository containing the scripts and Dockerfile
  -w, --workspace PATH     Path to the untracked workspace directory for heavy build data (kernel sources and build artifacts from phase 1)

Optional Arguments:
  -u, --uid UID            Override the User ID (Default: $(id -u))
  -g, --gid GID            Override the Group ID (Default: $(id -g))
  -n, --name USER          Override the Username (Default: $(whoami))
  -h, --help               Display this help and exit

Example:
  ${0##*/} -r /home1/public/ballis/spdk -w /home1/public/ballis/kernel-kasan
EOF
}

# --- Argument Parsing ---
while [[ "$#" -gt 0 ]]; do
    case $1 in
        -r|--repo-dir) REPO_DIR="$2"; shift ;;
        -w|--workspace) WORKSPACE="$2"; shift ;;
        -u|--uid) HOST_UID="$2"; shift ;;
        -g|--gid) HOST_GID="$2"; shift ;;
        -n|--name) HOST_USER="$2"; shift ;;
        -h|--help) show_help; exit 0 ;;
        *) echo "Unknown parameter passed: $1"; echo ""; show_help; exit 1 ;;
    esac
    shift
done

# --- Validation ---
if [ -z "$REPO_DIR" ] || [ -z "$WORKSPACE" ]; then
    echo "ERROR: Both --repo-dir and --workspace are mandatory parameters."
    echo ""
    show_help
    exit 1
fi

# Ensure absolute paths are provided
if [[ "$REPO_DIR" != /* ]] || [[ "$WORKSPACE" != /* ]]; then
    echo "ERROR: Please provide absolute paths (starting with '/') for directories."
    exit 1
fi

# --- Configuration ---
# Adjust DOCKERFILE_DIR if your Dockerfile is nested (e.g., "${REPO_DIR}/docker")
DOCKERFILE_DIR="${REPO_DIR}/dockerfiles/spdk_build_env"
DOCKER_SCRIPT="${REPO_DIR}/kernel_initiator/kernel-asan-build-instr/02_docker_build.sh"
DOCKER_IMAGE="spdk-build:latest"

# --- Execution ---
echo "Building Docker image from ${DOCKERFILE_DIR}..."
docker build -t ${DOCKER_IMAGE} ${DOCKERFILE_DIR}

echo "Launching Docker build environment for user: ${HOST_USER} (${HOST_UID}:${HOST_GID})"
echo "Repository mapped: ${REPO_DIR}"
echo "Workspace mapped:  ${WORKSPACE}"

# Run the container non-interactively with strict volume mounts
docker run --rm \
  -v "${REPO_DIR}:${REPO_DIR}" \
  -v "${WORKSPACE}:${WORKSPACE}" \
  -e BUILD_UID="${HOST_UID}" \
  -e BUILD_GID="${HOST_GID}" \
  -e BUILD_USER="${HOST_USER}" \
  -e WORKSPACE="${WORKSPACE}" \
  ${DOCKER_IMAGE} \
  /bin/bash "${DOCKER_SCRIPT}"

echo "Docker build complete. Files are staged in ${WORKSPACE}/install"

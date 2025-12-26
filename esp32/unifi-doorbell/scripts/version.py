Import("env")
import subprocess
import os

def get_git_hash():
    """Get short git commit hash"""
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            cwd=env.subst("$PROJECT_DIR")
        )
        if result.returncode == 0:
            return result.stdout.strip()
    except Exception:
        pass
    return None

def is_git_dirty():
    """Check if there are uncommitted changes"""
    try:
        result = subprocess.run(
            ["git", "status", "--porcelain"],
            capture_output=True,
            text=True,
            cwd=env.subst("$PROJECT_DIR")
        )
        if result.returncode == 0:
            return len(result.stdout.strip()) > 0
    except Exception:
        pass
    return False

def get_git_version():
    try:
        # Try to get version from git tag matching esp32-v* pattern
        result = subprocess.run(
            ["git", "describe", "--tags", "--match", "esp32-v*", "--always"],
            capture_output=True,
            text=True,
            cwd=env.subst("$PROJECT_DIR")
        )
        if result.returncode == 0:
            version = result.stdout.strip()
            # Remove 'esp32-v' or 'v' prefix if present
            if version.startswith('esp32-v'):
                version = version[7:]
            elif version.startswith('v'):
                version = version[1:]

            # Always append git hash if not already included
            git_hash = get_git_hash()
            if git_hash and git_hash not in version:
                version = f"{version}-{git_hash}"

            # Append -dirty if there are uncommitted changes
            if is_git_dirty():
                version = f"{version}-dirty"

            return version
    except Exception as e:
        print(f"Warning: Could not get git version: {e}")

    # Check for VERSION environment variable (set by CI)
    if "VERSION" in os.environ:
        return os.environ["VERSION"]

    # Fallback: just use git hash
    git_hash = get_git_hash()
    if git_hash:
        dirty = "-dirty" if is_git_dirty() else ""
        return f"dev-{git_hash}{dirty}"

    return "dev"

version = get_git_version()
print(f"Firmware version: {version}")

env.Append(CPPDEFINES=[
    ("FIRMWARE_VERSION", f'\\"{version}\\"')
])

Import("env")
import subprocess
import os

def get_git_version():
    try:
        # Try to get version from git tag
        result = subprocess.run(
            ["git", "describe", "--tags", "--always"],
            capture_output=True,
            text=True,
            cwd=env.subst("$PROJECT_DIR")
        )
        if result.returncode == 0:
            version = result.stdout.strip()
            # Remove 'v' prefix if present
            if version.startswith('v'):
                version = version[1:]
            return version
    except Exception as e:
        print(f"Warning: Could not get git version: {e}")

    # Check for VERSION environment variable (set by CI)
    if "VERSION" in os.environ:
        return os.environ["VERSION"]

    return "dev"

version = get_git_version()
print(f"Firmware version: {version}")

env.Append(CPPDEFINES=[
    ("FIRMWARE_VERSION", f'\\"{version}\\"')
])
